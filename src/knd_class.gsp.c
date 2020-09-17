#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

/* numeric conversion by strtol */
#include <errno.h>
#include <limits.h>

#include "knd_config.h"
#include "knd_mempool.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_user.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_shared_dict.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
#include "knd_shared_set.h"
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#include <gsl-parser.h>

#define DEBUG_CLASS_GSP_LEVEL_1 0
#define DEBUG_CLASS_GSP_LEVEL_2 0
#define DEBUG_CLASS_GSP_LEVEL_3 0
#define DEBUG_CLASS_GSP_LEVEL_4 0
#define DEBUG_CLASS_GSP_LEVEL_5 0
#define DEBUG_CLASS_GSP_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepo *repo;
    struct kndAttrVar *attr_var;
    struct kndClass *class;
    struct kndClassInst *class_inst;
    struct kndClassVar *class_var;
};

static int export_glosses(struct kndClass *self, struct kndOutput *out)
{
    char idbuf[KND_ID_SIZE];
    size_t id_size = 0;
    struct kndText *t;
    OUT("[_g", strlen("[_g"));
    FOREACH (t, self->tr) {
        OUT("{", 1);
        OUT(t->locale, t->locale_size);
        OUT("{t ", strlen("{t "));
        knd_uid_create(t->seq->numid, idbuf, &id_size);
        OUT(idbuf, id_size);
        // OUT(t->seq, t->seq_size);
        OUT("}}", 2);
    }
    OUT("]", 1);
    return knd_OK;
}

/*static int export_summary(struct kndClass *self,
                          struct kndOutput *out)
{
    struct kndText *tr;
    int err;

    err = out->write(out, "[!_summary", strlen("[!_summary"));
    if (err) return err;

    for (tr = self->tr; tr; tr = tr->next) {
        err = out->write(out, "{", 1);
        if (err) return err;
        err = out->write(out, tr->locale, tr->locale_size);
        if (err) return err;
        err = out->write(out, "{t ", 3);
        if (err) return err;
        err = out->write(out, tr->seq, tr->seq_size);
        if (err) return err;
        err = out->write(out, "}}", 2);
        if (err) return err;
    }
    err = out->write(out, "]", 1);
    if (err) return err;
    return knd_OK;
}
*/

static int export_baseclass_vars(struct kndClass *self, struct kndTask *task, struct kndOutput *out)
{
    struct kndClassVar *item;
    struct kndClass *c;
    int err;

    err = out->write(out, "[_is", strlen("[_is"));                              RET_ERR();
    for (item = self->baseclass_vars; item; item = item->next) {
        err = out->writec(out, '{');                                              RET_ERR();
        c = item->entry->class;
        err = out->write(out, c->entry->id, c->entry->id_size);             RET_ERR();
        if (item->attrs) {
            err = knd_attr_vars_export_GSP(item->attrs, out, task, 0, false);
            if (err) return err;
        }
        err = out->writec(out, '}');                                              RET_ERR();
    }
    err = out->writec(out, ']');                                                  RET_ERR();
    return knd_OK;
}

/*static gsl_err_t read_GSP(struct kndClass *self,
                          const char *rec,
                          size_t *total_size)
{
    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log(".. reading GSP: \"%.*s\"..", 256, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_name,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_g",
          .name_size = strlen("_g"),
          .parse = parse_gloss_array,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_summary",
          .name_size = strlen("_summary"),
          .parse = parse_summary_array,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_ci",
          .name_size = strlen("_ci"),
          .parse = parse_class_var_array,
          .obj = self
        },
        { .name = "inner",
          .name_size = strlen("inner"),
          .parse = parse_inner,
          .obj = self
        },
        { .name = "str",
          .name_size = strlen("str"),
          .parse = parse_str,
          .obj = self
        },
        { .name = "bin",
          .name_size = strlen("bin"),
          .parse = parse_bin,
          .obj = self
        },
        { .name = "num",
          .name_size = strlen("num"),
          .parse = parse_num,
          .obj = self
        },
        { .name = "ref",
          .name_size = strlen("ref"),
          .parse = parse_ref,
          .obj = self
        },
        { .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc,
          .obj = self
        },
        { .name = "text",
          .name_size = strlen("text"),
          .parse = parse_text,
          .obj = self
        },
        { .name = "_desc",
          .name_size = strlen("_desc"),
          .parse = parse_descendants,
          .obj = self
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}
*/

#if 0
static int export_descendants_GSP(struct kndClass *self, struct kndTask *task)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndOutput *out = task->out;
    struct kndSet *set;
    int err;

    set = self->entry->descendants;

    err = out->write(out, "{_desc", strlen("{_desc"));                            RET_ERR();
    buf_size = sprintf(buf, "{tot %zu}", set->num_elems);
    err = out->write(out, buf, buf_size);                                         RET_ERR();

    err = out->write(out, "[c", strlen("[c"));                                    RET_ERR();
    err = set->map(set, export_conc_id_GSP, (void*)out);
    if (err) return err;
    err = out->writec(out, ']');                                                  RET_ERR();

    /*    if (set->facets) {
        err = export_facets_GSP(set, task);                                       RET_ERR();
        } */

    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}
#endif

static int export_class_body_commits(struct kndClass *self,
                                     struct kndClassCommit *unused_var(class_commit),
                                     struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndState *state = self->entry->states;
    struct kndAttr *attr;
    int err;

    switch (state->phase) {
    case KND_CREATED:
        err = out->write(out, "{_new}", strlen("{_new}"));                        RET_ERR();
        break;
    case KND_REMOVED:
        err = out->write(out, "{_rm}", strlen("{_rm}"));                          RET_ERR();
        break;
    default:
        break;
    }

    // TODO

    if (self->tr) {
        err = export_glosses(self, out);                                          RET_ERR();
    }

    if (self->baseclass_vars) {
        err = export_baseclass_vars(self, task, out);                                   RET_ERR();
    }

    if (self->attrs) {
        for (attr = self->attrs; attr; attr = attr->next) {
            err = knd_attr_export(attr, KND_FORMAT_GSP, task);
            if (err) return err;
        }
    }
    
    return knd_OK;
}

static int export_class_inst_commits(struct kndClass *unused_var(self),
                                     struct kndClassCommit *class_commit,
                                     struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndClassInst *inst;
    int err;

    err = out->write(out, "[!inst", strlen("[!inst"));                            RET_ERR();
    for (size_t i = 0; i < class_commit->num_insts; i++) {
        inst = class_commit->insts[i];
        err = out->writec(out, '{');                                              RET_ERR();
        err = out->write(out, inst->entry->id, inst->entry->id_size);             RET_ERR();

        err = out->write(out, "{_n ", strlen("{_n "));                            RET_ERR();
        err = out->write(out, inst->name, inst->name_size);                       RET_ERR();
        err = out->writec(out, '}');                                              RET_ERR();

        //err = inst->export_state(inst, KND_FORMAT_GSP, out);                      RET_ERR();
        err = out->writec(out, '}');                                              RET_ERR();
    }
    err = out->writec(out, ']');                                                  RET_ERR();

    return knd_OK;
}

extern int knd_class_export_commits_GSP(struct kndClass *self,
                                        struct kndClassCommit *class_commit,
                                        struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndCommit *commit = class_commit->commit;
    struct kndState *state = self->entry->states;
    int err;
    
    err = out->writec(out, '{');                                                  RET_ERR();
    err = out->write(out, self->entry->id, self->entry->id_size);                 RET_ERR();
    err = out->write(out, "{_n ", strlen("{_n "));                                RET_ERR();
    err = out->write(out, self->name, self->name_size);                           RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();

    err = out->write(out, "{_st", strlen("{_st"));                                RET_ERR();

    if (state && state->commit == commit) {
        err = out->writec(out, ' ');                                              RET_ERR();

        // TODO
        //err = out->write(out, state->id, state->id_size);                         RET_ERR();

        /* any commits of the class body? */
        err = export_class_body_commits(self, class_commit, task);                 RET_ERR();
    }

    if (self->entry->inst_states) {
        state = self->entry->inst_states;
        /* any commits of the class insts? */
        if (state->commit == commit) {
            err = export_class_inst_commits(self, class_commit, task);             RET_ERR();
        }
    }

    err = out->writec(out, '}');                                                  RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();
    return knd_OK;
}

int knd_class_export_GSP(struct kndClass *self, struct kndTask *task)
{
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size = 0;
    struct kndOutput *out = task->out;
    struct kndAttr *attr;
    struct kndClassEntry *entry = self->entry;
    int err;

    assert(entry->seq != NULL);

    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log(".. GSP export of \"%.*s\" [%.*s]",
                entry->name_size, entry->name, entry->id_size, entry->id);

    knd_uid_create(entry->seq->numid, idbuf, &idbuf_size);
    OUT(idbuf, idbuf_size);

    if (self->tr) {
        err = export_glosses(self, out);
        KND_TASK_ERR("failed to export glosses");
    }
    if (self->baseclass_vars) {
        err = export_baseclass_vars(self, task, out);
        KND_TASK_ERR("failed to export baseclass vars");
    }
    if (self->attrs) {
        for (attr = self->attrs; attr; attr = attr->next) {
            err = knd_attr_export(attr, KND_FORMAT_GSP, task);
            KND_TASK_ERR("failed to export attr");
        }
    }
    /*if (self->entry->descendants) {
        err = export_descendants_GSP(self, task);                                     RET_ERR();
    }
    */
    return knd_OK;
}

#if 0
static gsl_err_t attr_var_alloc(void *accu, struct kndAttrVar **result)
{
    struct LocalContext *ctx = accu;
    struct kndTask *task = ctx->task;
    struct kndAttrVar *self = ctx->attr_var;
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = task->mempool;
    int err;

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) return make_gsl_err_external(err);

    //attr_var->name_size = sprintf(attr_var->name, "%lu", (unsigned long)count);
    attr_var->attr = self->attr;
    attr_var->parent = self;

    *result = attr_var;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t attr_var_append(struct kndAttrVar *self,
                                 struct kndAttrVar *attr_var)
{
    if (!self->list_tail) {
        self->list_tail = attr_var;
        self->list = attr_var;
    }
    else {
        self->list_tail->next = attr_var;
        self->list_tail = attr_var;
    }
    self->num_list_elems++;
    //attr_var->list_count = self->num_list_elems;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_attr_var_item(void *obj,
                                   const char *unused_var(name),
                                   size_t unused_var(name_size))
{
    struct LocalContext *ctx = obj;
    struct kndAttrVar *attr_var;
    gsl_err_t err;

    err = attr_var_alloc(ctx, &attr_var);
    if (err.code) return err;

    return attr_var_append(ctx->attr_var, attr_var);
}

static gsl_err_t check_list_item_id(void *obj,
                                    const char *id, size_t id_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrVar *attr_var = ctx->attr_var;
    struct kndClass *c;
    int err;

    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    memcpy(attr_var->id, id, id_size);
    attr_var->id_size = id_size;

    switch (attr_var->attr->type) {
    case KND_ATTR_REF:
        err = knd_get_class_by_id(ctx->task->repo, id, id_size, &c, ctx->task);
        if (err) {
            knd_log("-- no such class: %.*s", id_size, id);
            return make_gsl_err(gsl_FAIL);
        }

        if (DEBUG_CLASS_GSP_LEVEL_2)
            c->str(c, 1);

        attr_var->class = c;
        attr_var->class_entry = c->entry;
        break;
    case KND_ATTR_INNER:
        // TODO
        //knd_log(".. checking inner item id: %.*s", id_size, id);

        err = knd_get_class_by_id(ctx->task->repo, id, id_size, &c, ctx->task);
        if (err) {
            knd_log("-- no such class: %.*s", id_size, id);
            return make_gsl_err(gsl_FAIL);
        }

        if (DEBUG_CLASS_GSP_LEVEL_2)
            c->str(c, 1);

        attr_var->class = c;
        attr_var->class_entry = c->entry;
        
    default:
        break;
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t read_nested_attr_var(void *obj,
                                      const char *name, size_t name_size,
                                      const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrVar *self = ctx->attr_var;
    struct kndAttrVar *attr_var;
    struct kndAttr *attr;
    struct kndAttrRef *ref;
    struct kndClass *c = NULL;
    struct kndSharedDict *class_name_idx;
    struct kndClassEntry *entry;
    struct kndMemPool *mempool = ctx->task->mempool;
    gsl_err_t parser_err;
    int err;

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_var->parent = self;
    attr_var->class_var = self->class_var;

    attr_var->name = name;
    attr_var->name_size = name_size;

    if (DEBUG_CLASS_GSP_LEVEL_2) {
        knd_log("== reading nested attr item: \"%.*s\" REC: %.*s VAL:%.*s",
                name_size, name, 16, rec, attr_var->val_size, attr_var->val);
    }

    if (!self->attr->ref_class) {
        knd_log("-- no class ref in attr: \"%.*s\"",
                self->attr->name_size, self->attr->name);
        return *total_size = 0, make_gsl_err_external(knd_FAIL);
    }

    c = self->attr->ref_class;

    err = knd_class_get_attr(c, name, name_size, &ref);
    if (err) {
        knd_log("-- no attr \"%.*s\" in class \"%.*s\" :(",
                name_size, name,
                c->entry->name_size, c->entry->name);
        if (err) return *total_size = 0, make_gsl_err_external(err);
    }
    attr = ref->attr;

    switch (attr->type) {
    case KND_ATTR_INNER:
        if (attr->ref_class) break;

        class_name_idx = c->entry->repo->class_name_idx;
        entry = knd_shared_dict_get(class_name_idx, attr->ref_classname, attr->ref_classname_size);
        if (!entry) {
            knd_log("-- inner ref not resolved :( no such class: %.*s",
                    attr->ref_classname_size,
                    attr->ref_classname);
            return *total_size = 0, make_gsl_err(gsl_FAIL);
        }

        if (!entry->class) {
            //err = unfreeze_class(conc, entry, &entry->class);
            //if (err) return *total_size = 0, make_gsl_err_external(err);
        }
        attr->ref_class = entry->class;
        break;
    default:
        break;
    }
    attr_var->attr = attr;

    struct LocalContext nested_ctx = {
        .task = ctx->task,
        .attr_var = ctx->attr_var
    };
    struct gslTaskSpec specs[] = {
        /*{ .is_implied = true,
          .buf = attr_var->val,
          .buf_size = &attr_var->val_size,
          .max_buf_size = sizeof attr_var->val
          }*/
        { .validate = read_nested_attr_var,
          .obj = &nested_ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log("== got attr var value: %.*s => %.*s",
                attr_var->name_size, attr_var->name,
                attr_var->val_size, attr_var->val);

    /* resolve refs */
    switch (attr->type) {
    case KND_ATTR_REF:
        err = knd_get_class_by_id(ctx->task->repo,
                                  attr_var->val, attr_var->val_size, &c, ctx->task);
        if (err) {
            knd_log("-- no such class: %.*s", attr_var->val_size, attr_var->val);
            return make_gsl_err(gsl_FAIL);
        }

        if (DEBUG_CLASS_GSP_LEVEL_2)
            c->str(c, 1);

        attr_var->class = c;
        attr_var->class_entry = c->entry;
        break;
    default:
        break;
    }

    attr_var->next = self->children;
    self->children = attr_var;
    self->num_children++;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_attr_var_item(void *obj,
                                     const char *rec,
                                     size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrVar *attr_var;
    gsl_err_t err;

    err = attr_var_alloc(ctx, &attr_var);
    if (err.code) return *total_size = 0, err;

    struct LocalContext nested_ctx = {
        .task = ctx->task,
        .attr_var = attr_var
    };
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = check_list_item_id,
          .obj = &nested_ctx
        },
        { .validate = read_nested_attr_var,
          .obj = &nested_ctx
        }
    };

    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log(".. parsing nested item: \"%.*s\"", 64, rec);

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    return attr_var_append(ctx->attr_var, attr_var);
}

static gsl_err_t alloc_class_inst_item(struct LocalContext *ctx, struct kndClassInst **item)
{
    struct kndClass *self = ctx->class;
    struct kndClassInst *inst;
    struct kndClassInstEntry *entry;
    struct kndState *state;
    struct kndMemPool *mempool = ctx->task->mempool;
    int err;

    err = knd_class_inst_new(mempool, &inst);
    if (err) {
        knd_log("-- class inst alloc failed :(");
        return make_gsl_err_external(err);
    }

    err = knd_state_new(mempool, &state);
    if (err) {
        knd_log("-- state alloc failed :(");
        return make_gsl_err_external(err);
    }
    state->phase = KND_CREATED;
    state->next = inst->states;
    inst->states = state;

    err = knd_class_inst_entry_new(mempool, &entry);
    if (err) return make_gsl_err_external(err);

    inst->entry = entry;
    entry->inst = inst;
    inst->blueprint = self->entry;

    *item = inst;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t append_class_inst_item(struct LocalContext *ctx, struct kndClassInst *inst)
{
    struct kndClass *self = ctx->class;
    struct kndSharedDict *name_idx = atomic_load_explicit(&self->entry->inst_name_idx, memory_order_acquire);
    //struct kndSharedSet *idx = atomic_load_explicit(&self->entry->inst_idx, memory_order_acquire);
    struct kndMemPool *mempool = ctx->task->mempool;
    struct kndSharedDictItem *item;
    int err;

    if (DEBUG_CLASS_GSP_LEVEL_2) {
        knd_log(".. append inst: %.*s %.*s", inst->entry->id_size, inst->entry->id,
                inst->entry->name_size, inst->entry->name);
        knd_class_inst_str(inst, 0);
    }

    // TODO atomic
    /*if (!name_idx) {
        err = knd_shared_dict_new(&self->entry->inst_name_idx, KND_MEDIUM_DICT_SIZE);
        if (err) return make_gsl_err_external(err);
        name_idx = self->entry->inst_name_idx;
        }*/

    err = knd_shared_dict_set(name_idx, inst->name, inst->name_size, (void*)inst->entry, mempool,
                              ctx->task->ctx->commit, &item, false);
    if (err) return make_gsl_err_external(err);

    self->entry->num_insts++;

    /* index by id */
    /*if (!set) {
        err = knd_set_new(mempool, &set);
        if (err) return make_gsl_err_external(err);
        set->type = KND_SET_CLASS_INST;
        self->entry->inst_idx = set;
    }

    err = set->add(set, inst->entry->id, inst->entry->id_size, (void*)inst->entry);
    if (err) {
        knd_log("-- failed to commit the class inst idx");
        return make_gsl_err_external(err);
        }*/
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_class_inst_id(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndClassInst *self = ctx->class_inst;

    memcpy(self->entry->id, name, name_size);
    self->entry->id_size = name_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_class_inst_name(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndClassInst *self = ctx->class_inst;

    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log(".. class inst name: %.*s ..", name_size, name);

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= sizeof self->entry->name) return make_gsl_err(gsl_LIMIT);

    self->entry->name = name;
    self->entry->name_size = name_size;

    self->name = self->entry->name;
    self->name_size = self->entry->name_size;
    
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_inst_state(void *obj,
                                        const char *rec,
                                        size_t *total_size)
{
    struct LocalContext *ctx = obj;
    return kndClassInst_read_state(ctx->class_inst, rec, total_size, ctx->task);
}

static gsl_err_t parse_class_inst_item(void *obj,
                                       const char *rec,
                                       size_t *total_size)
{
    struct LocalContext *ctx = obj;
    gsl_err_t err;

    err = alloc_class_inst_item(ctx, &ctx->class_inst);
    if (err.code) return *total_size = 0, err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_inst_id,
          .obj = ctx
        },
        { .name = "_n",
          .name_size = strlen("_n"),
          .run = set_class_inst_name,
          .obj = ctx
        }/*,
        { .name = "_st",
          .name_size = strlen("_st"),
          .parse = parse_class_inst_state,
          .obj = ctx
          }*/
    };

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    return append_class_inst_item(ctx, ctx->class_inst);
}

static void append_attr_var(struct kndClassVar *ci, struct kndAttrVar *attr_var)
{
    struct kndAttrVar *curr_var;

    for (curr_var = ci->attrs; curr_var; curr_var = curr_var->next) {
        if (curr_var->name_size != attr_var->name_size) continue;
        if (!memcmp(curr_var->name, attr_var->name, attr_var->name_size)) {
            if (!curr_var->list_tail) {
                curr_var->list_tail = attr_var;
                curr_var->list = attr_var;
            }
            else {
                curr_var->list_tail->next = attr_var;
                curr_var->list_tail = attr_var;
            }
            curr_var->num_list_elems++;
            return;
        }
    }

    if (!ci->tail) {
        ci->tail  = attr_var;
        ci->attrs = attr_var;
    }
    else {
        ci->tail->next = attr_var;
        ci->tail = attr_var;
    }
    ci->num_attrs++;
}

static gsl_err_t validate_attr_var(void *obj, const char *name, size_t name_size, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndClassVar *class_var = ctx->class_var;
    struct kndAttrVar *attr_var;
    struct kndAttr *attr;
    struct kndAttrRef *ref;
    struct kndRepo *repo = class_var->entry->repo;
    struct kndMemPool *mempool = ctx->task->mempool;
//   struct kndAttrVarCtx ctx;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log(".. class var \"%.*s\" to validate attr var: %.*s..",
                class_var->entry->name_size, class_var->entry->name,
                name_size, name);

    if (!class_var->entry->class) {
        knd_log("-- class var not yet resolved: %.*s",
                class_var->entry->name_size, class_var->entry->name);
        return make_gsl_err(gsl_FAIL);
    }

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_var->class_var = class_var;

    err = knd_class_get_attr(class_var->entry->class,
                             name, name_size, &ref);
    if (err) {
        knd_log("-- no attr \"%.*s\" in class \"%.*s\"",
                name_size, name,
                class_var->entry->name_size, class_var->entry->name);
        return *total_size = 0, make_gsl_err_external(err);
    }
    attr = ref->attr;
    attr_var->attr = attr;
    attr_var->name = name;
    attr_var->name_size = name_size;

    struct gslTaskSpec attr_var_spec = {
        .is_list_item = true,
        .run = run_attr_var_item,
        .obj = attr_var
    };

    struct gslTaskSpec specs[] = {
        /*{ .is_implied = true,
          .buf = attr_var->val,
          .buf_size = &attr_var->val_size,
          .max_buf_size = sizeof attr_var->val
          },*/
        { .validate = read_nested_attr_var,
          .obj = attr_var
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "r",
          .name_size = strlen("r"),
          .parse = gsl_parse_array,
          .obj = &attr_var_spec
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    switch (attr->type) {
    case KND_ATTR_PROC_REF:
        if (DEBUG_CLASS_GSP_LEVEL_2)
            knd_log("== proc attr: %.*s => %.*s",
                    attr_var->name_size, attr_var->name,
                    attr_var->val_size, attr_var->val);

        err = knd_get_proc(repo,
                           attr_var->val,
                           attr_var->val_size, &attr_var->proc, ctx->task);
        if (err) return make_gsl_err_external(err);
        break;
    default:
        break;
    }

    append_attr_var(class_var, attr_var);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t validate_attr_var_list(void *obj,
                                         const char *name, size_t name_size,
                                         const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndClassVar *class_var = ctx->class_var;
    struct kndAttrVar *attr_var;
    struct kndAttr *attr;
    struct kndAttrRef *ref;
    struct kndSharedDict *class_name_idx;
    struct kndClassEntry *entry;
    struct kndMemPool *mempool = ctx->task->mempool;
    int err;

    if (DEBUG_CLASS_GSP_LEVEL_1)
        knd_log("\n.. \"%.*s\" class to validate list attr: %.*s..",
                class_var->entry->name_size, class_var->entry->name,
                name_size, name);

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    err = knd_class_get_attr(class_var->entry->class,
                             name, name_size, &ref);
    if (err) {
        knd_log("-- no attr \"%.*s\" in class \"%.*s\"",
                name_size, name,
                class_var->entry->name_size, class_var->entry->name);
        return *total_size = 0, make_gsl_err_external(err);
    }
    attr = ref->attr;
    attr_var->attr = attr;
    attr_var->class_var = class_var;

    attr_var->name = name;
    attr_var->name_size = name_size;

    switch (attr->type) {
    case KND_ATTR_REF:
        //knd_log("== array of refs: %.*s", name_size, name);
        break;
    case KND_ATTR_INNER:
        if (attr->ref_class) break;

        // TODO
        class_name_idx = class_var->entry->repo->class_name_idx;
        entry = knd_shared_dict_get(class_name_idx, attr->ref_classname, attr->ref_classname_size);
        if (!entry) {
            knd_log("-- inner ref not resolved :( no such class: %.*s",
                    attr->ref_classname_size, attr->ref_classname);
            return *total_size = 0, make_gsl_err(gsl_FAIL);
        }

        if (!entry->class) {
            //err = unfreeze_class(class_var->root_class, entry, &entry->class);
            //if (err) return *total_size = 0, make_gsl_err_external(err);
        }
        attr->ref_class = entry->class;
        break;
    default:
        break;
    }

    struct gslTaskSpec attr_var_spec = {
        .is_list_item = true,
        .parse = parse_attr_var_item,
        .obj = ctx
    };

    append_attr_var(class_var, attr_var);

    return gsl_parse_array(&attr_var_spec, rec, total_size);
}

static gsl_err_t set_class_var_baseclass(void *obj, const char *id, size_t id_size)
{
    struct LocalContext *ctx = obj;
    struct kndClassVar *class_var = ctx->class_var;
    struct kndClass *c;
    int err;

    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    memcpy(class_var->id, id, id_size);
    class_var->id_size = id_size;

    err = knd_get_class_by_id(ctx->task->repo, id, id_size, &c, ctx->task);
    if (err) {
        return make_gsl_err(gsl_FAIL);
    }
    class_var->entry = c->entry;

    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log("== conc item baseclass: %.*s (id:%.*s)",
                c->entry->name_size, c->entry->name, id_size, id);

    return make_gsl_err(gsl_OK);
}
#endif

static gsl_err_t parse_baseclass_array_item(void *obj, const char *unused_var(rec), size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndClass *self = ctx->class;
    struct kndClassVar *class_var;
    struct kndMemPool *mempool = ctx->task->mempool;
    int err;

    err = knd_class_var_new(mempool, &class_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    ctx->class_var = class_var;

    /*struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_var_baseclass,
          .obj = ctx
        },
        { .type = GSL_SET_ARRAY_STATE,
          .validate = validate_attr_var_list,
          .obj = ctx
        },
        { .validate = validate_attr_var,
          .obj = ctx
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    knd_calc_num_id(class_var->id, class_var->id_size, &class_var->numid);
    */

    // append
    class_var->next = self->baseclass_vars;
    self->baseclass_vars = class_var;
    self->num_baseclass_vars++;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_baseclass_array(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;

    struct gslTaskSpec cvar_spec = {
        .is_list_item = true,
        .parse = parse_baseclass_array_item,
        .obj = ctx
    };
    return gsl_parse_array(&cvar_spec, rec, total_size);
}

static gsl_err_t check_class_name(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx      = obj;
    struct kndRepo *repo          = ctx->repo;
    // struct kndClassEntry *entry = NULL;

    if (DEBUG_CLASS_GSP_LEVEL_TMP)
        knd_log(".. repo \"%.*s\" to check a class name: \"%.*s\" (size:%zu)",
                repo->name_size, repo->name, name_size, name, name_size);

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);
    return make_gsl_err(gsl_OK);
}

int knd_class_read(struct kndClass *self, const char *rec, size_t *total_size, struct kndTask *task)
{
    if (DEBUG_CLASS_GSP_LEVEL_TMP)
        knd_log(".. reading class GSP: \"%.*s\"..", 64, rec);

    struct LocalContext ctx = {
        .task = task,
        .class = self,
        .repo = task->repo
    };

    /*struct gslTaskSpec inst_commit_spec = {
        .is_list_item = true,
        .parse  = parse_class_inst_item,
        .obj = &ctx
    };
    */
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = check_class_name,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "_g",
          .name_size = strlen("_g"),
          .parse = knd_parse_gloss_array,
          .obj = ctx.task
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "_is",
          .name_size = strlen("_is"),
          .parse = parse_baseclass_array,
          .obj = &ctx
        }/*,
        { .name = "inst",
          .name_size = strlen("inst"),
          .parse = gsl_parse_array,
          .obj = &inst_commit_spec
          }*/
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err.code;

    // glosses

    return knd_OK;
}

int knd_class_marshall(void *elem, size_t *output_size, struct kndTask *task)
{
    struct kndClassEntry *entry = elem;
    struct kndOutput *out = task->out;
    size_t orig_size = out->buf_size;
    int err;

    knd_log(".. marshall class entry: %.*s", entry->name_size, entry->name);

    assert(entry->class != NULL);

    err = knd_class_export_GSP(entry->class, task);
    KND_TASK_ERR("failed to export class GSP");

    if (DEBUG_CLASS_GSP_LEVEL_TMP)
        knd_log("== GSP of %.*s (%.*s)  size:%zu", 
                entry->class->name_size,  entry->class->name, entry->id_size, entry->id,
                out->buf_size - orig_size);

    *output_size = out->buf_size - orig_size;
    return knd_OK;
}

int knd_class_entry_unmarshall(const char *elem_id, size_t elem_id_size,
                               const char *rec, size_t rec_size, void **result, struct kndTask *task)
{
    struct kndMemPool *mempool = task->user_ctx ? task->user_ctx->mempool : task->mempool;
    struct kndClassEntry *entry = NULL;
    struct kndRepo *repo = task->repo;
    struct kndCharSeq *seq;
    struct kndSharedDictItem *item;
    const char *c, *name = rec;
    size_t name_size;
    int err;

    if (DEBUG_CLASS_GSP_LEVEL_TMP)
        knd_log(">> GSP class entry \"%.*s\" => \"%.*s\"", elem_id_size, elem_id, rec_size, rec);

    err = knd_class_entry_new(mempool, &entry);
    KND_TASK_ERR("failed to alloc a class entry");
    entry->repo = task->repo;
    memcpy(entry->id, elem_id, elem_id_size);
    entry->id_size = elem_id_size;

    /* get name numid */
    c = name;
    while (*c) {
        if (*c == '{' || *c == '[') break;
        c++;
    }
    name_size = c - name;
    if (!name_size) {
        err = knd_FORMAT;
        KND_TASK_ERR("anonymous class entry in GSP");
    }
    if (name_size > KND_ID_SIZE) {
        err = knd_FORMAT;
        KND_TASK_ERR("invalid class name numid in GSP");
    }
    err = knd_shared_set_get(repo->str_idx, name, name_size, (void**)&seq);
    if (err) {
        err = knd_FAIL;
        KND_TASK_ERR("failed to decode class name numid \"%.*s\"", name_size, name);
    }
    entry->name = seq->val;
    entry->name_size = seq->val_size;
    entry->seq = seq;

    err = knd_shared_dict_set(repo->class_name_idx, entry->name, entry->name_size,
                              (void*)entry, task->mempool, NULL, &item, false);
    KND_TASK_ERR("failed to register class name");
    entry->dict_item = item;

    if (DEBUG_CLASS_GSP_LEVEL_TMP)
        knd_log("== class name decoded \"%.*s\" => \"%.*s\"",
                elem_id_size, elem_id, entry->name_size, entry->name);

    *result = entry;
    return knd_OK;
}

int knd_class_unmarshall(const char *elem_id, size_t elem_id_size,
                         const char *rec, size_t rec_size, void **result, struct kndTask *task)
{
    struct kndMemPool *mempool = task->user_ctx ? task->user_ctx->mempool : task->mempool;
    struct kndClass *c = NULL;
    size_t total_size = rec_size;
    int err;

    if (DEBUG_CLASS_GSP_LEVEL_TMP)
        knd_log(">> GSP class \"%.*s\" => \"%.*s\"", elem_id_size, elem_id, rec_size, rec);

    err = knd_class_new(mempool, &c);
    KND_TASK_ERR("failed to alloc a class");

    err = knd_class_read(c, rec, &total_size, task);
    KND_TASK_ERR("failed to read GSP class rec");

    *result = c;
    return knd_OK;
}

