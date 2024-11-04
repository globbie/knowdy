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

#define DEBUG_QUERY_GSL_LEVEL_1 0
#define DEBUG_QUERY_GSL_LEVEL_2 0
#define DEBUG_QUERY_GSL_LEVEL_3 0
#define DEBUG_QUERY_GSL_LEVEL_4 0
#define DEBUG_QUERY_GSL_LEVEL_5 0
#define DEBUG_QUERY_GSL_LEVEL_TMP 1

#include "knd_query.h"

static int export_base_preds(struct kndQuery *self, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndClassBasePred *bp;
    size_t bp_count = 0;
    size_t indent_size = task->ctx->format_indent;
    int err;

    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, depth * indent_size);
        RET_ERR();
    }
    OUT("[is", strlen("[is"));

    FOREACH (bp, self->base_preds) {
        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth + 1) * indent_size);
            RET_ERR();
        }
        OUT("{ ", strlen("{ "));
        OUT(bp->entry->name, bp->entry->name_size);

        // TODO
        /*err = knd_class_acquire(bp->entry, &c, task);
        KND_TASK_ERR("failed to acquire baseclass %.*s",
                     bp->entry->name_size, bp->entry->name);

        if (c->tr) {
            err = knd_text_gloss_export_GSL(c->tr, true, task, depth + 2);
            KND_TASK_ERR("failed to export baseclass gloss GSL");
            }*/
       
        if (bp->attr_stms) {
            //curr_depth = task->ctx->depth;
            err = knd_attr_stms_export_GSL(bp->attr_stms, task, false, depth + 1);
            KND_TASK_ERR("failed to export attr vars GSL");
            //task->ctx->depth = curr_depth;   
        }
        OUT("}", 1);
        bp_count++;
    }
    OUT("]", 1);
    return knd_OK;
}

int knd_query_export_GSL(struct kndQuery *self, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    size_t indent_size = task->ctx->format_indent;
    size_t num_children;
    bool use_locale = true;
    int err;

    if (DEBUG_QUERY_GSL_LEVEL_2) {
        knd_log(".. GSL export {repo %.*s {query {type %d}}",
                self->repo->name_size, self->repo->name, self->type);
    }
    OUT("{", 1);
    OUT("query", strlen("query"));
    OUT(" ", 1);

    if (self->num_base_preds) {
        err = export_base_preds(self, task, 0);
        RET_ERR();
    }

    OUT("}", 1);
    return knd_OK;
}
