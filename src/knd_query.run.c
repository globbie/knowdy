#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <stdatomic.h>

#include "knd_repo.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_proc.h"
#include "knd_set.h"
#include "knd_state.h"
#include "knd_query.h"
#include "knd_user.h"
#include "knd_output.h"

#define DEBUG_QUERY_RUN_LEVEL_0 0
#define DEBUG_QUERY_RUN_LEVEL_1 0
#define DEBUG_QUERY_RU_LEVEL_2 0
#define DEBUG_QUERY_RUN_LEVEL_3 0
#define DEBUG_QUERY_RUN_LEVEL_TMP 1

static gsl_err_t set_format(void *obj, const char *name, size_t name_size)
{
    struct kndTask *self = obj;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);

    for (size_t i = 0; i < sizeof knd_format_names / sizeof knd_format_names[0]; i++) {
        const char *format_str = knd_format_names[i];
        assert(format_str != NULL);

        size_t format_str_size = strlen(format_str);
        if (name_size != format_str_size) continue;

        if (!memcmp(format_str, name, name_size)) {
            self->ctx->format = (knd_format)i;
            return make_gsl_err(gsl_OK);
        }
    }

    err = self->log->write(self->log, name, name_size);
    if (err) return make_gsl_err_external(err);
    err = self->log->write(self->log, " format not supported",
                           strlen(" format not supported"));
    if (err) return make_gsl_err_external(err);

    return make_gsl_err_external(knd_NO_MATCH);
}

static gsl_err_t parse_format(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *self = obj;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_format,
          .obj = self
        },
        { .name = "indent",
          .name_size = strlen("indent"),
          .parse = gsl_parse_size_t,
          .obj = &self->ctx->format_indent
        },
        { .name = "depth",
          .name_size = strlen("depth"),
          .parse = gsl_parse_size_t,
          .obj = &self->ctx->max_depth
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t set_locale(void *obj, const char *name, size_t name_size)
{
    struct kndTask *self = obj;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size > sizeof(self->ctx->locale)) return make_gsl_err(gsl_FORMAT);

    memcpy(self->ctx->locale, name, name_size);
    self->ctx->locale_size = name_size;

    knd_log(">> set {locale %.*s}", name_size, name);

    /* TODO check locale
    for (size_t i = 0; i < sizeof knd_format_names / sizeof knd_format_names[0]; i++) {
        const char *format_str = knd_format_names[i];
        assert(format_str != NULL);

        size_t format_str_size = strlen(format_str);
        if (name_size != format_str_size) continue;

        if (!memcmp(format_str, name, name_size)) {
            self->ctx->format = (knd_format)i;
            return make_gsl_err(gsl_OK);
        }
    }

    err = self->log->write(self->log, name, name_size);
    if (err) return make_gsl_err_external(err);
    err = self->log->write(self->log, " locale not supported",
                           strlen(" locale not supported"));
    if (err) return make_gsl_err_external(err);
    */

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_locale(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *self = obj;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_locale,
          .obj = self
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

int knd_query_run(struct kndQuery *query, struct kndTask *task)
{
    struct kndSet *set;
    struct kndAttrStm *stm;
    int err;

    //FOREACH (stm, query->attr_stms) {
        
    //}

    err = knd_set_new(&set, task->mempool);
    KND_TASK_ERR("failed to alloc a set");

    //err = knd_set_intersect(set, sets, num_sets);
    //KND_TASK_ERR("failed to intersect sets");

    query->match = set;

    return knd_OK;
}

gsl_err_t knd_parse_query(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndQuery *query;
    gsl_err_t parser_err;
    int err;

    struct gslTaskSpec specs[] = {
        { .name = "locale",
          .name_size = strlen("locale"),
          .parse = parse_locale,
          .obj = task
        },
        { .name = "format",
          .name_size = strlen("format"),
          .parse = parse_format,
          .obj = task
        },
        { .name = "user",
          .name_size = strlen("user"),
          .parse = knd_parse_select_user,
          .obj = task
        },
        { .name = "repo",
          .name_size = strlen("repo"),
          .parse = knd_parse_repo_select,
          .obj = task
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    switch (parser_err.code) {
    case gsl_OK:
        break;
    case gsl_NO_MATCH:
        KND_TASK_LOG("unknown tag: %.*s", parser_err.val_size, parser_err.val);
        // fall through
    default:
        return parser_err;
    }

    knd_log(".. present query results..");

    query = task->ctx->query;

    err = knd_query_export_GSL(query, task);
    if (err) {
        KND_TASK_LOG("failed to present a query");
        return make_gsl_err_external(err);
    }

    return make_gsl_err(gsl_OK);
}

