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
#include "knd_state.h"
#include "knd_query.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_QUERY_LEVEL_0 0
#define DEBUG_QUERY_LEVEL_1 0
#define DEBUG_QUERY_LEVEL_2 0
#define DEBUG_QUERY_LEVEL_3 0
#define DEBUG_QUERY_LEVEL_TMP 1

int knd_query_obj_export(struct kndQuery *query, struct kndTask *task)
{
    int err;

    switch (task->ctx->format) {
    case KND_FORMAT_JSON:
        //err = knd_query_results_export_JSON(query, task, 0);
        //KND_TASK_ERR("failed to export query result in JSON");
        break;
    default:
        err = knd_query_obj_export_GSL(query, task, 0);
        KND_TASK_ERR("failed to export a requested object in GSL");
        break;
    }
    return knd_OK;
}

int knd_query_match_export(struct kndQuery *query, struct kndTask *task)
{
    int err;

    switch (task->ctx->format) {
    case KND_FORMAT_JSON:
        //err = knd_query_results_export_JSON(query, task, 0);
        //KND_TASK_ERR("failed to export query result in JSON");
        break;
    default:
        err = knd_query_match_export_GSL(query, task, 0);
        KND_TASK_ERR("failed to export query matching results in GSL");
        break;
    }
    return knd_OK;
}

int knd_query_new(struct kndQuery **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->small_x2_page_size >= sizeof(struct kndQuery));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL_X2, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndQuery));
    *result = page;
    return knd_OK;
}
