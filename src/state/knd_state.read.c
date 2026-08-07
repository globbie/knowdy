#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "knd_mempool.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_utils.h"

#define DEBUG_STATE_READ_LEVEL_1 0
#define DEBUG_STATE_READ_LEVEL_2 0
#define DEBUG_STATE_READ_LEVEL_3 0
#define DEBUG_STATE_READ_LEVEL_4 0
#define DEBUG_STATE_READ_LEVEL_5 0
#define DEBUG_STATE_READ_LEVEL_TMP 1

int knd_state_read(struct kndRepoSnapshot *snapshot, struct kndStateRange *range, struct kndTask *task)
{
    struct kndRepo *repo = snapshot->repo;
    size_t writer_id;

    if (DEBUG_STATE_READ_LEVEL_TMP) {
        knd_log(">> reading the state of {repo %.*s}",
                repo->name_size, repo->name);
    }

    return knd_OK;
}
