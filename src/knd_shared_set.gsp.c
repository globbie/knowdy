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

static int unmarshall_block(struct kndSharedSet *self, struct kndSharedSetDir *dir,
                            int fd, size_t offset, size_t block_size,
                            char *idbuf, size_t idbuf_size, elem_unmarshall_cb cb, struct kndTask *task);

static int payload_linear_scan(struct kndSharedSet *self, const char *block, size_t block_size,
                               char *idbuf, size_t idbuf_size, elem_unmarshall_cb cb, struct kndTask *task)
{
    const char *b, *c;
    size_t remainder = block_size;
    bool in_tag = true;
    void *result;
    size_t val_size;
    int err;

    b = block;
    c = block;
    idbuf[idbuf_size] = *c;

    while (remainder) {
        switch (*c) {
        case '{':
            val_size = c - b;
            err = cb(idbuf, idbuf_size + 1, b, val_size, &result, task);
            KND_TASK_ERR("failed to unmarshall elem \"%.*s\"", idbuf_size + 1, idbuf);
            err = knd_shared_set_add(self, idbuf, idbuf_size + 1, result);
            KND_TASK_ERR("failed to register elem \"%.*s\"", idbuf_size + 1, idbuf);
        
            c++;
            idbuf[idbuf_size] = *c++;
            remainder -= 2;
            in_tag = true;
            b = c;
            continue;
        default:
            c++;
            remainder--;
        }
    }
    val_size = c - b;
    err = cb(idbuf, idbuf_size + 1, b, val_size, &result, task);
    KND_TASK_ERR("failed to unmarshall elem \"%.*s\"", idbuf_size + 1, idbuf);
    err = knd_shared_set_add(self, idbuf, idbuf_size + 1, result);
    KND_TASK_ERR("failed to register elem \"%.*s\"", idbuf_size + 1, idbuf);
    return knd_OK;
}

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

    if (DEBUG_SHARED_SET_GSP_LEVEL_TMP)
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
        knd_log("== \"%.*s\" use keys:%d  cell size:%zu", idbuf_size, idbuf, use_keys, cell_size);

    // linear scan needed
    if (use_keys) {
        err = payload_linear_scan(self, block, block_size, idbuf, idbuf_size, cb, task);
        KND_TASK_ERR("failed to scan payload");
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

static int read_payload_size(struct kndSharedSetDir *dir, int fd, size_t offset, size_t block_size)
{
    unsigned char buf[KND_NAME_SIZE];
    char size_spec = 0;
    ssize_t num_bytes;
    size_t numval;
    size_t footer_size = 0;

    knd_log(".. reading a payload size spec from offset %zu", offset + block_size - 1);

    // the last byte is an offset size spec
    lseek(fd, offset + block_size - 1, SEEK_SET);
    num_bytes = read(fd, &size_spec, 1);
    if (num_bytes != 1) return knd_IO_FAIL;
    footer_size++;

    if (DEBUG_SHARED_SET_GSP_LEVEL_TMP)
        knd_log("== offset size:%d", size_spec);

    //lseek(fd, offset + block_size - 4, SEEK_SET);
    //num_bytes = read(fd, buf, 4);
    //knd_log("== TAIL: %d %d %d %d ", buf[0], buf[1], buf[2], buf[3]);
    
    if (size_spec == 0) {
        knd_log("-- no payload in this block");
        dir->payload_footer_size = footer_size;
        return knd_OK;
    }
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

    if (DEBUG_SHARED_SET_GSP_LEVEL_TMP)
        knd_log("== payload block size:%zu  payload footer size:%zu", numval, footer_size);

    dir->payload_size = numval;
    dir->payload_footer_size = footer_size;
    return knd_OK;
}

static int read_subdirs(struct kndSharedSet *self, struct kndSharedSetDir *dir,
                        int fd, size_t offset, size_t parent_block_size,
                        char *idbuf, size_t idbuf_size, elem_unmarshall_cb cb, struct kndTask *task)
{
    unsigned char buf[KND_NAME_SIZE];
    size_t spec_size = 2;
    ssize_t num_bytes;
    size_t subdir_area_size;
    size_t subdir_footer_size;
    bool use_keys = false;
    size_t cell_size;
    const unsigned char *c;
    size_t numval;
    size_t block_offset = 0;
    int err;

    if (DEBUG_SHARED_SET_GSP_LEVEL_TMP)
        knd_log(".. reading subdirs from offset %zu + %zu", offset, parent_block_size);

    // the last three bytes contain a dir size spec:
    // dir_size (optional)   cell_size (1-4)   use_keys (0/1)
    // [2]                   [1]               [0]
    lseek(fd, offset + parent_block_size - 3, SEEK_SET);
    num_bytes = read(fd, buf, 3);
    if (num_bytes != 3) return knd_IO_FAIL;

    if (DEBUG_SHARED_SET_GSP_LEVEL_TMP)
        knd_log(">> cell size:%d use keys: %d", buf[1], buf[2]);

    if (buf[2]) use_keys = true;
    if (buf[1] > KND_UINT_SIZE || buf[1] == 0) return knd_LIMIT;
    cell_size = buf[1];

    subdir_footer_size = KND_RADIX_BASE * cell_size;
    if (use_keys) {
        subdir_footer_size = buf[0];
        spec_size = 3;
    }

    // buf is enough
    if (subdir_footer_size > KND_NAME_SIZE) return knd_LIMIT;

    if (DEBUG_SHARED_SET_GSP_LEVEL_TMP)
        knd_log("== cell size:%zu footer size:%zu", cell_size, subdir_footer_size);

    subdir_area_size = parent_block_size - (subdir_footer_size + spec_size);

    lseek(fd, offset + subdir_area_size, SEEK_SET);
    num_bytes = read(fd, buf, subdir_footer_size);
    if (num_bytes != (ssize_t)subdir_footer_size) return knd_IO_FAIL;

    if (use_keys) {
        // TODO: linear traversal
        return knd_OK;
    }

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        c = (unsigned char*)buf + (i * cell_size);
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
        idbuf[idbuf_size] = obj_id_seq[i];

        knd_log("== \"%.*s\" subdir area size:%zu", idbuf_size + 1, idbuf, numval);

        err = unmarshall_block(self, dir, fd, offset + block_offset, numval, idbuf, idbuf_size + 1, cb, task);
        KND_TASK_ERR("failed to unmarshall subdir block %.*s", idbuf_size + 1, idbuf);

        block_offset += numval;
    }
    return knd_OK;
}

static int unmarshall_block(struct kndSharedSet *self, struct kndSharedSetDir *dir,
                            int fd, size_t offset, size_t block_size,
                            char *idbuf, size_t idbuf_size, elem_unmarshall_cb cb, struct kndTask *task)
{
    struct kndRepo *repo = task->repo;
    struct kndMemBlock *block;
    ssize_t num_bytes;
    size_t subdirs_area_size;
    int err;

    if (DEBUG_SHARED_SET_GSP_LEVEL_TMP)
        knd_log("\n.. unmarshall block from offset %zu (size: %zu)", offset, block_size);

    err = read_payload_size(dir, fd, offset, block_size);
    KND_TASK_ERR("failed to read payload size");

    if (dir->payload_size) {
        if (DEBUG_SHARED_SET_GSP_LEVEL_2)
            knd_log("== alloc a payload block of size %zu", dir->payload_size);
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
    }

    subdirs_area_size = block_size - (dir->payload_size + dir->payload_footer_size);
    if (subdirs_area_size) {
        err = read_subdirs(self, dir, fd, offset + dir->payload_size, subdirs_area_size, idbuf, idbuf_size, cb, task);
        if (err) return err;
    }
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

    err = unmarshall_block(self, dir, fd, offset, filesize - offset, idbuf, idbuf_size, cb, task);
    if (err) goto final;
    
 final:
    close(fd);
    return err;
}

