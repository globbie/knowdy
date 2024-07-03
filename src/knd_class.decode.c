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
#include "knd_mempool.h"
#include "knd_memblock.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_user.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_ignore.h"
#include "knd_shared_dict.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
#include "knd_shared_set.h"
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#include <gsl-parser.h>

#define DEBUG_CLASS_DECODE_LEVEL_1 0
#define DEBUG_CLASS_DECODE_LEVEL_2 0
#define DEBUG_CLASS_DECODE_LEVEL_3 0
#define DEBUG_CLASS_DECODE_LEVEL_4 0
#define DEBUG_CLASS_DECODE_LEVEL_5 0
#define DEBUG_CLASS_DECODE_LEVEL_TMP 1

int knd_class_entry_unmarshall(const char *elem_id, size_t elem_id_size,
                               const char *rec, size_t rec_size,
                               void **result, struct kndTask *task)
{
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndClassEntry *entry = NULL;
    struct kndRepo *repo = task->repo;
    struct kndCharSeq *seq;
    const char *c, *name = rec;
    size_t name_size;
    int err;

    if (DEBUG_CLASS_DECODE_LEVEL_2) {
        knd_log(">> GSP class entry \"%.*s\" => \"%.*s\"",
                elem_id_size, elem_id, rec_size, rec);
    }
    err = knd_class_entry_new(&entry, mempool);
    KND_TASK_ERR("failed to alloc a class entry");
    entry->repo = task->repo;
    memcpy(entry->id, elem_id, elem_id_size);
    entry->id_size = elem_id_size;

    /* get name numid */
    c = name;
    while (*c) {
        if (*c == '{' || *c == '[') break;
        c++;
    }
    name_size = c - name;
    if (!name_size) {
        err = knd_FORMAT;
        KND_TASK_ERR("anonymous class entry in GSP");
    }
    if (name_size > KND_ID_SIZE) {
        err = knd_FORMAT;
        KND_TASK_ERR("invalid class name numid in GSP");
    }

    err = knd_charseq_decode(name, name_size, &seq, task);
    KND_TASK_ERR("failed to decode a charseq");

    entry->name = seq->val;
    entry->name_size = seq->val_size;
    entry->seq = seq;

    err = knd_shared_dict_set(task->idxs->class_name_idx, entry->name, entry->name_size,
                              (void*)entry);
    KND_TASK_ERR("failed to register class name");

    err = knd_shared_set_add(task->idxs->class_idx, entry->id, entry->id_size, (void*)entry);
    KND_TASK_ERR("failed to register class entry \"%.*s\"", entry->id_size, entry->id);

    if (DEBUG_CLASS_DECODE_LEVEL_3)
        knd_log("== class name decoded \"%.*s\" => \"%.*s\" {repo %.*s}",
                entry->id_size, entry->id, entry->name_size, entry->name,
                repo->name_size, repo->name);

    *result = entry;
    return knd_OK;
}

