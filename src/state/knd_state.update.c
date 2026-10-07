#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "knd_mempool.h"
#include "knd_output.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_utils.h"

#define DEBUG_STATE_UPDATE_LEVEL_1 0
#define DEBUG_STATE_UPDATE_LEVEL_2 0
#define DEBUG_STATE_UPDATE_LEVEL_3 0
#define DEBUG_STATE_UPDATE_LEVEL_4 0
#define DEBUG_STATE_UPDATE_LEVEL_5 0
#define DEBUG_STATE_UPDATE_LEVEL_TMP 1

int knd_state_apply_upstream_updates(struct kndRepoSnapshot *unused_var(snapshot), struct kndTask *unused_var(task))
{
    knd_log(".. apply upstream updates");

    // TODO
    return knd_OK;
}

int knd_state_advance(struct kndRepoSnapshot *snapshot, struct kndStateLedger *ledger,
                      struct kndTask *task)
{
    knd_log("++ advance global state");

    return knd_OK;
}
