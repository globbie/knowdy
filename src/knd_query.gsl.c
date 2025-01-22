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

static int export_class_entry_GSL(void *obj, const char *elem_id, size_t elem_id_size,
                                  size_t count, void *elem)
{
    struct kndTask *task = obj;
    struct kndQueryView *view = task->ctx->query->view;
    //struct kndBatchLimits *batch = view->batch;
    //if (count < batch->from) return knd_OK;
    //if (batch->size >= batch->max_items) return knd_RANGE;

    struct kndOutput *out = task->out;
    struct kndClassEntry *entry = elem;
    struct kndClass *c;
    size_t curr_depth = 0;
    int err;

    if (DEBUG_QUERY_GSL_LEVEL_2) {
        knd_log(".. Query GSL export {class %.*s}",
                entry->name_size, entry->name);
    }

    err = knd_class_acquire(entry, &c, task);
    KND_TASK_ERR("failed to acquire class %.*s", entry->name_size, entry->name);

    err = knd_class_export_GSL(c, task, true, 1);
    KND_TASK_ERR("failed to export GSL {class %.*s}", entry->name_size, entry->name);

    task->depth = curr_depth;
    // batch->size++;
    return knd_OK;
}

static int export_attr_stms(struct kndQuery *query, struct kndTask *task, size_t depth)
{
    struct kndAttrStm *stm;
    struct kndOutput *out = task->out;
    size_t indent_size = task->ctx->format_indent;
    int err;

    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, depth * indent_size);
        RET_ERR();
    }
    OUT("[", 1);
    OUT("stm", strlen("stm"));

    FOREACH (stm, query->attr_stms) {

        OUT("{", 1);

        if (stm->match) {
            OUT("[", 1);
            OUT("cls", strlen("cls"));

            err = knd_set_map(stm->match, export_class_entry_GSL, (void*)task);
            KND_TASK_ERR("failed to export attr stm matching set to GSL");
            OUT("]", 1);
        }

        OUT("}", 1);
    }

    OUT("]", 1); // stm list
    return knd_OK;
}

int knd_query_export_GSL(struct kndQuery *query, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    //size_t indent_size = task->ctx->format_indent;
    int err;

    if (DEBUG_QUERY_GSL_LEVEL_2) {
        knd_log(".. GSL export {repo %.*s {query {type %d}}",
                query->repo->name_size, query->repo->name, query->type);
    }
    OUT("{", 1);
    OUT("query", strlen("query"));
    OUT(" ", 1);

    if (query->num_attr_stms) {
        err = export_attr_stms(query, task, 0);
        KND_TASK_ERR("failed to export attr stms to GSL");
    }

    OUT("}", 1);
    return knd_OK;
}
