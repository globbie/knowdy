#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

/* numeric conversion by strtol */
#include <errno.h>
#include <limits.h>

#include "knd_config.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_attr_stm.h"
#include "knd_task.h"
#include "knd_state.h"
#include "knd_commit.h"
#include "knd_query.h"
#include "knd_user.h"
#include "knd_repo.h"
#include "knd_mempool.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_shared_set.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#define DEBUG_COMMIT_GSL_LEVEL_1 0
#define DEBUG_COMMIT_GSL_LEVEL_2 0
#define DEBUG_COMMIT_GSL_LEVEL_3 0
#define DEBUG_COMMIT_GSL_LEVEL_4 0
#define DEBUG_COMMIT_GSL_LEVEL_5 0
#define DEBUG_COMMIT_GSL_LEVEL_TMP 1

int knd_commit_export_GSL(struct kndCommit *commit, size_t *total_size, struct kndTask *task)
{
    struct kndOutput *out = task->file_out;
    struct kndRepo *repo = commit->snapshot->repo;
    struct kndStateRef *ref;
    struct kndState *state;
    struct kndClassEntry *entry;
    struct kndClassInst *user_inst;
    int err;

    out->reset(out);
    task->ctx->max_depth = KND_MAX_DEPTH;
    OUT("{task", strlen("{task"));

    switch (task->user_ctx->type) {
    case KND_USER_AUTHENTICATED:
        user_inst = task->user_ctx->inst;
        OUT("{user ", strlen("{user "));
        OUT(user_inst->name, user_inst->name_size);
        break;
    default:
        break;
    }

    OUT("{repo ", strlen("{repo "));
    OUT(repo->name, repo->name_size);

#if 0
    FOREACH (ref, commit->class_state_refs) {
        entry = ref->obj;
        if (!entry) continue;

        err = out->writec(out, '{');                                              RET_ERR();

        OUT("cls ", strlen("cls "));
        OUT(entry->name, entry->name_size);

        OUT("}", 1);
    }    
#endif

    OUT("}", 1);
    if (task->user_ctx) {
        OUT("}", 1);
    }
    OUT("}", 1);

    *total_size = out->buf_size;
    return knd_OK;
}

int knd_commit_calc_GSL_size(struct kndCommit *unused_var(commit), size_t *result_size,
                             struct kndTask *unused_var(task))
{
    size_t total_size = 0;

    *result_size = total_size;
    return knd_OK;
}
