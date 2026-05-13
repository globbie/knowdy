#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_class.h"
#include "knd_utils.h"
#include "knd_repo.h"
#include "knd_mempool.h"
#include "knd_set.h"
#include "knd_task.h"

#include <gsl-parser.h>

#define DEBUG_SET_FETCH_LEVEL_0 0
#define DEBUG_SET_FETCH_LEVEL_1 0
#define DEBUG_SET_FETCH_LEVEL_2 0
#define DEBUG_SET_FETCH_LEVEL_3 0
#define DEBUG_SET_FETCH_LEVEL_4 0
#define DEBUG_SET_FETCH_LEVEL_TMP 1

static int get_elem(struct kndSet *s, struct kndSetDir *parent_dir,
                    void **result, const char *id, size_t id_size)
{
    struct kndSetDir *dir;
    void *elem;
    int dir_pos;
    int err;

    dir_pos = obj_id_base[(unsigned char)*id];

    if (DEBUG_SET_FETCH_LEVEL_2) {
        knd_log(".. get elem by ID, {id-remainder %.*s} {dir-pos %d}",
                id_size, id, dir_pos);
    }
    if (id_size > 1) {
        dir = parent_dir->subdirs[dir_pos];
        if (!dir) return knd_NO_MATCH;

        err = get_elem(s, dir, result, id + 1, id_size - 1);
        if (err) return err;

        return knd_OK;
    }

    elem = parent_dir->elems[dir_pos];
    if (!elem) {
        return knd_NO_MATCH;
    }

    *result = elem;
    return knd_OK;
}

int knd_set_fetch_elem(struct kndSet *s, struct kndStorageLeaf *leaf, const char *key, size_t key_size,
                       knd_set_elem_unmarshall_cb_t cb, void *cb_ctx,
                       void **result, struct kndTask *task)
{
    int err;

    assert (leaf != NULL);

    knd_log(".. fetching elem from {leaf %zu}", leaf->numid);

    //err = fetch_elem(s, s->dir, elem, key, key_size, cb, ctx);
    //if (err) return err;

    return knd_NO_MATCH;
}

