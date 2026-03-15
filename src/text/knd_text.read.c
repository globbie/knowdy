#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>

#include "knd_text.h"
#include "knd_task.h"
#include "knd_repo.h"
#include "knd_class.h"
#include "knd_proc.h"
#include "knd_shared_set.h"
#include "knd_user.h"
#include "knd_utils.h"
#include "knd_mempool.h"
#include "knd_memblock.h"
#include "knd_output.h"

#define DEBUG_TEXT_READ_LEVEL_0 0
#define DEBUG_TEXT_READ_LEVEL_1 0
#define DEBUG_TEXT_READ_LEVEL_2 0
#define DEBUG_TEXT_READ_LEVEL_3 0
#define DEBUG_TEXT_READ_LEVEL_TMP 1

struct ExternalContext {
    struct kndRepo       *repo;
    struct kndTask       *task;
};

struct LocalContext {
    struct kndRepo       *repo;
    struct kndTask       *task;
    struct kndText       *text;
    struct kndPar        *par;
    struct kndSentence   *sent;
    struct kndClause     *clause;
    struct kndSyNode     *synode;
    struct kndSyNodeSpec *synode_spec;
    struct kndStatement  *stm;
};

static gsl_err_t parse_synode(void *obj, const char *rec, size_t *total_size);

static gsl_err_t set_gloss_locale(void *obj, const char *name, size_t name_size)
{
    struct kndText *t = obj;
    if (name_size >= KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    memcpy(t->locale_id, name, name_size);
    t->locale_id_size = name_size;

    t->locale = t->locale_id;
    t->locale_size = t->locale_id_size;
    
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_gloss_id(void *obj, const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndText *t = ctx->text;
    int err;

    assert(val_size != 0);

    if (val_size > KND_ID_SIZE) {
        err = knd_LIMIT;
        KND_TASK_LOG("id size exceeds limit: %.*s", val_size, val);
        return make_gsl_err_external(err);
    }

    memcpy(t->id, val, val_size);
    t->id_size = val_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_gloss_abbr(void *obj, const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    int err;

    assert(val_size != 0);

    err = knd_charseq_decode(val, val_size, &ctx->text->abbr, task);
    if (err) {
        KND_TASK_LOG("failed to decode a gloss abbr charseq %.*s", val_size, val);
        return make_gsl_err_external(err);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t read_gloss_item(void *obj, const char *rec, size_t *total_size)
{
    struct ExternalContext *ext_ctx = obj;
    struct kndTask *task = ext_ctx->task;
    struct kndRepo *repo = ext_ctx->repo;
    struct kndText *t;
    int err;

    err = knd_text_new(&t, task->mempool);
    if (err) {
        KND_TASK_LOG("failed to alloc a text");
        return *total_size = 0, make_gsl_err_external(err);
    }

    struct LocalContext ctx = {
        .task = task,
        .repo = repo,
        .text = t
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_gloss_locale,
          .obj = t
        },
        { .name = "t",
          .name_size = strlen("t"),
          .run = set_gloss_id,
          .obj = &ctx
        },
        { .name = "abbr",
          .name_size = strlen("abbr"),
          .run = set_gloss_abbr,
          .obj = &ctx
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    /* make sure text ids are set */
    if (t->locale_id_size == 0 || t->id_size == 0)
        return make_gsl_err(gsl_FORMAT);

    if (DEBUG_TEXT_READ_LEVEL_3) {
        knd_log(".. gloss translation: {locale %.*s}  {text-id %.*s}",
                t->locale_size, t->locale, t->id_size, t->id);
    }
    // append
    t->next = task->ctx->tr;
    task->ctx->tr = t;
    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_read_gloss_array(void *obj, const char *rec, size_t *total_size)
{
    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .parse = read_gloss_item,
        .obj = obj
    };
    return gsl_parse_array(&item_spec, rec, total_size);
}

static gsl_err_t set_text_lang(void *obj, const char *val, size_t val_size)    
{
    struct LocalContext *ctx = obj;
    struct kndText *self = ctx->text;
    self->locale_size = val_size;
    self->locale = val;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_text_seq(void *obj, const char *val, size_t val_size)    
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    int err;

    if (DEBUG_TEXT_READ_LEVEL_2)
        knd_log(">> text encoded seq: %.*s (size:%zu)", val_size, val, val_size);

    err = knd_charseq_decode(val, val_size, &ctx->text->seq, task);
    if (err) {
        KND_TASK_LOG("failed to decode a text charseq %.*s", val_size, val);
        return make_gsl_err_external(err);
    }

    if (DEBUG_TEXT_READ_LEVEL_3) {
        knd_log(">> locale: %.*s text seq:%.*s", ctx->text->locale_size, ctx->text->locale,
            ctx->text->seq->val_size, ctx->text->seq->val);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_synode_spec_class(void *obj, const char *name, size_t name_size)    
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndRepo *repo = ctx->repo;
    struct kndSyNodeSpec *spec = ctx->synode_spec;
    int err;

    spec->name = name;
    spec->name_size = name_size;

    err = knd_get_cls_by_name(repo, name, name_size, &spec->class, ctx->task);
    if (err) {
        KND_TASK_LOG("no such {cls %.*s}", name_size, name);
        return make_gsl_err(gsl_NO_MATCH);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_synode_class(void *obj, const char *name, size_t name_size)    
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndRepo *repo = ctx->repo;
    struct kndSyNode *synode = ctx->synode;
    int err;

    synode->name = name;
    synode->name_size = name_size;

    err = knd_get_cls_by_name(repo, name, name_size, &synode->role, ctx->task);
    if (err) {
        KND_TASK_LOG("no such {cls %.*s}", name_size, name);
        return make_gsl_err(gsl_NO_MATCH);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_clause_class(void *obj, const char *val, size_t val_size)    
{
    struct LocalContext *ctx = obj;
    struct kndClause *self = ctx->clause;
    self->name = val;
    self->name_size = val_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_sent_seq(void *obj, const char *val, size_t val_size)    
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndSentence *sent = ctx->sent;
    int err;
    if (!val_size) return make_gsl_err(gsl_FORMAT);

    err = knd_charseq_decode(val, val_size, &sent->seq, task);
    if (err) {
        KND_TASK_LOG("failed to decode a sent charseq %.*s", val_size, val);
        return make_gsl_err_external(err);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_statement_schema(void *obj, const char *val, size_t val_size)    
{
    struct LocalContext *ctx = obj;
    struct kndStatement *stm = ctx->stm;
    stm->schema_name = val;
    stm->schema_name_size = val_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_synode_spec(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->mempool;
    struct kndSyNode *base_synode = ctx->synode;
    struct kndSyNodeSpec *spec;
    gsl_err_t parser_err;
    int err;

    err = knd_synode_spec_new(&spec, mempool);
    if (err) return make_gsl_err_external(err);
    ctx->synode_spec = spec;
    ctx->synode = NULL;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_synode_spec_class,
          .obj = obj
        },
        { .name = "syn",
          .name_size = strlen("syn"),
          .parse = parse_synode,
          .obj = obj
        }
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    base_synode->spec = spec;
    ctx->synode = base_synode;
    ctx->synode_spec = NULL;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_term_synode(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->mempool;
    struct kndSyNode *base_synode = ctx->synode;
    struct kndSyNode *synode;
    gsl_err_t parser_err;
    int err;

    err = knd_synode_new(&synode, mempool);
    if (err) return make_gsl_err_external(err);
    synode->is_terminal = true;
    base_synode->topic = synode;

    ctx->synode = synode;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_synode_class,
          .obj = ctx
        },
        { .name = "pos",
          .name_size = strlen("pos"),
          .parse = gsl_parse_size_t,
          .obj = &synode->linear_pos
        },
        { .name = "len",
          .name_size = strlen("len"),
          .parse = gsl_parse_size_t,
          .obj = &synode->linear_len
        }
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        return parser_err;
    }

    ctx->synode = base_synode;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_synode(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->mempool;
    struct kndSyNode *base_synode = ctx->synode;
    struct kndSyNodeSpec *spec = ctx->synode_spec;
    struct kndSyNode *synode;
    gsl_err_t parser_err;
    int err;

    err = knd_synode_new(&synode, mempool);
    if (err) return make_gsl_err_external(err);
    ctx->synode = synode;
    ctx->synode_spec = NULL;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_synode_class,
          .obj = obj
        },
        { .name = "syn",
          .name_size = strlen("syn"),
          .parse = parse_synode,
          .obj = obj
        },
        { .name = "pos",
          .name_size = strlen("pos"),
          .parse = gsl_parse_size_t,
          .obj = &synode->linear_pos
        },
        { .name = "len",
          .name_size = strlen("len"),
          .parse = gsl_parse_size_t,
          .obj = &synode->linear_len
        },
        { .name = "term",
          .name_size = strlen("term"),
          .parse = parse_term_synode,
          .obj = obj
        },
        { .name = "spec",
          .name_size = strlen("spec"),
          .parse = parse_synode_spec,
          .obj = obj
        }
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (base_synode)
        base_synode->topic = synode;
    if (spec)
        spec->synode = synode;
    
    ctx->synode = base_synode;
    ctx->synode_spec = spec;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_subj(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndClause *clause = ctx->clause;
    struct kndMemPool *mempool = task->mempool;
    struct kndSyNode *synode;
    gsl_err_t parser_err;
    int err;

    err = knd_synode_new(&synode, mempool);
    if (err) return make_gsl_err_external(err);
    ctx->synode = synode;
    ctx->synode_spec = NULL;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_synode_class,
          .obj = obj
        },
        { .name = "syn",
          .name_size = strlen("syn"),
          .parse = parse_synode,
          .obj = obj
        },
        { .name = "term",
          .name_size = strlen("term"),
          .parse = parse_term_synode,
          .obj = obj
        },
        { .name = "spec",
          .name_size = strlen("spec"),
          .parse = parse_synode_spec,
          .obj = obj
        }
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        return parser_err;
    }
    clause->subj = synode;
    ctx->clause = clause;
    ctx->synode = NULL;
    ctx->synode_spec = NULL;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_pred(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndClause *clause = ctx->clause;
    struct kndMemPool *mempool = task->mempool;
    struct kndSyNode *synode;
    gsl_err_t parser_err;
    int err;

    err = knd_synode_new(&synode, mempool);
    if (err) return make_gsl_err_external(err);
    ctx->synode = synode;
    ctx->synode_spec = NULL;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_synode_class,
          .obj = obj
        },
        { .name = "syn",
          .name_size = strlen("syn"),
          .parse = parse_synode,
          .obj = obj
        }
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    clause->pred = synode;
    ctx->clause = clause;
    ctx->synode = NULL;
    ctx->synode_spec = NULL;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_clause(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndSentence *sent = ctx->sent;
    // struct kndClause *parent_clause = ctx->clause;
    struct kndClause *clause;
    struct kndMemPool *mempool = task->mempool;
    gsl_err_t parser_err;
    int err;

    err = knd_clause_new(&clause, mempool);
    if (err) return make_gsl_err_external(err);
    if (!sent->clause)
        sent->clause = clause;
    ctx->clause = clause;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_clause_class,
          .obj = obj
        }, /* subclauses */
        { .name = "clause",
          .name_size = strlen("clause"),
          .parse = parse_clause,
          .obj = obj
        },
        { .name = "syn",
          .name_size = strlen("syn"),
          .parse = parse_subj,
          .obj = obj
        },
        { .name = "pred",
          .name_size = strlen("pred"),
          .parse = parse_pred,
          .obj = obj
        }, /* exclamations etc */
        /* { .name = "phono",
          .name_size = strlen("phono"),
          .parse = parse_phone,
          .obj = obj
          }*/
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    /*if (par->last_sent)
        par->last_sent->next = sent;
    else
        par->sents = sent;

    par->last_sent = sent;
    par->num_sents++;
    sent->numid = par->num_sents;
    */
    ctx->clause = NULL;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_select(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndRepo *repo = ctx->repo;
    knd_task_type orig_task_type = task->type;
    gsl_err_t parser_err;

    /* switch to statement's local scope */
    task->type = KND_TASK_INNER;
    parser_err = knd_class_select(rec, total_size, repo, task);
    task->type = orig_task_type;

    return parser_err;
}

static gsl_err_t parse_proc_select(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndRepo *repo = ctx->repo;
    knd_task_type orig_task_type = task->type;
    gsl_err_t parser_err;

    knd_log("proc inner state  {repo %.*s}", repo->name_size, repo->name);

    /* switch to statement's local scope */
    task->type = KND_TASK_INNER;
    parser_err = knd_proc_select(rec, total_size, repo, task);
    task->type = orig_task_type;
    return parser_err;
}

static gsl_err_t parse_statement(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndSentence *sent = ctx->sent;
    struct kndMemPool *mempool = task->mempool;
    struct kndStatement *stm;
    gsl_err_t parser_err;
    int err;

    err = knd_statement_new(&stm, mempool);
    if (err) return make_gsl_err_external(err);
    sent->stm = stm;

    parser_err = knd_statement_read(stm, rec, total_size, task);
    if (parser_err.code) {
        KND_TASK_LOG("text stm read failed");
        return *total_size = 0, parser_err;
    }

    err = knd_statement_resolve(stm, task);
    if (err) return make_gsl_err_external(err);
    
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_sentence(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndPar *par = ctx->par;
    struct kndSentence *sent;
    struct kndMemPool *mempool = task->mempool;
    gsl_err_t parser_err;
    int err;

    err = knd_sentence_new(&sent, mempool);
    if (err) return make_gsl_err_external(err);
    ctx->sent = sent;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_sent_seq,
          .obj = ctx
        },
        { .name = "clause",
          .name_size = strlen("clause"),
          .parse = parse_clause,
          .obj = ctx
        },
        { .name = "stm",
          .name_size = strlen("stm"),
          .parse = parse_statement,
          .obj = ctx
        }
    };
    
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (par->last_sent)
        par->last_sent->next = sent;
    else
        par->sents = sent;

    par->last_sent = sent;
    par->num_sents++;
    sent->numid = par->num_sents;
    ctx->sent = sent;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_sent_array(void *obj, const char *rec, size_t *total_size)
{
    if (DEBUG_TEXT_READ_LEVEL_2)
        knd_log(".. parse sentence array");

    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .parse = parse_sentence,
        .obj = obj
    };
    return gsl_parse_array(&item_spec, rec, total_size);
}

static gsl_err_t set_par_numid(void *obj, const char *val, size_t val_size)    
{
    struct LocalContext *ctx = obj;
    struct kndPar *self = ctx->par;
    char buf[KND_NAME_SIZE];
    long numval;
    int err;

    if (val_size >= KND_NAME_SIZE) return make_gsl_err(gsl_FAIL);
    memcpy(buf, val, val_size);
    buf[val_size] = '\0';
            
    err = knd_parse_int(buf, &numval);
    if (err) return make_gsl_err_external(err);
    self->numid = (size_t)numval;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_valid_par(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct LocalContext *ctx = obj;
    struct kndPar *par = ctx->par;
    if (!par->num_sents) {
        knd_log("NB: empty text par #zu", par->numid);
        return make_gsl_err(gsl_FORMAT);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_statement(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;

    if (!task->ctx->declars) {
        knd_log("-- empty stm");
        KND_TASK_LOG("empty statement");
        return make_gsl_err(gsl_FORMAT);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_par(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndText *text = ctx->text;
    struct kndPar *par;
    struct kndMemPool *mempool = task->mempool;
    gsl_err_t parser_err;
    int err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_par_numid,
          .obj = obj
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "s",
          .name_size = strlen("s"),
          .parse = parse_sent_array,
          .obj = obj
        },
        { .is_default = true,
          .run = confirm_valid_par,
          .obj = obj
        }
    };

    err = knd_par_new(&par, mempool);
    if (err) return make_gsl_err_external(err);
    ctx->par = par;
    ctx->sent = NULL;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        switch (parser_err.code) {
        case gsl_NO_MATCH:
            KND_TASK_LOG("text par got an unrecognized tag \"%.*s\"", parser_err.val_size, parser_err.val);
            break;
        default:
            break;
        }
        return parser_err;
    }
    if (!par->num_sents) {
        KND_TASK_LOG("empty paragraphs not accepted");
        return make_gsl_err_external(err);
    }
    
    if (text->last_par)
        text->last_par->next = par;
    else
        text->pars = par;

    text->last_par = par;
    text->num_pars++;
    par->numid = text->num_pars;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_par_array(void *obj, const char *rec, size_t *total_size)
{
    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .parse = parse_par,
        .obj = obj
    };
    return gsl_parse_array(&item_spec, rec, total_size);
}

static gsl_err_t parse_translation(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndText *orig_text = ctx->text;
    struct kndText *trn;
    struct kndMemPool *mempool = task->mempool;
    gsl_err_t parser_err;
    int err;

    err = knd_text_new(&trn, mempool);
    if (err) {
        KND_TASK_LOG("failed to alloc a text");
        return *total_size = 0, make_gsl_err_external(err);
    }
    ctx->text = trn;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_gloss_locale,
          .obj = trn
        },
        { .name = "t",
          .name_size = strlen("t"),
          .run = set_gloss_id,
          .obj = ctx
        }
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        switch (parser_err.code) {
        case gsl_NO_MATCH:
            KND_TASK_LOG("text trn got an unrecognized {tag %.*s}",
                         parser_err.val_size, parser_err.val);
            break;
        default:
            break;
        }
        return parser_err;
    }
    trn->next = orig_text->trs;
    orig_text->trs = trn;
    ctx->text = orig_text;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_translation_array(void *obj, const char *rec, size_t *total_size)
{
    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .parse = parse_translation,
        .obj = obj
    };
    return gsl_parse_array(&item_spec, rec, total_size);
}

gsl_err_t knd_statement_read(struct kndStatement *stm, const char *rec, size_t *total_size, struct kndTask *task)
{
    if (DEBUG_TEXT_READ_LEVEL_2)
        knd_log(".. read statement: \"%.*s\"", 64, rec);

    struct LocalContext ctx = {
        .task = task,
        .stm = stm
    };
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = set_statement_schema,
          .obj = &ctx
        },
        { .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_select,
          .obj = &ctx
        },
        { .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc_select,
          .obj = &ctx
        },
        { .is_default = true,
          .run = confirm_statement,
          .obj = &ctx
        }
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        return parser_err;
    }
    stm->declars = task->ctx->declars;
    // stm->proc_declars = task->ctx->proc_declars;
    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_text_read(struct kndText *self, const char *rec, size_t *total_size, struct kndTask *task)
{
    if (DEBUG_TEXT_READ_LEVEL_2)
        knd_log(".. read text: \"%.*s\"", 128, rec);
   
    struct LocalContext ctx = {
        .task = task,
        .text = self
    };
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_text_seq,
          .obj = &ctx
        },
        { .name = "lang",
          .name_size = strlen("lang"),
          .run = set_text_lang,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "p",
          .name_size = strlen("p"),
          .parse = parse_par_array,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "trn",
          .name_size = strlen("trn"),
          .parse = parse_translation_array,
          .obj = &ctx
        }
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;
    return make_gsl_err(gsl_OK);
}

int knd_string_unmarshall(const char *elem_id, size_t elem_id_size,
                          const char *rec, size_t rec_size,
                          void *unused_var(ctx), void **result, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndMemBlock *memblock;
    struct kndCharSeq *seq;
    int err;

    if (elem_id_size > KND_ID_SIZE) return knd_LIMIT;

    err = knd_charseq_new(&seq, mempool);
    KND_TASK_ERR("failed to alloc a class to unmarshall");

    memcpy(seq->id, elem_id, elem_id_size);
    seq->id_size = elem_id_size;

    err = knd_memblock_fetch(&memblock, rec_size, task);
    KND_TASK_ERR("failed to fetch a memblock");

    err = knd_memblock_write(memblock, rec, rec_size, false, &seq->val);
    KND_TASK_ERR("failed to to save {seq %.*s}", rec_size, rec);
    seq->val_size = rec_size;

    if (DEBUG_TEXT_READ_LEVEL_3) {
        knd_log(">> {elem %.*s {seq %.*s}}", elem_id_size, elem_id, rec_size, rec);
    }

    *result = seq;
    return knd_OK;
}
