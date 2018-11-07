#include "knd_attr.h"

#include "knd_mempool.h"
#include "knd_text.h"
#include "knd_utils.h"

#include <gsl-parser.h>

#include <assert.h>
#include <string.h>

#include "knd_task.h"
#include "knd_proc.h"

#define DEBUG_ATTR_LEVEL_1 0
#define DEBUG_ATTR_LEVEL_2 0
#define DEBUG_ATTR_LEVEL_3 0
#define DEBUG_ATTR_LEVEL_4 0
#define DEBUG_ATTR_LEVEL_5 0
#define DEBUG_ATTR_LEVEL_TMP 1

static gsl_err_t run_set_name(void *obj, const char *name, size_t name_size)
{
    struct kndAttr *self = obj;
    self->name = name;
    self->name_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_gloss_locale(void *obj, const char *name, size_t name_size)
{
    struct kndTranslation *self = obj;
    if (name_size >= KND_SHORT_NAME_SIZE) return make_gsl_err(gsl_LIMIT);
    self->curr_locale = name;
    self->curr_locale_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_gloss_value(void *obj, const char *name, size_t name_size)
{
    struct kndTranslation *self = obj;
    if (!name_size) return make_gsl_err(gsl_FORMAT);
    self->val = name;
    self->val_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_gloss_item(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndAttr *self = task->attr;
    struct kndTranslation *tr;
    struct kndMemPool *mempool = task->mempool;
    int err;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. %.*s: allocate gloss translation",
                self->name_size, self->name);

    err = knd_text_translation_new(mempool, &tr);
    if (err) return *total_size = 0, make_gsl_err_external(knd_NOMEM);
    memset(tr, 0, sizeof(struct kndTranslation));

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_gloss_locale,
          .obj = tr
        },
        { .name = "t",
          .name_size = strlen("t"),
          .run = set_gloss_value,
          .obj = tr
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (tr->curr_locale_size == 0 || tr->val_size == 0)
        return make_gsl_err(gsl_FORMAT);  // error: both of them are required

    tr->locale = tr->curr_locale;
    tr->locale_size = tr->curr_locale_size;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. read gloss translation: \"%.*s\",  text: \"%.*s\"",
                tr->locale_size, tr->locale, tr->val_size, tr->val);

    // append
    tr->next = self->tr;
    self->tr = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_gloss(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;

    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .parse = parse_gloss_item,
        .obj = task
    };

    return gsl_parse_array(&item_spec, rec, total_size);
}

static gsl_err_t set_ref_class(void *obj, const char *name, size_t name_size)
{
    struct kndAttr *self = obj;
    if (!name_size) return make_gsl_err(gsl_FAIL);
    self->ref_classname = name;
    self->ref_classname_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_proc(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndAttr *self = task->attr;
    struct kndProc *proc;
    struct kndProcEntry *entry;
    struct kndMemPool *mempool = task->mempool;
    int err;
    gsl_err_t parser_err;

    err = knd_proc_new(mempool, &proc);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    err = knd_proc_entry_new(mempool, &entry);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    entry->repo = task->repo;
    entry->proc = proc;
    proc->entry = entry;

    parser_err = knd_proc_read(proc, rec, total_size);
    if (parser_err.code) return parser_err;

    self->proc = proc;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_set_quant(void *obj, const char *name, size_t name_size)
{
    struct kndAttr *self = (struct kndAttr*)obj;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. run set quant!\n");

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_SHORT_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    if (!memcmp("set", name, name_size)) {
        self->quant_type = KND_ATTR_SET;
        self->is_a_set = true;
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_quant_type(void *obj, const char *rec, size_t *total_size)
{
    struct kndAttr *self = obj;
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_quant,
          .obj = self
        }, // TODO
        { .type = GSL_SET_STATE,
          .name = "uniq",
          .name_size = strlen("uniq"),
          .run = run_set_quant,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t run_set_access_control(void *obj, const char *name, size_t name_size)
{
    struct kndAttr *self = (struct kndAttr*)obj;

    if (name_size >= KND_SHORT_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. run set ACL..");

    if (!strncmp(name, "restrict", strlen("restrict"))) {
        self->access_type = KND_ATTR_ACCESS_RESTRICTED;
        if (DEBUG_ATTR_LEVEL_2)
            knd_log("** NB: restricted attr: %.*s!",
                    self->name_size, self->name);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_access_control(void *obj, const char *rec, size_t *total_size)
{
    struct kndAttr *self = (struct kndAttr*)obj;
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_access_control,
          .obj = self
        }
    };

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. parsing ACL: \"%s\"", rec);

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t confirm_idx(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct kndAttr *self = obj;

    if (DEBUG_ATTR_LEVEL_1)
        knd_log(".. confirm IDX");

    self->is_indexed = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_implied(void *obj,
                                 const char *unused_var(name),
                                 size_t unused_var(name_size))
{
    struct kndAttr *self = obj;
    self->is_implied = true;
    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_import_attr(struct kndTask *task, const char *rec, size_t *total_size)
{
    struct kndAttr *self = task->attr;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .parse = parse_gloss,
          .obj = task
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_g",
          .name_size = strlen("_g"),
          .parse = parse_gloss,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "c",
          .name_size = strlen("c"),
          .run = set_ref_class,
          .obj = self
        },
        { .name = "c",
          .name_size = strlen("c"),
          .run = set_ref_class,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc,
          .obj = self
        },
        { .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "t",
          .name_size = strlen("t"),
          .parse = parse_quant_type,
          .obj = self
        },
        { .name = "t",
          .name_size = strlen("t"),
          .parse = parse_quant_type,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "acl",
          .name_size = strlen("acl"),
          .parse = parse_access_control,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "idx",
          .name_size = strlen("idx"),
          .run = confirm_idx,
          .obj = self
        },
        { .name = "idx",
          .name_size = strlen("idx"),
          .run = confirm_idx,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "impl",
          .name_size = strlen("impl"),
          .run = confirm_implied,
          .obj = self
        },
        { .name = "impl",
          .name_size = strlen("impl"),
          .run = confirm_implied,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "concise",
          .name_size = strlen("concise"),
          .parse = gsl_parse_size_t,
          .obj = &self->concise_level
        },
        { .name = "concise",
          .name_size = strlen("concise"),
          .parse = gsl_parse_size_t,
          .obj = &self->concise_level
        }
    };
    gsl_err_t err;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. attr parsing: \"%.*s\"..", 32, rec);

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    if (self->type == KND_ATTR_INNER) {
        if (!self->ref_classname_size) {
            knd_log("-- ref class not specified in %.*s",
                    self->name_size, self->name);
            return make_gsl_err_external(knd_FAIL);
        }
    }

    // TODO: reject attr names starting with an underscore _

    return make_gsl_err(gsl_OK);
}
