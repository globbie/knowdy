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
    struct kndClass *baseclass;
    struct kndClassRef *class_ref;
    struct kndClassInst *class_inst;
    struct kndClassVar *class_var;
};

int knd_class_inst_idx_fetch(struct kndClass *self, struct kndSharedDict **result, struct kndTask *task)
{
    struct kndOutput *out = task->file_out;
    struct kndSharedSet *idx, *new_idx;
    struct kndSharedDict *name_idx, *new_name_idx;
    struct stat st;
    int err;

    out->reset(out);
    OUT(task->path, task->path_size);
    OUT(task->repo->path, task->repo->path_size);
    err = out->writef(out, "snapshot_%zu/", task->repo->snapshots->numid);
    KND_TASK_ERR("snapshot path construction failed");

    OUT("inst_", strlen("inst_"));
    OUT(self->entry->id, self->entry->id_size);
    OUT(".gsp", strlen(".gsp"));

    if (DEBUG_CLASS_GSP_LEVEL_TMP)
        knd_log(">> open class inst storage in %.*s", out->buf_size, out->buf);

    if (stat(out->buf, &st)) {
        return knd_NO_MATCH;
    }

    if (DEBUG_CLASS_GSP_LEVEL_TMP)
        knd_log(".. reading class inst storage: %.*s [%zu]", out->buf_size, out->buf, (size_t)st.st_size);

    do {
        name_idx = atomic_load_explicit(&self->inst_name_idx, memory_order_acquire);
        if (name_idx) {
            // TODO free new_name_idx if (new_name_idx != NULL) 
            *result = name_idx;
            break;
        }
        err = knd_shared_dict_new(&new_name_idx, KND_MEDIUM_DICT_SIZE);
        KND_TASK_ERR("failed to create inst name idx");
        *result = new_name_idx;
    } while (!atomic_compare_exchange_weak(&self->inst_name_idx, &name_idx, new_name_idx));

    do {
        idx = atomic_load_explicit(&self->inst_idx, memory_order_acquire);
        if (idx) {
            // TODO free new_idx if (new_idx != NULL) 
            break;
        }
        err = knd_shared_set_new(NULL, &new_idx);
        KND_TASK_ERR("failed to create inst idx");

    } while (!atomic_compare_exchange_weak(&self->inst_idx, &idx, new_idx));

    task->blueprint = self->entry;
    err = knd_shared_set_unmarshall_file(self->inst_idx, out->buf, out->buf_size,
                                         (size_t)st.st_size, knd_class_inst_entry_unmarshall, task);
    KND_TASK_ERR("failed to unmarshall class inst storage GSP file");
    return knd_OK;
}

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

static int export_ancestors(struct kndClass *self, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndClassRef *ref;
    struct kndClassEntry *entry;

    /* ignore root concept */
    if (self->num_ancestors == 1) {
        if (!self->ancestors->entry->id_size) 
            return knd_OK;
    }
    OUT("[anc", strlen("[anc"));
    FOREACH (ref, self->ancestors) {
        entry = ref->entry;
        OUT("{", 1);
        OUT(entry->id, entry->id_size);
        OUT("}", 1);
    }
    OUT("]", 1);
    return knd_OK;
}


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
    struct kndState *state = self->states;
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

static int export_class_inst_commits(struct kndClass *unused_var(self), struct kndClassCommit *class_commit,
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

int knd_class_export_commits_GSP(struct kndClass *self, struct kndClassCommit *class_commit, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndCommit *commit = class_commit->commit;
    struct kndState *state = self->states;
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
    if (self->inst_states) {
        state = self->inst_states;
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
        FOREACH (attr, self->attrs) {
            err = knd_attr_export(attr, KND_FORMAT_GSP, task);
            KND_TASK_ERR("failed to export attr");
        }
    }

    if (self->num_ancestors) {
        err = export_ancestors(self, task);
        KND_TASK_ERR("failed to export ancestors GSP");
    }
    // insts
    if (self->inst_idx) {
        err = out->writef(out, "{insts %zu}", self->inst_idx->num_elems);
        KND_TASK_ERR("failed to export num insts GSP");
    }
    
    return knd_OK;
}

static gsl_err_t set_baseclass(void *obj, const char *id, size_t id_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndClassVar *class_var = ctx->class_var;
    struct kndRepo *repo = ctx->task->repo;
    struct kndClassEntry *entry;
    int err;

    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);
    // knd_log(">> baseclass: %.*s", id_size, id);

    memcpy(class_var->id, id, id_size);
    class_var->id_size = id_size;

    err = knd_shared_set_get(repo->class_idx, id, id_size, (void**)&entry);
    if (err) {
        KND_TASK_LOG("class \"%.*s\" not found in repo %.*s", id_size, id, repo->name_size, repo->name);
        return make_gsl_err(gsl_FAIL);
    }
    class_var->entry = entry;

    if (DEBUG_CLASS_GSP_LEVEL_3)
        knd_log("== conc item baseclass: %.*s (id:%.*s)", entry->name_size, entry->name, id_size, id);
    
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_ancestor(void *obj, const char *id, size_t id_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndClassRef *ref = ctx->class_ref;
    struct kndRepo *repo = ctx->task->repo;
    struct kndClassEntry *entry;
    int err;

    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    err = knd_shared_set_get(repo->class_idx, id, id_size, (void**)&entry);
    if (err) {
        KND_TASK_LOG("class \"%.*s\" not found in repo %.*s", id_size, id, repo->name_size, repo->name);
        return make_gsl_err(gsl_FAIL);
    }
    ref->entry = entry;

    if (DEBUG_CLASS_GSP_LEVEL_3)
        knd_log("== ancestor: %.*s (id:%.*s)", entry->name_size, entry->name, id_size, id);
    
    return make_gsl_err(gsl_OK);
}

static gsl_err_t read_attr_var(void *obj, const char *name, size_t name_size,
                               const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    int err;
    err = knd_read_attr_var(ctx->class_var, name, name_size, rec, total_size, ctx->task);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_baseclass_array_item(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndClass *self = ctx->class;
    struct kndClassVar *class_var;
    struct kndMemPool *mempool = ctx->task->mempool;
    int err;

    err = knd_class_var_new(mempool, &class_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    ctx->class_var = class_var;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_baseclass,
          .obj = ctx
        }/*,
        { .type = GSL_SET_ARRAY_STATE,
          .validate = validate_attr_var_list,
          .obj = ctx
          }*/,
        { .validate = read_attr_var,
          .obj = ctx
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    knd_calc_num_id(class_var->id, class_var->id_size, &class_var->numid);

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

static gsl_err_t parse_ancestor_array_item(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndClass *self = ctx->class;
    // struct kndClassVar *class_var;
    struct kndMemPool *mempool = ctx->task->mempool;
    struct kndClassRef *ref;
    int err;

    err = knd_class_ref_new(mempool, &ref);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    ctx->class_ref = ref;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_ancestor,
          .obj = ctx
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    // append
    ref->next = self->ancestors;
    self->ancestors = ref;
    self->num_ancestors++;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_ancestor_array(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;

    struct gslTaskSpec cvar_spec = {
        .is_list_item = true,
        .parse = parse_ancestor_array_item,
        .obj = ctx
    };
    return gsl_parse_array(&cvar_spec, rec, total_size);
}

static gsl_err_t check_class_name(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx      = obj;
    struct kndRepo *repo          = ctx->repo;
    // struct kndClassEntry *entry = NULL;

    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log(".. repo \"%.*s\" to check a class name: \"%.*s\" (size:%zu)",
                repo->name_size, repo->name, name_size, name, name_size);

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t read_attr(void *obj, const char *name, size_t name_size, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask    *task = ctx->task;
    struct kndMemPool *mempool = task->user_ctx ? task->user_ctx->mempool : task->mempool;
    struct kndClass *self = ctx->class;
    struct kndAttr *attr;
    struct kndAttrRef *ref;
    struct kndSet     *attr_idx = self->attr_idx;
    const char *c;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CLASS_GSP_LEVEL_TMP)
        knd_log(".. reading attr: \"%.*s\" rec:\"%.*s\"", name_size, name, 32, rec);

    err = knd_attr_new(mempool, &attr);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent_class = self;

    for (size_t i = 0; i < sizeof(knd_attr_names) / sizeof(knd_attr_names[0]); i++) {
        c = knd_attr_names[i];
        if (!memcmp(c, name, name_size)) 
            attr->type = (knd_attr_type)i;
    }

    if (attr->type == KND_ATTR_NONE) {
        KND_TASK_LOG("\"%.*s\" attr is not supported (class \"%.*s\")",
                name_size, name, self->name_size, self->name);
        return *total_size = 0, make_gsl_err_external(err);
    }
    parser_err = knd_attr_read(attr, task, rec, total_size);
    if (parser_err.code) {
        KND_TASK_LOG("failed to read attr \"%.*s\"", name_size, name);
        return parser_err;
    }
    if (attr->is_implied)
        self->implied_attr = attr;

    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    } else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }
    self->num_attrs++;

    /* new attr entry */
    err = knd_attr_ref_new(mempool, &ref);
    if (err) {
        KND_TASK_LOG("failed to alloc an attr ref");
        return *total_size = 0, make_gsl_err_external(err);
    }
    ref->attr = attr;

    err = attr_idx->add(attr_idx, attr->id, attr->id_size, (void*)ref);
    if (err) {
        KND_TASK_LOG("failed to update attr idx of %.*s", self->name_size, self->name);
        return *total_size = 0, make_gsl_err_external(err);
    }
    return make_gsl_err(gsl_OK);
}

int knd_class_read(struct kndClass *self, const char *rec, size_t *total_size, struct kndTask *task)
{
    if (DEBUG_CLASS_GSP_LEVEL_TMP)
        knd_log(".. reading class GSP: \"%.*s\"..", 128, rec);

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
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "anc",
          .name_size = strlen("anc"),
          .parse = parse_ancestor_array,
          .obj = &ctx
        },
        { .validate = read_attr,
          .obj = &ctx
        },
        { .name = "insts",
          .name_size = strlen("insts"),
          .parse = gsl_parse_size_t,
          .obj = &self->num_snapshot_insts
        },
        /*,
        { .type = GSL_GET_ARRAY_STATE,
          .name = "inst",
          .name_size = strlen("inst"),
          .parse = gsl_parse_array,
          .obj = &inst_commit_spec
        }*/
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err.code;

    // assign glosses
    if (task->ctx->tr) {
        self->tr = task->ctx->tr;
        task->ctx->tr = NULL;
    }


    return knd_OK;
}

int knd_class_marshall(void *elem, size_t *output_size, struct kndTask *task)
{
    struct kndClassEntry *entry = elem;
    struct kndOutput *out = task->out;
    size_t orig_size = out->buf_size;
    int err;
    assert(entry->class != NULL);

    err = knd_class_export_GSP(entry->class, task);
    KND_TASK_ERR("failed to export class GSP");

    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log("== GSP of %.*s (%.*s)  size:%zu", 
                entry->class->name_size,  entry->class->name, entry->id_size, entry->id, out->buf_size - orig_size);

    *output_size = out->buf_size - orig_size;
    return knd_OK;
}

int knd_class_entry_unmarshall(const char *elem_id, size_t elem_id_size, const char *rec, size_t rec_size,
                               void **result, struct kndTask *task)
{
    struct kndMemPool *mempool = task->user_ctx ? task->user_ctx->mempool : task->mempool;
    struct kndClassEntry *entry = NULL;
    struct kndRepo *repo = task->repo;
    struct kndCharSeq *seq;
    struct kndSharedDictItem *item;
    const char *c, *name = rec;
    size_t name_size;
    int err;

    if (DEBUG_CLASS_GSP_LEVEL_2)
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

    err = knd_shared_set_add(repo->class_idx, entry->id, entry->id_size, (void*)entry);
    KND_TASK_ERR("failed to register class entry \"%.*s\"", entry->id_size, entry->id);

    if (DEBUG_CLASS_GSP_LEVEL_3)
        knd_log("== class name decoded \"%.*s\" => \"%.*s\" (repo:%.*s)",
                entry->id_size, entry->id, entry->name_size, entry->name, repo->name_size, repo->name);

    *result = entry;
    return knd_OK;
}

int knd_class_unmarshall(const char *elem_id, size_t elem_id_size, const char *rec, size_t rec_size,
                         void **result, struct kndTask *task)
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

static int inherit_attr(void *obj, const char *unused_var(elem_id), size_t unused_var(elem_id_size),
                        size_t unused_var(count), void *elem)
{
    struct LocalContext *ctx = obj;
    struct kndTask    *task = ctx->task;
    struct kndMemPool *mempool = task->mempool;
    struct kndClass   *self = ctx->class;
    struct kndSet     *attr_idx = self->attr_idx;
    struct kndAttrRef *src_ref = elem;
    struct kndAttr    *attr    = src_ref->attr;
    struct kndAttrRef *ref = NULL;
    int err;

    assert(src_ref->attr != NULL);

    if (DEBUG_CLASS_GSP_LEVEL_TMP) 
        knd_log("..  \"%.*s\" (id:%.*s) attr to be inherited by %.*s..",
                attr->name_size, attr->name, attr->id_size, attr->id,
                self->name_size, self->name);

    /* new attr entry */
    err = knd_attr_ref_new(mempool, &ref);
    KND_TASK_ERR("failed to alloc an attr ref");
    ref->attr = attr;
    ref->attr_var = src_ref->attr_var;
    ref->class_entry = src_ref->class_entry;

    err = attr_idx->add(attr_idx, attr->id, attr->id_size, (void*)ref);
    KND_TASK_ERR("failed to update attr idx of %.*s", self->name_size, self->name);
    return knd_OK;
}

static int resolve_class(struct kndClass *self, struct kndTask *task)
{
    struct kndClassRef *ref;
    struct kndClassEntry *entry;
    struct kndClass *c;
    int err;

    struct LocalContext ctx = {
        .task = task,
        .class = self,
        .baseclass = NULL
    };
    
    FOREACH (ref, self->ancestors) {
        entry = ref->entry;
        err = knd_get_class_by_id(task->repo, entry->id, entry->id_size, &c, task);
        KND_TASK_ERR("failed to get class %.*s", entry->name_size, entry->name);

        ctx.baseclass = c;
        err = c->attr_idx->map(c->attr_idx, inherit_attr, (void*)&ctx);
        KND_TASK_ERR("class \"%.*s\" failed to inherit attrs from \"%.*s\"",
                     self->name_size, self->name, c->name_size, c->name);

        ref->class = c;
    }
    return knd_OK;
}

int knd_class_acquire(struct kndClassEntry *entry, struct kndClass **result, struct kndTask *task)
{
    struct kndRepo *repo = entry->repo;
    struct kndClass *c = NULL, *prev_c;
    // int num_readers;
    int err;

    // TODO read/write conflicts
    atomic_fetch_add_explicit(&entry->num_readers, 1, memory_order_relaxed);
 
    do {
        prev_c = atomic_load_explicit(&entry->class, memory_order_relaxed);
        if (prev_c) {
            // TODO if (c != NULL)  - free 
            *result = prev_c;
            return knd_OK;
        }
        if (!c) {
            err = knd_shared_set_unmarshall_elem(repo->class_idx, entry->id, entry->id_size,
                                                 knd_class_unmarshall, (void**)&c, task);
            if (err) return err;
            c->entry = entry;
            entry->class = c;
            c->name = entry->name;
            c->name_size = entry->name_size;

            err = resolve_class(c, task);
            KND_TASK_ERR("failed to resolve class %.*s", c->name_size, c->name);
        }
    } while (!atomic_compare_exchange_weak(&entry->class, &prev_c, c));

    *result = c;
    return knd_OK;
}
