#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>

#include "knd_text.h"
#include "knd_task.h"
#include "knd_repo.h"
#include "knd_class.h"
#include "knd_attr.h"
#include "knd_proc.h"
#include "knd_shard.h"
#include "knd_user.h"
#include "knd_utils.h"
#include "knd_mempool.h"
#include "knd_output.h"

#define DEBUG_TEXT_SEARCH_LEVEL_0 0
#define DEBUG_TEXT_SEARCH_LEVEL_1 0
#define DEBUG_TEXT_SEARCH_LEVEL_2 0
#define DEBUG_TEXT_SEARCH_LEVEL_3 0
#define DEBUG_TEXT_SEARCH_LEVEL_TMP 1

struct LocalContext {
    struct kndTask      *task;
    struct kndRepo      *repo;
    struct kndText      *text;
    struct kndPar       *par;
    struct kndSentence  *sent;
    struct kndSyNode    *syn;
    struct kndStatement *stm;
    struct kndTextSearchReport *report;
};

static int approve_src(struct kndTextSearchReport *pref, struct kndTextIdx *idx)
{
    for (; pref; pref = pref->next) {
        if (pref->entry == idx->entry && pref->attr == idx->attr)
            return knd_OK;
    }
    return knd_NO_MATCH;
}

static gsl_err_t build_search_plan(void *obj, const char *unused_var(name), size_t unused_var(name_size))    
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndTextIdx *idx = NULL;
    struct kndTextSearchReport *report, *pref = task->ctx->reports;
    struct kndClassDeclaration *declar;
    struct kndClassEntry *entry;
    int err;

    if (!ctx->stm) {
        err = knd_FORMAT;
        KND_TASK_LOG("no query given");
        return make_gsl_err_external(err);
    }
    
    knd_log(".. building a text search plan, class declars:%p", ctx->stm->class_declars);

    for (report = pref; report; report = report->next) {
        knd_log(".. search in \"%.*s\"", report->entry->name_size, report->entry->name);
        if (report->attr) {
            knd_log("  >> \"%.*s\"", report->attr->name_size, report->attr->name);
        } else {
            knd_log("  >> all text attrs");
        }
    }

    for (declar = ctx->stm->class_declars; declar; declar = declar->next) {

        entry = declar->entry;

        knd_log(">> class declar: %.*s (repo:%.*s)",
                entry->name_size, entry->name, entry->repo->name_size, entry->repo->name);

        idx = entry->text_idxs; // atomic_load_explicit(&entry->text_idxs, memory_order_relaxed);
        for (; idx; idx = idx->next) {
            assert(idx->entry != NULL);

            //knd_log("text idx: \"%.*s\" (repo:%.*s)",
            //        idx->entry->name_size, idx->entry->name,
            //        idx->entry->repo->name_size, idx->entry->repo->name);

            if (pref) {
                knd_log("++ check pref \"%.*s\" (repo:%.*s)",
                        pref->entry->name_size, pref->entry->name,
                        pref->entry->repo->name_size, pref->entry->repo->name);

                err = approve_src(pref, idx);
                if (err) {
                    knd_log("-- wrong idx");
                    continue;
                }
                pref->num_locs = idx->num_locs;
                continue;
            }

            err = knd_text_search_report_new(task->mempool, &report);
            if (err) {
                KND_TASK_LOG("failed to alloc text idx");
                return make_gsl_err_external(err);
            }
            report->entry = idx->entry;
            report->locs = idx->locs;
            report->num_locs = idx->num_locs;

            report->next = task->ctx->reports;
            task->ctx->reports = report;
        }
    }

    err = knd_text_export_query_report(task);
    if (err) {
        KND_TASK_LOG("failed to export text query report");
        return make_gsl_err_external(err);
    }
    
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_text_src(void *obj, const char *name, size_t name_size)    
{
    struct LocalContext *ctx = obj;
    struct kndTextSearchReport *report = NULL;
    struct kndRepo *repo = ctx->repo;
    struct kndTask *task = ctx->task;
    struct kndClassEntry *entry;
    int err;

    err = knd_get_class_entry(repo, name, name_size, &entry, task);
    if (err) {
        KND_TASK_LOG("class \"%.*s\" not found in repo \"%.*s\"", name_size, name, repo->name_size, repo->name);
        return make_gsl_err_external(err);
    }
    err = knd_text_search_report_new(task->mempool, &report);
    if (err) {
        KND_TASK_LOG("failed to alloc text search report");
        return make_gsl_err_external(err);
    }
    report->entry = entry;
    ctx->report = report;

    report->next = task->ctx->reports;
    task->ctx->reports = report;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_src_attr(void *obj, const char *name, size_t name_size, const char *unused_var(rec), size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndAttrRef *attr_ref;
    struct kndClass *c;
    int err;

    if (!ctx->report) {
        err = knd_FORMAT;
        KND_TASK_LOG("no text class selected");
        return *total_size = 0, make_gsl_err_external(err);
    }

    c = ctx->report->entry->class;
    err = knd_class_get_attr(c, name, name_size, &attr_ref);
    if (err) {
        KND_TASK_LOG("attr \"%.*s\" not found in class \"%.*s\"", name_size, name, c->name_size, c->name);
        return *total_size = 0, make_gsl_err_external(err);
    }

    ctx->report->attr = attr_ref->attr;
    
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_text_src(void *obj, const char *rec, size_t *total_size)
{
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_text_src,
          .obj = obj
        },
        { .validate = parse_src_attr,
          .obj = obj
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_text_stm(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->mempool;
    if (task->user_ctx)
        mempool = task->shard->user->mempool;
    struct kndStatement *stm;
    gsl_err_t parser_err;
    int err;

    knd_log(".. new text search stm");
    err = knd_statement_new(mempool, &stm);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    ctx->stm = stm;

    parser_err = knd_statement_import(stm, rec, total_size, task);
    if (parser_err.code) {
        knd_log("-- text stm parsing failed: %d", parser_err.code);
        return *total_size = 0, parser_err;
    }
    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_text_search(struct kndRepo *repo, const char *rec, size_t *total_size, struct kndTask *task)
{
    struct LocalContext ctx = {
        .task = task,
        .repo = repo
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = set_text_src,
          .obj = &ctx
        },
        { .name = "src",
          .name_size = strlen("src"),
          .is_selector = true,
          .parse = parse_text_src,
          .obj = &ctx
        },
        { .name = "stm",
          .name_size = strlen("stm"),
          .is_selector = true,
          .parse = parse_text_stm,
          .obj = &ctx
        },
        { .is_default = true,
          .run = build_search_plan,
          .obj = &ctx
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}
