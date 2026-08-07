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
#include "knd_commit.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_COMMIT_INDEX_LEVEL_0 0
#define DEBUG_COMMIT_INDEX_LEVEL_1 0
#define DEBUG_COMMIT_INDEX_LEVEL_2 0
#define DEBUG_COMMIT_INDEX_LEVEL_3 0
#define DEBUG_COMMIT_INDEX_LEVEL_TMP 1

int knd_commit_index(struct kndCommit *commit, struct kndStateLedger *ledger, struct kndTask *task)
{
    int err;

    if (DEBUG_COMMIT_INDEX_LEVEL_TMP) {
        knd_log(".. indexing {commit #%zu}", commit->numid);
    }

    
    return knd_OK;
}

int knd_state_index_commits(struct kndRepoSnapshot *snapshot,
                            struct kndStateLedger *ledger, struct kndTask *task)
{
    struct kndCommit *commit;
    int err;

    if (DEBUG_COMMIT_INDEX_LEVEL_TMP) {
        knd_log(".. indexing commits of {collector #%zu}", task->id);
    }

    //err = knd_set_map();

    return knd_OK;
}
