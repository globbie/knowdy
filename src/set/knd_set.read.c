#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "knd_class.h"
#include "knd_utils.h"
#include "knd_memblock.h"
#include "knd_mempool.h"
#include "knd_shared_set.h"
#include "knd_repo.h"
#include "knd_task.h"
#include "knd_utils.h"

#include <gsl-parser.h>

#define DEBUG_SET_READ_LEVEL_0 0
#define DEBUG_SET_READ_LEVEL_1 0
#define DEBUG_SET_READ_LEVEL_2 0
#define DEBUG_SET_READ_LEVEL_3 0
#define DEBUG_SET_READ_LEVEL_4 0
#define DEBUG_SET_READ_LEVEL_TMP 1

int knd_set_read_leaf(struct kndSet *s, struct kndStorageLeaf *leaf,
                      knd_set_elem_unmarshall_cb_t unused_var(cb), void *unused_var(cb_ctx), struct kndTask *task)
{
    //char idbuf[KND_ID_SIZE];
    //size_t idbuf_size = 0;
    struct stat st;
    struct kndSetDir *dir = s->dir;
    //struct kndSetDirBlock *block;
    const char *filename = leaf->filepath;
    size_t filename_size = leaf->filepath_size;
    // TODO: check header
    //size_t offset = strlen("GSP");
    int fd;
    int err;

    assert (filename_size != 0);

    if (DEBUG_SET_READ_LEVEL_2) {
        knd_log(".. open storage {leaf %.*s {filepath %.*s} {size %zu}}",
                leaf->name_size, leaf->name, filename_size, filename,
                leaf->curr_size);
    }

    if (stat(filename, &st)) {
        err = knd_IO_FAIL;
        KND_TASK_ERR("no such {file %.*s}", filename_size, filename);
    }

    if (leaf->curr_size != (size_t)st.st_size) {
        err = knd_IO_FAIL;
        KND_TASK_ERR("{file %.*s} size mismatch: expected %zu, not %zu bytes",
                     filename_size, filename, leaf->curr_size, st.st_size);
    }

    fd = open(filename, O_RDONLY);
    if (fd == -1) {
        err = knd_IO_FAIL;
        KND_TASK_ERR("failed to open {file %.*s}", filename_size, filename);
    }

    if (!s->dir) {
        err = knd_set_dir_new(&dir, "", 0, "", s->mempool);
        if (err) {
            KND_TASK_LOG("failed to alloc a set dir");
            goto final;
        }
        dir->id[0] = '/';
        dir->id_size = 1;

        //dir->total_size = st.st_size;
        //dir->global_offset = offset;
        s->dir = dir;
    }



    //err = unmarshall_block(s, dir, fd, st.st_size - offset, idbuf, idbuf_size, cb, task);
    //if (err) goto final;

    //self->num_elems += dir->total_elems;

 final:
    close(fd);
    return err;
}
