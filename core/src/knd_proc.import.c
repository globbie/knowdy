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
    struct kndProc *self = obj;
    int err;

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

static gsl_err_t kndProc_alloc_translation(void *obj __attribute__((unused)),
                                           const char *name __attribute__((unused)),
                                           size_t name_size __attribute__((unused)),
                                           size_t count __attribute__((unused)),
                                           void **item)
{
    /* TODO: mempool alloc */
    //self->entry->repo->mempool->new_text_seq();
    struct kndTranslation *tr = malloc(sizeof *tr);
    if (!tr) return make_gsl_err_external(knd_NOMEM);

    memset(tr, 0, sizeof *tr);
    *item = tr;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t kndProc_append_gloss(void *accu,
                                      void *item)
{
    struct kndProc *self = accu;
    kndProc_declare_tr(self, (struct kndTranslation *)item);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t kndProc_parse_gloss(void *obj,
                                     const char *rec,
                                     size_t *total_size)
{
    struct kndTranslation *tr = obj;
    struct gslTaskSpec specs[] = {
        {
            .is_implied = true,
            .buf = tr->curr_locale,
            .buf_size = &tr->curr_locale_size,
            .max_buf_size = sizeof tr->curr_locale
        },
        {
            .name = "t",
            .name_size = strlen ("t"),
            .buf = tr->val,
            .buf_size = &tr->val_size,
            .max_buf_size = sizeof tr->val
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    if (tr->curr_locale_size == 0 || tr->val_size == 0)
        return make_gsl_err(gsl_FORMAT);  // error: both of them are required ;

    // TODO(k15tfu): remove this
    tr->locale = tr->curr_locale;
    tr->locale_size = tr->curr_locale_size;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. read gloss translation: \"%.*s\",  text: \"%.*s\"",
            tr->locale_size, tr->locale, tr->val_size, tr->val);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t kndProc_append_summary(void *accu,
                                        void *item)
{
    struct kndProc *self = accu;
    kndProc_declare_summary(self, (struct kndTranslation *)item);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t kndProc_parse_summary(void *obj,
                                       const char *rec,
                                       size_t *total_size)
{
    struct kndTranslation *tr = obj;
    struct gslTaskSpec specs[] = {
        {
            .is_implied = true,
            .buf = tr->curr_locale,
            .buf_size = &tr->curr_locale_size,
            .max_buf_size = sizeof tr->curr_locale
        },
        {
            .name = "t",
            .name_size = strlen ("t"),
            .buf = tr->val,
            .buf_size = &tr->val_size,
            .max_buf_size = sizeof tr->val
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    if (tr->curr_locale_size == 0 || tr->val_size == 0)
        return make_gsl_err(gsl_FORMAT);  // error: both of them are required ;

    // TODO(k15tfu): remove this
    tr->locale = tr->curr_locale;
    tr->locale_size = tr->curr_locale_size;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. read summary translation: \"%.*s\",  text: \"%.*s\"",
                tr->locale_size, tr->locale, tr->val_size, tr->val);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t kndProc_validate_base_arg(void *obj,
                                           const char *name,
                                           size_t name_size,
                                           const char *rec,
                                           size_t *total_size)
{
    struct kndProcVar *base = obj;

    assert(name != NULL && name_size != 0);

    struct kndProcArgVar *base_arg = malloc(sizeof *base_arg);
    if (!base_arg) return make_gsl_err_external(knd_NOMEM);
    memset(base_arg, 0, sizeof *base_arg);

    if (name_size > sizeof base_arg->name)
        return free(base_arg), make_gsl_err(gsl_LIMIT);
    memcpy(base_arg->name, name, name_size);
    base_arg->name_size = name_size;
    base_arg->name[name_size] = '\0';

    struct gslTaskSpec specs[] = {
// FIXME(k15tfu): ?? switch to something like this
//        {
//            .is_implied = true,
//            .buf = base_arg->name,
//            .buf_size = &base_arg->name_size,
//            .max_buf_size = sizeof base_arg->name
//        },
        {
            .name = "c",
            .name_size = strlen("c"),
            .buf = base_arg->classname,
            .buf_size = &base_arg->classname_size,
            .max_buf_size = sizeof base_arg->classname
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) { free(base_arg); return err; }

    kndProcVar_declare_arg(base, base_arg);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t kndProc_parse_base(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndProc *self = obj;

    struct kndProcVar *base = malloc(sizeof *base);
    if (!base) return make_gsl_err_external(knd_NOMEM);
    memset(base, 0, sizeof *base);

    struct gslTaskSpec specs[] = {
        {
            .is_implied = true,
            .buf = base->name,
            .buf_size = &base->name_size,
            .max_buf_size = sizeof base->name
        },
        {
            .type = GSL_SET_STATE,
            .is_validator = true,
            .validate = kndProc_validate_base_arg,
            .obj = base
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(specs[0]));
    if (err.code) { free(base); return err; }

    kndProc_declare_base(self, base);

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

gsl_err_t kndProc_import(struct kndProc *root_proc,
                         const char *rec,
                         size_t *total_size)
{
    struct kndProc *proc;
    struct kndProcEntry *entry;
    struct kndMemPool *mempool = root_proc->entry->repo->mempool;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. import Proc: \"%.*s\"..", 32, rec);

    err = mempool->new_proc(mempool, &proc);
    if (err) { *total_size = 0; return make_gsl_err_external(err); }

    proc->proc_name_idx = root_proc->proc_name_idx;
    proc->proc_idx = root_proc->proc_idx;
    proc->class_name_idx = root_proc->class_name_idx;
    proc->class_idx = root_proc->class_idx;

    err = mempool->new_proc_entry(mempool, &entry);
    if (err) return make_gsl_err_external(err);
    entry->repo = root_proc->entry->repo;
    entry->proc = proc;
    proc->entry = entry;

    struct gslTaskSpec proc_arg_spec = {
        .is_list_item = true,
        .alloc = kndProc_alloc_proc_arg,
        .append = kndProc_append_proc_arg,
        .parse = kndProc_parse_proc_arg,
        .accu = proc
    };

    struct gslTaskSpec gloss_spec = {
        .is_list_item = true,
        .alloc = kndProc_alloc_translation,
        .append = kndProc_append_gloss,
        .parse = kndProc_parse_gloss,
        .accu = proc
    };

    struct gslTaskSpec summary_spec = {
        .is_list_item = true,
        .alloc = kndProc_alloc_translation,
        .append = kndProc_append_summary,
        .parse = kndProc_parse_summary,
        .accu = proc
    };

    struct gslTaskSpec specs[] = {
        {
            .is_implied = true,
            .buf = proc->entry->name,
            .buf_size = &proc->entry->name_size,
            .max_buf_size = sizeof proc->entry->name
        },
        {
            .type = GSL_SET_ARRAY_STATE,
            .name = "arg",
            .name_size = strlen("arg"),
            .parse = gsl_parse_array,
            .obj = &proc_arg_spec
        },
        {
            .type = GSL_SET_ARRAY_STATE,
            .name = "_g",
            .name_size = strlen("_g"),
            .parse = gsl_parse_array,
            .obj = &gloss_spec
        },
        {
            .type = GSL_SET_ARRAY_STATE,
            .name = "_gloss",
            .name_size = strlen("_gloss"),
            .parse = gsl_parse_array,
            .obj = &gloss_spec
        },
        {
            .type = GSL_SET_ARRAY_STATE,
            .name = "_summary",
            .name_size = strlen("_summary"),
            .parse = gsl_parse_array,
            .obj = &summary_spec
        },
        {
            .name = "is",
            .name_size = strlen("is"),
            .parse = kndProc_parse_base,
            .obj = proc
        },
        {
            .name = "result",
            .name_size = strlen("result"),
            .buf = proc->result_classname,
            .buf_size = &proc->result_classname_size,
            .max_buf_size = sizeof proc->result_classname
        },
        {
            .name = "do",
            .name_size = strlen("do"),
            .parse = parse_proc_call,
            .obj = proc //&proc->proc_call
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (!proc->entry->name_size)
        return make_gsl_err_external(knd_FAIL);

    entry = root_proc->proc_name_idx->get(root_proc->proc_name_idx,
                                     proc->entry->name, proc->entry->name_size);
    if (entry) {
        if (entry->phase == KND_REMOVED) {
            knd_log("== proc was removed recently");
        } else {
            knd_log(".. doublet?");
            knd_log("-- %.*s proc name doublet found :( log:%p",
                    proc->entry->name_size, proc->entry->name,
                    root_proc->entry->repo->log);
            root_proc->entry->repo->log->reset(root_proc->entry->repo->log);
            err = root_proc->entry->repo->log->write(root_proc->entry->repo->log,
                                                proc->name,
                                                proc->name_size);
            if (err) return make_gsl_err_external(err);
            err = root_proc->entry->repo->log->write(root_proc->entry->repo->log,
                                                " proc name already exists",
                                                strlen(" proc name already exists"));
            if (err) return make_gsl_err_external(err);
            return make_gsl_err_external(knd_FAIL);
        }
    }

    if (!root_proc->batch_mode) {
        proc->next = root_proc->inbox;
        root_proc->inbox = proc;
        root_proc->inbox_size++;
    }

    root_proc->num_procs++;
    proc->entry->numid = root_proc->num_procs;
    proc->entry->id_size = KND_ID_SIZE;

    knd_num_to_str(proc->entry->numid,
                   proc->entry->id, &proc->entry->id_size, KND_RADIX_BASE);

    err = root_proc->proc_name_idx->set(root_proc->proc_name_idx,
                                   proc->entry->name, proc->entry->name_size,
                                   (void*)proc->entry);
    if (err) return make_gsl_err_external(err);

    if (DEBUG_PROC_LEVEL_2)
        proc->str(proc);

    return make_gsl_err(gsl_OK);
}
