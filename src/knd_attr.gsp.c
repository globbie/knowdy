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
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_state.h"
#include "knd_user.h"
#include "knd_repo.h"
#include "knd_mempool.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_shared_set.h"
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#define DEBUG_ATTR_GSP_LEVEL_1 0
#define DEBUG_ATTR_GSP_LEVEL_2 0
#define DEBUG_ATTR_GSP_LEVEL_3 0
#define DEBUG_ATTR_GSP_LEVEL_4 0
#define DEBUG_ATTR_GSP_LEVEL_5 0
#define DEBUG_ATTR_GSP_LEVEL_TMP 1

struct LocalContext {
    struct kndClassVar *class_var;
    struct kndAttr     *attr;
    struct kndRepo     *repo;
    struct kndTask     *task;
};

static gsl_err_t run_set_name(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttr *self = ctx->attr;
    struct kndTask *task = ctx->task;
    struct kndRepo *repo = task->repo;
    struct kndCharSeq *seq;
    int err;

    self->name = name;
    self->name_size = name_size;

    if (name_size <= KND_ID_SIZE) {
        err = knd_shared_set_get(repo->str_idx, name, name_size, (void**)&seq);
        if (err) {
            KND_TASK_LOG("failed to decode attr name code \"%.*s\"", name_size, name);
            return make_gsl_err_external(err);
        }
        self->name = seq->val;
        self->name_size = seq->val_size;
        self->seq = seq;
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_ref_class(void *obj, const char *id, size_t id_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttr *self = ctx->attr;
    struct kndRepo *repo = ctx->repo;
    struct kndClassEntry *entry;
    struct kndTask *task = ctx->task;
    int err;
    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_FORMAT);

    err = knd_shared_set_get(repo->class_idx, id, id_size, (void**)&entry);
    if (err) {
        KND_TASK_LOG("failed to link class entry \"%.*s\"", id_size, id);
        return make_gsl_err_external(err);
    }
    self->ref_classname = entry->name;
    self->ref_classname_size = entry->name_size;
    self->ref_class_entry = entry;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_idx(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct kndAttr *self = obj;
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
static gsl_err_t confirm_required(void *obj,
                                  const char *unused_var(name),
                                  size_t unused_var(name_size))
{
    struct kndAttr *self = obj;
    self->is_required = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_unique(void *obj,
                                const char *unused_var(name),
                                size_t unused_var(name_size))
{
    struct kndAttr *self = obj;
    self->is_unique = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_quant(void *obj, const char *name, size_t name_size)
{
    struct kndAttr *self = (struct kndAttr*)obj;
    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_SHORT_NAME_SIZE) return make_gsl_err(gsl_LIMIT);
    if (!memcmp("set", name, name_size)) {
        self->quant_type = KND_ATTR_SET;
        self->is_a_set = true;
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_quant_uniq(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct kndAttr *self = (struct kndAttr*)obj;
    self->set_is_unique = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_quant_atomic(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct kndAttr *self = obj;
    self->set_is_atomic = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_attr(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct kndAttr *attr = obj;
    if (DEBUG_ATTR_GSP_LEVEL_2)
        knd_log("++ confirm attr: %.*s",
                attr->name_size, attr->name);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_id(void *obj, const char *rec, size_t *total_size)
{
    struct kndAttr *self = obj;

    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .buf = self->id,
            .buf_size = &self->id_size,
            .max_buf_size = KND_ID_SIZE
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_quant_type(void *obj, const char *rec, size_t *total_size)
{
    struct kndAttr *self = obj;
    if (!self->name_size) {
        knd_log("-- attr name not specified");
        return make_gsl_err(gsl_FAIL);
    }

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_quant,
          .obj = self
        },
        { .name = "uniq",
          .name_size = strlen("uniq"),
          .run = set_quant_uniq,
          .obj = self
        },
        { .name = "atom",
          .name_size = strlen("atom"),
          .run = set_quant_atomic,
          .obj = self
        },
        { .is_default = true,
          .run = confirm_attr,
          .obj = self
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

gsl_err_t knd_attr_read(struct kndAttr *self, struct kndTask *task, const char *rec, size_t *total_size)
{
    struct LocalContext ctx = {
        .attr = self,
        .repo = task->repo,
        .task = task
    };
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = &ctx
        },
        { .name = "id",
          .name_size = strlen("id"),
          .parse = parse_id,
          .obj = self
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "_g",
          .name_size = strlen("_g"),
          .parse = knd_parse_gloss_array,
          .obj = task
        },
        { .name = "c",
          .name_size = strlen("c"),
          .run = set_ref_class,
          .obj = &ctx
        }/*,
        { .name = "_proc",
          .name_size = strlen("_proc"),
          .run = set_procref,
          .obj = self
        },
        { .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc,
          .obj = &ctx
          }*/,
        { .name = "t",
          .name_size = strlen("t"),
          .parse = parse_quant_type,
          .obj = self
        },
        { .name = "idx",
          .name_size = strlen("idx"),
          .run = confirm_idx,
          .obj = self
        },
        { .name = "impl",
          .name_size = strlen("impl"),
          .run = confirm_implied,
          .obj = self
        },
        { .name = "req",
          .name_size = strlen("req"),
          .run = confirm_required,
          .obj = self
        },
        { .name = "uniq",
          .name_size = strlen("uniq"),
          .run = confirm_unique,
          .obj = self
        },
        { .name = "concise",
          .name_size = strlen("concise"),
          .parse = gsl_parse_size_t,
          .obj = &self->concise_level
        }
    };
    gsl_err_t err;

    if (DEBUG_ATTR_GSP_LEVEL_2)
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

static int export_glosses(struct kndAttr *self, struct kndOutput *out)
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

int knd_attr_export_GSP(struct kndAttr *self, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    char buf[KND_NAME_SIZE] = {0};
    size_t buf_size = 0;

    const char *type_name = knd_attr_names[self->type];
    size_t type_name_size = strlen(knd_attr_names[self->type]);
    int err;

    OUT("{", 1);
    OUT(type_name, type_name_size);

    OUT(" ", 1);
    knd_uid_create(self->seq->numid, buf, &buf_size);
    OUT(buf, buf_size);

    OUT("{id ", strlen("{id "));
    OUT(self->id, self->id_size);
    OUT("}", 1);

    if (self->is_a_set) {
        OUT("{t set}", strlen("{t set}"));
    }

    if (self->is_implied) {
        OUT("{impl}", strlen("{impl}"));
    }

    if (self->is_required) {
        OUT("{req}", strlen("{req}"));
    }

    if (self->is_unique) {
        OUT("{uniq}", strlen("{uniq}"));
    }

    if (self->is_indexed) {
        OUT("{idx}", strlen("{idx}"));
    }

    if (self->concise_level) {
        buf_size = sprintf(buf, "%zu", self->concise_level);
        OUT("{concise ", strlen("{concise "));
        OUT(buf, buf_size);
        OUT("}", 1);
    }

    if (self->ref_class_entry) {
        OUT("{c ", strlen("{c "));
        OUT(self->ref_class_entry->id, self->ref_class_entry->id_size);
        OUT("}", 1);
    }

    if (self->ref_procname_size) {
        OUT("{p ", strlen("{p "));
        OUT(self->ref_procname, self->ref_procname_size);
        OUT("}", 1);
    }

    /* choose gloss */
    if (self->tr) {
        err = export_glosses(self, out);
        KND_TASK_ERR("failed to export glosses GSP");
    }

    OUT("}", 1);
    return knd_OK;
}
