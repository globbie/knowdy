#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <stdatomic.h>
#include <unistd.h>

#include "knd_state.h"
#include "knd_commit.h"
#include "knd_output.h"
#include "knd_storage.h"
#include "knd_utils.h"
#include "knd_config.h"

#include <gsl-parser.h>

#define DEBUG_STATE_WAL_LEVEL_0 0
#define DEBUG_STATE_WAL_LEVEL_1 0
#define DEBUG_STATE_WAL_LEVEL_2 0
#define DEBUG_STATE_WAL_LEVEL_3 0
#define DEBUG_STATE_WAL_LEVEL_TMP 1

int knd_state_update_collector_wal(struct kndRepoSnapshot *snapshot, struct kndStateLedger *ledger,
                                   struct kndTask *task)
{
    knd_log(".. updating collector's WAL");


    
    return knd_OK;
}
