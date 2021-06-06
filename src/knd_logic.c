#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_attr.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_proc_call.h"
#include "knd_task.h"
#include "knd_class.h"
#include "knd_text.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_set.h"
#include "knd_mempool.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_LOGIC_LEVEL_1 0
#define DEBUG_LOGIC_LEVEL_2 0
#define DEBUG_LOGIC_LEVEL_3 0
#define DEBUG_LOGIC_LEVEL_4 0
#define DEBUG_LOGIC_LEVEL_5 0
#define DEBUG_LOGIC_LEVEL_TMP 1

int knd_situation_new(struct kndMemPool *mempool, struct kndSituation **result)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndSituation));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndSituation));
    *result = page;
    return knd_OK;
}

int knd_logic_clause_new(struct kndMemPool *mempool, struct kndLogicClause **result)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndLogicClause));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndLogicClause));
    *result = page;
    return knd_OK;
}
