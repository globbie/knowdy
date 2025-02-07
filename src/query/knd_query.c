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

int knd_query_plan(struct kndQuery *query, struct kndTask *task)
{
    struct kndAttrStm *stm;
    size_t min_ops = 0;
    int err;

    // TODO query cache lookup

    FOREACH (stm, query->attr_stms) {
        err = knd_attr_stm_plan(stm, task);
        switch (err) {
        case knd_OK:
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
