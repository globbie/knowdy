#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "knd_class.h"
#include "knd_utils.h"
#include "knd_mempool.h"
#include "knd_shared_set.h"
#include "knd_repo.h"
#include "knd_task.h"
#include "knd_utils.h"

#include <gsl-parser.h>

#define DEBUG_SHARED_SET_GSP_LEVEL_0 0
#define DEBUG_SHARED_SET_GSP_LEVEL_1 0
#define DEBUG_SHARED_SET_GSP_LEVEL_2 0
#define DEBUG_SHARED_SET_GSP_LEVEL_3 0
#define DEBUG_SHARED_SET_GSP_LEVEL_4 0
#define DEBUG_SHARED_SET_GSP_LEVEL_TMP 1

static int unmarshall_elems(struct kndSharedSet *self, struct kndSharedSetDir *unused_var(dir),
                            const char *block, size_t block_size,
                            char *idbuf, size_t idbuf_size, elem_unmarshall_cb cb, struct kndTask *task)
{
    char spec;
    bool use_keys = false;
    size_t footer_size = 0;
    size_t cell_size = 0;
    size_t dir_size = 0;
    const char *b, *e;
    const unsigned char *c;
    size_t numval;
    size_t remainder;
    void *result;
    int err;

    if (DEBUG_SHARED_SET_GSP_LEVEL_2)
        knd_log(".. unmarshall payload block \"%.*s\"", block_size, block);

    // use keys spec
    spec = block[block_size - 1];
    footer_size++;
    if (spec) use_keys = true;

    // read cell size
    footer_size++;
    if (block_size < footer_size) return knd_LIMIT;
    spec = block[block_size - footer_size];
    if (spec > KND_UINT_SIZE) return knd_LIMIT;

    cell_size = spec;

    if (DEBUG_SHARED_SET_GSP_LEVEL_3)
        knd_log("== use keys:%d  cell size:%zu", use_keys, cell_size);

    // linear scan needed
    if (use_keys) {
        // TODO
    } else {
        // fixed cell size
        dir_size = KND_RADIX_BASE * cell_size;
        if (block_size < dir_size + footer_size) return knd_LIMIT;

        remainder = block_size - (dir_size + footer_size);
        b = block + remainder;
        e = block;

        for (size_t i = 0; i < KND_RADIX_BASE; i++) {
            c = (unsigned char*)b + (i * cell_size);

            switch (cell_size) {
            case 4:
                numval = knd_unpack_u32(c);
                break;
            case 3:
                numval = knd_unpack_u24(c);
                break;
            case 2:
                numval = knd_unpack_u16(c);
                break;
            default:
                numval = *c;
                break;
            }
            if (numval == 0) continue;
            if (numval > remainder) return knd_LIMIT;

            //if (numval > (block_size - footer_size)) return knd_LIMIT;
            idbuf[idbuf_size] = obj_id_seq[i];
            
            err = cb(idbuf, idbuf_size + 1, e, numval, &result, task);
            KND_TASK_ERR("failed to unmarshall elem \"%.*s\"", idbuf_size + 1, idbuf);

            err = knd_shared_set_add(self, idbuf, idbuf_size + 1, result);
            KND_TASK_ERR("failed to register elem \"%.*s\"", idbuf_size + 1, idbuf);

            e += numval;
            remainder -= numval;
        }
    }
    return knd_OK;
}

static int read_payload_size(struct kndSharedSetDir *dir, int fd, size_t block_size, size_t offset)
{
    unsigned char buf[KND_NAME_SIZE];
    char size_spec = 0;
    ssize_t num_bytes;
    size_t numval;
    size_t footer_size = 0;

    // the last byte is an offset size spec
    lseek(fd, offset + block_size - 1, SEEK_SET);
    num_bytes = read(fd, &size_spec, 1);
    if (num_bytes != 1) return knd_IO_FAIL;
    footer_size++;

    if (DEBUG_SHARED_SET_GSP_LEVEL_3)
        knd_log("== offset size:%d", size_spec);

    if (size_spec > KND_UINT_SIZE) return knd_LIMIT;

    footer_size += size_spec;
    lseek(fd, offset + block_size - footer_size, SEEK_SET);
    num_bytes = read(fd, &buf, (size_t)size_spec);
    if (num_bytes != (ssize_t)size_spec) return knd_IO_FAIL;

    switch (size_spec) {
    case 4:
        numval = knd_unpack_u32(buf);
        break;
    case 3:
        numval = knd_unpack_u24(buf);
        break;
    case 2:
        numval = knd_unpack_u16(buf);
        break;
    default:
        numval = *buf;
        break;
    }
    if (numval > (block_size - footer_size)) return knd_LIMIT;

    if (DEBUG_SHARED_SET_GSP_LEVEL_3)
        knd_log("== payload block size:%zu  payload footer size:%zu", numval, footer_size);

    dir->payload_size = numval;
    dir->payload_footer_size = footer_size;
    return knd_OK;
}

static int unmarshall_block(struct kndSharedSet *self, struct kndSharedSetDir *dir,
                            int fd, size_t block_size, size_t offset,
                            char *idbuf, size_t idbuf_size, elem_unmarshall_cb cb, struct kndTask *task)
{
    struct kndRepo *repo = task->repo;
    struct kndMemBlock *block;
    ssize_t num_bytes;
    int err;

    err = read_payload_size(dir, fd, block_size, offset);
    if (err) return err;

    block = calloc(1, sizeof(struct kndMemBlock));
    if (!block) {
        err = knd_NOMEM;
        KND_TASK_ERR("block alloc failed");
    }
    block->buf_size = dir->payload_size + 1;

    char *b = malloc(block->buf_size);
    if (!b) {
        err = knd_NOMEM;
        KND_TASK_ERR("file memblock alloc failed");
    }
    b[dir->payload_size] = '\0';
    block->buf = b;

    lseek(fd, offset, SEEK_SET);
    num_bytes = read(fd, b, dir->payload_size);
    if (num_bytes != (ssize_t)dir->payload_size) return knd_IO_FAIL;

    block->next = repo->blocks;
    repo->blocks = block;
    repo->num_blocks++;
    repo->total_block_size += block->buf_size;

    err = unmarshall_elems(self, dir, block->buf, dir->payload_size, idbuf, idbuf_size, cb, task);
    if (err) return err;

    return knd_OK;
}

int knd_shared_set_unmarshall_file(struct kndSharedSet *self, const char *filename, size_t filesize,
                                   elem_unmarshall_cb cb, struct kndTask *task)
{
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size = 0;
    struct kndSharedSetDir *dir;
    // TODO: check header
    size_t offset = strlen("GSP");
    int fd;
    int err;
    
    fd = open(filename, O_RDONLY);
    if (fd == -1) {
        err = knd_IO_FAIL;
        KND_TASK_ERR("failed to open file");
    }

    dir = calloc(1, sizeof(struct kndSharedSetDir));
    if (!dir) {
        err = knd_NOMEM;
        KND_TASK_LOG("failed to alloc kndSharedSetDir");
        goto final;
    }

    err = unmarshall_block(self, dir, fd, filesize - offset, offset, idbuf, idbuf_size, cb, task);
    if (err) goto final;
    
 final:
    close(fd);
    return err;
}

