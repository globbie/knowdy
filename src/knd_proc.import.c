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
                                        const char *unused_var(name),
                                        size_t unused_var(name_size),
                                        size_t unused_var(count),
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

static gsl_err_t kndProc_alloc_translation(void *unused_var(obj),
                                           const char *unused_var(name),
                                           size_t unused_var(name_size),
                                           size_t unused_var(count),
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

static gsl_err_t set_gloss_locale(void *obj, const char *name, size_t name_size)
{
    struct kndTranslation *self = obj;
    if (!name_size) return make_gsl_err(gsl_FAIL);
    if (name_size >= KND_SHORT_NAME_SIZE) return make_gsl_err(gsl_LIMIT);
    self->curr_locale = name;
    self->curr_locale_size = name_size;
    return make_gsl_err(gsl_OK);
}
static gsl_err_t set_gloss_value(void *obj, const char *name, size_t name_size)
{
    struct kndTranslation *self = obj;
    if (!name_size) return make_gsl_err(gsl_FAIL);
    self->val = name;
    self->val_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t kndProc_parse_gloss(void *obj,
                                     const char *rec,
                                     size_t *total_size)
{
    struct kndTranslation *tr = obj;
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_gloss_locale,
          .obj = tr
        },
        { .name = "t",
          .name_size = strlen ("t"),
          .run = set_gloss_value,
          .obj = tr
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

static gsl_err_t set_base_arg_classname(void *obj, const char *name, size_t name_size)
{
    struct kndProcArgVar *self = obj;
    if (!name_size) return make_gsl_err(gsl_FORMAT);
    self->name = name;
    self->name_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t kndProc_validate_base_arg(void *obj,
                                           const char *name,
                                           size_t name_size,
                                           const char *rec,
                                           size_t *total_size)
{
    struct kndProcVar *base = obj;

    if (name_size > sizeof ((struct kndProcArgVar *)NULL)->name)
        return *total_size = 0, make_gsl_err(gsl_LIMIT);

    struct kndProcArgVar *base_arg = malloc(sizeof *base_arg);
    if (!base_arg) return make_gsl_err_external(knd_NOMEM);
    memset(base_arg, 0, sizeof *base_arg);

    base_arg->name = name;
    base_arg->name_size = name_size;

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
            .run = set_base_arg_classname,
            .obj = base_arg
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) { free(base_arg); return err; }

    kndProcVar_declare_arg(base, base_arg);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_proc_var_name(void *obj, const char *name, size_t name_size)
{
    struct kndProcVar *self = obj;
    if (!name_size) return make_gsl_err(gsl_FORMAT);
    self->name = name;
    self->name_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t kndProc_parse_base(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndProc *self = obj;

    struct kndProcVar *base = malloc(sizeof *base);
    if (!base) return *total_size = 0, make_gsl_err_external(knd_NOMEM);
    memset(base, 0, sizeof *base);

    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .run = set_proc_var_name,
            .obj = base
        },
        {   .type = GSL_SET_STATE,
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

static gsl_err_t kndProc_validate_do_arg(void *obj,
                                         const char *name,
                                         size_t name_size,
                                         const char *rec,
                                         size_t *total_size)
{
    struct kndProc *proc = obj;
    gsl_err_t err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. Proc Call Arg \"%.*s\" to validate: \"%.*s\"..",
                name_size, name, 32, rec);

    if (name_size > sizeof ((struct kndProcCallArg *)NULL)->name)
        return *total_size = 0, make_gsl_err(gsl_LIMIT);

    struct kndClassVar *class_var;
    err.code = knd_class_var_new(proc->entry->repo->mempool, &class_var);
    if (err.code) return *total_size = 0, make_gsl_err_external(err.code);
    class_var->root_class = proc->entry->repo->root_class;

    err = knd_import_class_var(class_var, rec, total_size);
    if (err.code) return err;

    // TODO: use mempool
    struct kndProcCallArg *call_arg = malloc(sizeof *call_arg);
    if (!call_arg) return make_gsl_err_external(knd_NOMEM);

    kndProcCallArg_init(call_arg, name, name_size, class_var);
    kndProcCall_declare_arg(proc->proc_call, call_arg);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_proc_call_name(void *obj, const char *name, size_t name_size)
{
    struct kndProcCall *self = obj;
    if (!name_size) return make_gsl_err(gsl_FORMAT);
    self->name = name;
    self->name_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t knd_proc_parse_do(void *obj,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndProc *self = obj;

    if (DEBUG_PROC_LEVEL_TMP)
        knd_log(".. Proc Call parsing: \"%.*s\"..",
                32, rec);

    if (!self->proc_call) {
           return *total_size = 0, make_gsl_err_external(knd_FAIL);
    }

    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .run = set_proc_call_name,
            .obj = self->proc_call
        },
        {
            .is_validator = true,
            .validate = kndProc_validate_do_arg,
            .obj = self
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    // TODO: lookup table
    if (self->proc_call->name_size == strlen("_mult") &&
        !strncmp("_mult", self->proc_call->name, self->proc_call->name_size))
        self->proc_call->type = KND_PROC_MULT;
    else if (!strncmp("_sum", self->proc_call->name, self->proc_call->name_size))
        self->proc_call->type = KND_PROC_SUM;
    else if (!strncmp("_mult_percent", self->proc_call->name, self->proc_call->name_size))
        self->proc_call->type = KND_PROC_MULT_PERCENT;
    else if (!strncmp("_div_percent", self->proc_call->name, self->proc_call->name_size))
        self->proc_call->type = KND_PROC_DIV_PERCENT;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_proc_name(void *obj, const char *name, size_t name_size)
{
    struct kndProc *self = obj;
    if (!name_size) return make_gsl_err(gsl_FORMAT);
    self->entry->name = name;
    self->entry->name_size = name_size;
    self->name = name;
    self->name_size = name_size;

    return make_gsl_err(gsl_OK);
}

extern gsl_err_t knd_proc_import(struct kndProc *root_proc,
                                 const char *rec,
                                 size_t *total_size)
{
    struct kndProc *proc;
    struct kndProcEntry *entry;
    struct kndRepo *repo = root_proc->entry->repo;
    struct kndMemPool *mempool = repo->mempool;
    int err;

    if (DEBUG_PROC_LEVEL_TMP)
        knd_log(".. import proc: \"%.*s\"..", 32, rec);

    err = knd_proc_entry_new(mempool, &entry);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    entry->name = "/";
    entry->name_size = 1;
    entry->repo = repo;

    err = knd_proc_new(mempool, &proc);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    proc->name = entry->name;
    proc->name_size = 1;
    entry->proc = proc;
    proc->entry = entry;

    //kndProc_inherit_idx(proc, root_proc);

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

    /*struct gslTaskSpec summary_spec = {
        .is_list_item = true,
        .alloc = kndProc_alloc_translation,
        .append = kndProc_append_summary,
        .parse = kndProc_parse_summary,
        .accu = proc
        }; */

    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .run = set_proc_name,
            .obj = proc
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
        }/*,
        {
            .type = GSL_SET_ARRAY_STATE,
            .name = "_summary",
            .name_size = strlen("_summary"),
            .parse = gsl_parse_array,
            .obj = &summary_spec
            }*/,
        {
            .name = "is",
            .name_size = strlen("is"),
            .parse = kndProc_parse_base,
            .obj = proc
        }
        /*{
            .name = "result",
            .name_size = strlen("result"),
            .buf = proc->result_classname,
            .buf_size = &proc->result_classname_size,
            .max_buf_size = sizeof proc->result_classname
            }*/,
        {   .name = "do",
            .name_size = strlen("do"),
            .parse = knd_proc_parse_do,
            .obj = proc
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (!proc->entry->name_size)
        return make_gsl_err(gsl_FORMAT);

    entry = repo->proc_name_idx->get(repo->proc_name_idx,
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
            err = root_proc->entry->repo->log->writef(root_proc->entry->repo->log,
                                                      "%*s proc name already exists",
                                                      proc->name_size, proc->name);
            if (err) return make_gsl_err_external(err);
            return make_gsl_err_external(knd_EXISTS);
        }
    }

    /*    if (!root_proc->batch_mode) {
        proc->next = root_proc->inbox;
        root_proc->inbox = proc;
        root_proc->inbox_size++;
        }*/

    /* generate ID and add to proc index */
    repo->num_procs++;
    proc->entry->numid = repo->num_procs;
    knd_uid_create(proc->entry->numid, proc->entry->id, &proc->entry->id_size);

    err = repo->proc_name_idx->set(repo->proc_name_idx,
                                   proc->entry->name, proc->entry->name_size,
                                   (void*)proc->entry);
    if (err) return make_gsl_err_external(err);

    if (DEBUG_PROC_LEVEL_TMP)
        proc->str(proc);

    return make_gsl_err(gsl_OK);
}
