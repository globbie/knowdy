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

/* 
 * query complexity assessment 
 */
static int query_plan(struct kndQuery *query, struct kndTask *task)
{
    struct kndRepo *repo = query->repo;
    struct kndAttrStm *stm;
    size_t min_ops = 0;
    int err;

    // TODO query cache lookup

    FOREACH (stm, query->attr_stms) {
        err = knd_attr_stm_plan(stm, repo, task);
        switch (err) {
        case knd_OK:
            query->num_matches += stm->num_matches;
            break;
        case knd_NO_MATCH:
            knd_log("no matches for attr stm");

            break;
        default:
            KND_TASK_ERR("failed to plan attr stm query");
            break;
        }

        if (stm->min_query_ops < min_ops) {
            min_ops = stm->min_query_ops;
        }
    }

    // < KND_QUERY_MIN_OPERS ?
    return knd_OK;
}

/* long-running task */
int knd_query_exec(struct kndQuery *query, struct kndTask *task)
{
    struct kndSet *set;
    //struct kndAttrStm *stm;
    int err;

    //FOREACH (stm, query->attr_stms) {
        
    //}

    err = knd_set_new(&set, KND_SET_UNIQUE_VALUES, task->mempool);
    KND_TASK_ERR("failed to alloc a set");

    //err = knd_set_intersect(set, sets, num_sets);
    //KND_TASK_ERR("failed to intersect sets");

    query->match = set;

    return knd_OK;
}

gsl_err_t knd_query_run(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndQuery *query;
    gsl_err_t parser_err;
    int err;

    err = knd_query_new(&query, task->mempool);
    if (err) return make_gsl_err_external(err);

    task->type = KND_TASK_QUERY;
    task->ctx->query = query;

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
        KND_TASK_LOG("unknown {tag %.*s}", parser_err.val_size, parser_err.val);
        return make_gsl_err(gsl_NO_MATCH);
    default:
        return parser_err;
    }

    switch (query->type) {
    case KND_QUERY_GET:
        err = knd_query_obj_export(query, task);
        if (err) {
            KND_TASK_LOG("failed to present a requested object");
            return make_gsl_err_external(err);
        }
        break;
    case KND_QUERY_SELECT:
        err = query_plan(query, task);
        if (err) {
            KND_TASK_LOG("failed to plan a query");
            return make_gsl_err_external(err);
        }

        if (query->complexity < query->max_complexity) {

            // perform query ops

            err = knd_query_match_export(query, task);
            if (err) {
                KND_TASK_LOG("failed to present the matching results of a query");
                return make_gsl_err_external(err);
            }
            return make_gsl_err(gsl_OK);
        }

        // TODO: signal the need for a long-running task

        break;
    default:
        break;
    }
    
    return make_gsl_err(gsl_OK);
}

