#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "knd_task.h"
#include "knd_steward.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_user.h"
#include "knd_set.h"
#include "knd_mempool.h"
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_query.h"
#include "knd_commit.h"

#include <gsl-parser.h>
#include <gsl-parser/gsl_err.h>

#define DEBUG_TASK_CACHE_LEVEL_0 0
#define DEBUG_TASK_CACHE_LEVEL_1 0
#define DEBUG_TASK_CACHE_LEVEL_2 0
#define DEBUG_TASK_CACHE_LEVEL_3 0
#define DEBUG_TASK_CACHE_LEVEL_TMP 1

int knd_task_cache_update(struct kndTask *unused_var(task))
{
    knd_log(".. update task cache..");

    
    return knd_OK;
}


