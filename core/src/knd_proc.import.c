#include <knd_proc.h>

#include <knd_utils.h>

#include <gsl-parser.h>

#include <string.h>

// TODO remove this
#include <knd_mempool.h>
#include <knd_proc_arg.h>
#include <knd_text.h>
#include <knd_class.h>
#include <knd_repo.h>

#define DEBUG_PROC_LEVEL_0 0
#define DEBUG_PROC_LEVEL_1 0
#define DEBUG_PROC_LEVEL_2 0
#define DEBUG_PROC_LEVEL_3 0
#define DEBUG_PROC_LEVEL_TMP 1

static gsl_err_t kndProc_alloc_proc_arg(void *obj,
                                        const char *name __attribute__((unused)),
                                        size_t name_size __attribute__((unused)),
                                        size_t count __attribute__((unused)),
                                        void **item)
{
    int err;
    struct kndProc *self = obj;

    err = kndProcArg_new((struct kndProcArg **)item, self, self->entry->repo->mempool);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t kndProc_append_proc_arg(void *accu,
                                         void *item)
{
    struct kndProc *self = accu;
    kndProc_declare_arg(self, (struct kndProcArg *)item);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t kndProc_parse_proc_arg(void *obj,
                                        const char *rec,
                                        size_t *total_size)
{
    struct kndProcArg *arg = obj;
    return arg->parse(arg, rec, total_size);
}

static gsl_err_t alloc_gloss_item(void *obj __attribute__((unused)),
                                  const char *name,
                                  size_t name_size,
                                  size_t count __attribute__((unused)),
                                  void **item)
{
    struct kndTranslation *tr;

    assert(name == NULL && name_size == 0);

    /* TODO: mempool alloc */
    //self->entry->repo->mempool->new_text_seq();
    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return make_gsl_err_external(knd_NOMEM);

    memset(tr, 0, sizeof(struct kndTranslation));

    *item = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t append_gloss_item(void *accu,
                                   void *item)
{
    struct kndProc *self = accu;
    struct kndTranslation *tr = item;

    tr->next = self->tr;
    self->tr = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_gloss_item(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndTranslation *tr = obj;
    struct gslTaskSpec specs[] = {
            { .is_implied = true,
                    .buf = tr->curr_locale,
                    .buf_size = &tr->curr_locale_size,
                    .max_buf_size = sizeof tr->curr_locale
            },
            { .name = "t",
                    .name_size = strlen("t"),
                    .buf = tr->val,
                    .buf_size = &tr->val_size,
                    .max_buf_size = sizeof tr->val
            }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    if (tr->curr_locale_size == 0 || tr->val_size == 0)
        return make_gsl_err(gsl_FORMAT);  // error: both of them are required

    tr->locale = tr->curr_locale;
    tr->locale_size = tr->curr_locale_size;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. read gloss translation: \"%.*s\",  text: \"%.*s\"",
                tr->locale_size, tr->locale, tr->val_size, tr->val);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_gloss(void *obj,
                             const char *rec,
                             size_t *total_size)
{
    struct kndProc *self = obj;
    struct gslTaskSpec item_spec = {
            .is_list_item = true,
            .alloc = alloc_gloss_item,
            .append = append_gloss_item,
            .accu = self,
            .parse = parse_gloss_item
    };

    return gsl_parse_array(&item_spec, rec, total_size);
}

static gsl_err_t alloc_summary_item(void *obj __attribute__((unused)),
                                    const char *name,
                                    size_t name_size,
                                    size_t count __attribute__((unused)),
                                    void **item)
{
    struct kndTranslation *tr;

    assert(name == NULL && name_size == 0);

    /* TODO: mempool alloc */
    //self->entry->repo->mempool->new_text_seq();
    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return make_gsl_err_external(knd_NOMEM);

    memset(tr, 0, sizeof(struct kndTranslation));

    *item = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t append_summary_item(void *accu,
                                     void *item)
{
    struct kndProc *self = accu;
    struct kndTranslation *tr = item;

    tr->next = self->summary;
    self->summary = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_summary_item(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndTranslation *tr = obj;
    struct gslTaskSpec specs[] = {
            { .is_implied = true,
                    .buf = tr->curr_locale,
                    .buf_size = &tr->curr_locale_size,
                    .max_buf_size = sizeof tr->curr_locale
            },
            { .name = "t",
                    .name_size = strlen("t"),
                    .buf = tr->val,
                    .buf_size = &tr->val_size,
                    .max_buf_size = sizeof tr->val
            }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    if (tr->curr_locale_size == 0 || tr->val_size == 0)
        return make_gsl_err(gsl_FORMAT);  // error: both of them are required

    tr->locale = tr->curr_locale;
    tr->locale_size = tr->curr_locale_size;

    if (DEBUG_PROC_LEVEL_1)
        knd_log(".. read summary translation: \"%.*s\",  text: \"%.*s\"",
                tr->locale_size, tr->locale, tr->val_size, tr->val);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_summary(void *obj,
                               const char *rec,
                               size_t *total_size)
{
    struct kndProc *self = obj;
    struct gslTaskSpec item_spec = {
            .is_list_item = true,
            .alloc = alloc_summary_item,
            .append = append_summary_item,
            .accu = self,
            .parse = parse_summary_item
    };

    return gsl_parse_array(&item_spec, rec, total_size);
}

static gsl_err_t arg_item_read(void *obj,
                               const char *name, size_t name_size,
                               const char *rec, size_t *total_size)
{
    struct kndProcVar *base = obj;
    struct kndProcArgVar *item;
    gsl_err_t parser_err;

    item = malloc(sizeof(struct kndProcArgVar));
    assert(item);
    memset(item, 0, sizeof(struct kndProcArgVar));
    memcpy(item->name, name, name_size);
    item->name_size = name_size;
    item->name[name_size] = '\0';

    struct gslTaskSpec specs[] = {
            { .name = "c",
                    .name_size = strlen("c"),
                    .buf_size = &item->classname_size,
                    .max_buf_size = KND_NAME_SIZE,
                    .buf = item->classname
            }
    };

    parser_err = gsl_parse_task(rec, total_size, specs,
                                sizeof(specs) / sizeof(specs[0]));
    if (parser_err.code) return parser_err;

    if (!base->tail) {
        base->tail = item;
        base->args = item;
    }
    else {
        base->tail->next = item;
        base->tail = item;
    }

    base->num_args++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_base(void *data,
                            const char *rec,
                            size_t *total_size)
{
    struct kndProc *self = data;
    struct kndProcVar *base;
    gsl_err_t parser_err;

    /*err = self->entry->repo->mempool->new_proc_base(self->entry->repo->mempool, &base);                       RET_ERR();
    base->task = self->task;
    err = base->parse(base, rec, total_size);                                       PARSE_ERR();
    */

    base = malloc(sizeof(struct kndProcVar));
    assert(base);
    memset(base, 0, sizeof(struct kndProcVar));

    struct gslTaskSpec specs[] = {
            { .is_implied = true,
                    .buf_size = &base->name_size,
                    .max_buf_size = KND_NAME_SIZE,
                    .buf = base->name
            },
            { .type = GSL_SET_STATE,
                    .is_validator = true,
                    .validate = arg_item_read,
                    .obj = base
            }
    };

    parser_err = gsl_parse_task(rec, total_size, specs,
                                sizeof(specs) / sizeof(specs[0]));
    if (parser_err.code) return parser_err;

    base->proc = self;
    base->next = self->bases;
    self->bases = base;
    self->num_bases++;

    return make_gsl_err(gsl_OK);
}


static gsl_err_t parse_proc_call_arg(void *obj,
                                     const char *name, size_t name_size,
                                     const char *rec, size_t *total_size)
{
    struct kndProc *proc = obj;
    struct kndProcCall *proc_call = &proc->proc_call;
    struct kndProcCallArg *call_arg;
    struct kndClassVar *class_var;
    struct kndMemPool *mempool = proc->entry->repo->mempool;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. Proc Call Arg \"%.*s\" to validate: \"%.*s\"..",
                name_size, name, 32, rec);

    // TODO: use mempool
    call_arg = malloc(sizeof(struct kndProcCallArg));
    if (!call_arg) return *total_size = 0, make_gsl_err_external(knd_NOMEM);

    memset(call_arg, 0, sizeof(struct kndProcCallArg));
    memcpy(call_arg->name, name, name_size);
    call_arg->name_size = name_size;

    call_arg->next = proc_call->args;
    proc_call->args = call_arg;
    proc_call->num_args++;

    err = knd_class_var_new(mempool, &class_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    class_var->root_class = proc->entry->repo->root_class;

    parser_err = import_class_var(class_var, rec, total_size);
    if (parser_err.code) return parser_err;

    call_arg->class_var = class_var;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_proc_call(void *obj,
                                 const char *rec,
                                 size_t *total_size)
{
    struct kndProc *proc = obj;
    struct kndProcCall *proc_call = &proc->proc_call;
    gsl_err_t parser_err;

    if (DEBUG_PROC_LEVEL_TMP)
        knd_log(".. Proc Call parsing: \"%.*s\".. entry:%p",
                32, rec, proc->entry);

    struct gslTaskSpec specs[] = {
            { .is_implied = true,
                    .buf_size = &proc_call->name_size,
                    .max_buf_size = KND_NAME_SIZE,
                    .buf = proc_call->name
            }/*,
        { .type = GSL_GET_ARRAY_STATE,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .parse = parse_gloss,
          .obj = proc_call
          }*/,
            { .type = GSL_SET_ARRAY_STATE,
                    .name = "_gloss",
                    .name_size = strlen("_gloss"),
                    .parse = parse_gloss,
                    .obj = proc_call
            },
            { .type = GSL_SET_ARRAY_STATE,
                    .name = "_summary",
                    .name_size = strlen("_summary"),
                    .parse = parse_summary,
                    .obj = proc_call
            },
            { .type = GSL_SET_ARRAY_STATE,
                    .name = "_g",
                    .name_size = strlen("_g"),
                    .parse = parse_gloss,
                    .obj = proc_call
            },
            { .is_validator = true,
                    .validate = parse_proc_call_arg,
                    .obj = proc
            }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    // TODO: lookup table
    if (!strncmp("_mult", proc_call->name, proc_call->name_size))
        proc_call->type = KND_PROC_MULT;

    if (!strncmp("_sum", proc_call->name, proc_call->name_size))
        proc_call->type = KND_PROC_SUM;

    if (!strncmp("_mult_percent", proc_call->name, proc_call->name_size))
        proc_call->type = KND_PROC_MULT_PERCENT;

    if (!strncmp("_div_percent", proc_call->name, proc_call->name_size))
        proc_call->type = KND_PROC_DIV_PERCENT;

    return make_gsl_err(gsl_OK);
}

gsl_err_t kndProc_import(struct kndProc *self,
                         const char *rec,
                         size_t *total_size)
{
    struct kndProc *proc;
    struct kndProcEntry *entry;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. import Proc: \"%.*s\"..", 32, rec);

    err = mempool->new_proc(mempool, &proc);
    if (err) { *total_size = 0; return make_gsl_err_external(err); }

    proc->proc_name_idx = self->proc_name_idx;
    proc->proc_idx = self->proc_idx;
    proc->class_name_idx = self->class_name_idx;
    proc->class_idx = self->class_idx;

    err = mempool->new_proc_entry(mempool, &entry);
    if (err) return make_gsl_err_external(err);
    entry->repo = self->entry->repo;
    entry->proc = proc;
    proc->entry = entry;

    struct gslTaskSpec proc_arg_spec = {
            .is_list_item = true,
            .alloc = kndProc_alloc_proc_arg,
            .append = kndProc_append_proc_arg,
            .parse = kndProc_parse_proc_arg,
            .accu = proc
    };

    struct gslTaskSpec specs[] = {
            { .is_implied = true,
                    .buf = proc->entry->name,
                    .buf_size = &proc->entry->name_size,
                    .max_buf_size = KND_NAME_SIZE
            },
            { .type = GSL_SET_ARRAY_STATE,
                    .name = "_gloss",
                    .name_size = strlen("_gloss"),
                    .parse = parse_gloss,
                    .obj = proc
            },
            { .type = GSL_SET_ARRAY_STATE,
                    .name = "_summary",
                    .name_size = strlen("_summary"),
                    .parse = parse_summary,
                    .obj = proc
            },
            { .type = GSL_SET_ARRAY_STATE,
                    .name = "_g",
                    .name_size = strlen("_g"),
                    .parse = parse_gloss,
                    .obj = proc
            },
            { .name = "is",
                    .name_size = strlen("is"),
                    .parse = parse_base,
                    .obj = proc
            },
            { .type = GSL_SET_STATE,
                    .name = "result",
                    .name_size = strlen("result"),
                    .buf = proc->result_classname,
                    .buf_size = &proc->result_classname_size,
                    .max_buf_size = KND_NAME_SIZE
            },
            { .name = "result",
                    .name_size = strlen("result"),
                    .buf = proc->result_classname,
                    .buf_size = &proc->result_classname_size,
                    .max_buf_size = KND_NAME_SIZE
            },
            { .type = GSL_SET_ARRAY_STATE,
                    .name = "arg",
                    .name_size = strlen("arg"),
                    .parse = gsl_parse_array,
                    .obj = &proc_arg_spec
            },
            { .name = "do",
                    .name_size = strlen("do"),
                    .parse = parse_proc_call,
                    .obj = proc //&proc->proc_call
            }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (!proc->entry->name_size)
        return make_gsl_err_external(knd_FAIL);

    entry = self->proc_name_idx->get(self->proc_name_idx,
                                     proc->entry->name, proc->entry->name_size);
    if (entry) {
        if (entry->phase == KND_REMOVED) {
            knd_log("== proc was removed recently");
        } else {
            knd_log(".. doublet?");
            knd_log("-- %.*s proc name doublet found :( log:%p",
                    proc->entry->name_size, proc->entry->name,
                    self->entry->repo->log);
            self->entry->repo->log->reset(self->entry->repo->log);
            err = self->entry->repo->log->write(self->entry->repo->log,
                                                proc->name,
                                                proc->name_size);
            if (err) return make_gsl_err_external(err);
            err = self->entry->repo->log->write(self->entry->repo->log,
                                                " proc name already exists",
                                                strlen(" proc name already exists"));
            if (err) return make_gsl_err_external(err);
            return make_gsl_err_external(knd_FAIL);
        }
    }

    if (!self->batch_mode) {
        proc->next = self->inbox;
        self->inbox = proc;
        self->inbox_size++;
    }

    self->num_procs++;
    proc->entry->numid = self->num_procs;
    proc->entry->id_size = KND_ID_SIZE;

    knd_num_to_str(proc->entry->numid,
                   proc->entry->id, &proc->entry->id_size, KND_RADIX_BASE);

    err = self->proc_name_idx->set(self->proc_name_idx,
                                   proc->entry->name, proc->entry->name_size,
                                   (void*)proc->entry);
    if (err) return make_gsl_err_external(err);

    if (DEBUG_PROC_LEVEL_2)
        proc->str(proc);

    return make_gsl_err(gsl_OK);
}
