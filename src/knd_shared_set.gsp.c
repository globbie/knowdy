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
                            int fd, size_t block_size,
                            char *idbuf, size_t idbuf_size, elem_unmarshall_cb cb, struct kndTask *task);

static int payload_linear_scan(struct kndSharedSetDir *dir,
                               const char *block, size_t block_size, char *idbuf, size_t idbuf_size,
                               elem_unmarshall_cb cb, struct kndTask *task)
{
    const char *b, *c;
    size_t remainder = block_size - 1;
    // bool in_tag = true;
    void *result;
    size_t val_size;
    int err;

    if (DEBUG_SHARED_SET_GSP_LEVEL_2)
        knd_log(".. linear scan of \"%.*s\" [size:%zu]", block_size, block, block_size);

    idbuf[idbuf_size] = *block;
    idbuf_size++;
    c = block + 1;
    b = c;

    while (remainder) {
        switch (*c) {
        case '\0':
            val_size = c - b;
            err = cb(idbuf, idbuf_size, b, val_size, &result, task);
            KND_TASK_ERR("failed to unmarshall elem \"%.*s\"", idbuf_size, idbuf);
            dir->num_elems++;
            c++;
            idbuf[idbuf_size - 1] = *c++;
            remainder -= 2;
            // in_tag = true;
            b = c;
            continue;
        default:
            c++;
            remainder--;
        }
    }
    val_size = c - b;
    err = cb(idbuf, idbuf_size, b, val_size, &result, task);
    KND_TASK_ERR("failed to unmarshall elem \"%.*s\"", idbuf_size, idbuf);
    dir->num_elems++;
    return knd_OK;
}

static int fetch_elem_linear_scan(const char *block, size_t block_size, const char *id, size_t id_size,
                                  elem_unmarshall_cb cb, void **result, struct kndTask *task)
{
    char curr_id;
    const char *b, *c;
    size_t remainder = block_size - 1;
    // bool in_tag = true;
    size_t val_size;
    int err;
    if (DEBUG_SHARED_SET_GSP_LEVEL_2)
        knd_log(".. linear scan of block \"%.*s\" [size:%zu] to fetch elem \"%.*s\"",
                block_size, block, block_size, id_size, id);

    curr_id = *block;
    c = block + 1;
    b = c;

    while (remainder) {
        switch (*c) {
        case '\0':
            val_size = c - b;
            if (curr_id == *id) {
                err = cb(id, id_size, b, val_size, result, task);
                KND_TASK_ERR("failed to unmarshall elem \"%.*s\"", id_size, id);
                return knd_OK;
            }
            c++;
            curr_id = *c++;
            if (curr_id > *id) return knd_NO_MATCH;

            remainder -= 2;
            // in_tag = true;
            b = c;
            continue;
        default:
            c++;
            remainder--;
        }
    }
    val_size = c - b;
    if (curr_id == *id) {
        err = cb(id, id_size, b, val_size, result, task);
        KND_TASK_ERR("failed to unmarshall elem \"%.*s\"", id_size, id);
        return knd_OK;
    }
    return knd_NO_MATCH;
}

static int unmarshall_elems(struct kndSharedSetDir *dir, const char *block, size_t block_size,
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
        knd_log("== \"%.*s\" use keys:%d  cell size:%zu", idbuf_size, idbuf, use_keys, cell_size);

    // linear scan needed
    if (use_keys) {
        err = payload_linear_scan(dir, block, block_size - footer_size, idbuf, idbuf_size, cb, task);
        KND_TASK_ERR("failed to scan payload");
        dir->elems_linear_scan = true;
    } else {
        // fixed cell size
        dir_size = KND_RADIX_BASE * cell_size;
        if (block_size < dir_size + footer_size) return knd_LIMIT;

        remainder = block_size - (dir_size + footer_size);
        b = block + remainder;
        e = block;

        for (size_t i = 0; i < KND_RADIX_BASE; i++) {
            c = (unsigned char*)b + (i * cell_size);
            numval = knd_unpack_int(c, cell_size);
            if (numval == 0) continue;
            if (numval > remainder) return knd_LIMIT;
            idbuf[idbuf_size] = obj_id_seq[i];

            dir->elem_block_sizes[i] = numval;

            err = cb(idbuf, idbuf_size + 1, e, numval, &result, task);
            KND_TASK_ERR("failed to unmarshall elem \"%.*s\"", idbuf_size + 1, idbuf);
            dir->num_elems++;

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

    if (DEBUG_SHARED_SET_GSP_LEVEL_2)
        knd_log(".. reading payload size spec from offset %zu", offset + block_size - 1);

    // the last byte is an offset size spec
    lseek(fd, offset + block_size - 1, SEEK_SET);
    num_bytes = read(fd, &size_spec, 1);
    if (num_bytes != 1) return knd_IO_FAIL;
    footer_size++;

    if (DEBUG_SHARED_SET_GSP_LEVEL_2)
        knd_log("== payload offset size:%d", size_spec);
    
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
    numval = knd_unpack_int(buf, size_spec);
    if (numval > (block_size - footer_size)) return knd_LIMIT;

    if (DEBUG_SHARED_SET_GSP_LEVEL_2)
        knd_log("== payload block size:%zu  payload footer size:%zu", numval, footer_size);

    dir->payload_block_size = numval;
    dir->payload_footer_size = footer_size;
    return knd_OK;
}

static int read_subdirs(struct kndSharedSet *self, struct kndSharedSetDir *dir,
                        int fd, size_t offset, size_t parent_block_size,
                        char *idbuf, size_t idbuf_size, elem_unmarshall_cb cb, struct kndTask *task)
{
    unsigned char buf[KND_NAME_SIZE];
    struct kndSharedSetDir *subdir;
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

    if (DEBUG_SHARED_SET_GSP_LEVEL_2)
        knd_log(".. reading subdirs from offset %zu + %zu", offset, parent_block_size);

    // the last three bytes contain a dir size spec:
    // dir_size (optional)   cell_size (1-4)   use_keys (0/1)
    // [2]                   [1]               [0]
    lseek(fd, offset + parent_block_size - 3, SEEK_SET);
    num_bytes = read(fd, buf, 3);
    if (num_bytes != 3) return knd_IO_FAIL;

    if (DEBUG_SHARED_SET_GSP_LEVEL_2)
        knd_log(">> cell size:%d use keys: %d", buf[1], buf[2]);

    if (buf[2]) use_keys = true;
    if (buf[1] > KND_UINT_SIZE || buf[1] == 0) return knd_LIMIT;
    cell_size = buf[1];

    subdir_footer_size = KND_RADIX_BASE * cell_size;
    if (use_keys) {
        subdir_footer_size = buf[0];
        spec_size = 3;
    }
    // make sure buf len is enough
    if (subdir_footer_size > KND_NAME_SIZE) return knd_LIMIT;

    if (DEBUG_SHARED_SET_GSP_LEVEL_2)
        knd_log("== cell size:%zu footer size:%zu", cell_size, subdir_footer_size);

    subdir_area_size = parent_block_size - (subdir_footer_size + spec_size);

    lseek(fd, offset + subdir_area_size, SEEK_SET);
    num_bytes = read(fd, buf, subdir_footer_size);
    if (num_bytes != (ssize_t)subdir_footer_size) return knd_IO_FAIL;

    if (use_keys) {
        // TODO: subdir linear traversal
        return knd_OK;
    }
    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        c = (unsigned char*)buf + (i * cell_size);
        numval = knd_unpack_int(c, cell_size);
        if (numval == 0) continue;
        idbuf[idbuf_size] = obj_id_seq[i];

        err = knd_shared_set_dir_new(self->mempool, &subdir);
        KND_TASK_ERR("failed to alloc a set subdir");
        subdir->total_size = numval;
        subdir->global_offset = offset + block_offset;

        err = unmarshall_block(self, subdir, fd, numval, idbuf, idbuf_size + 1, cb, task);
        KND_TASK_ERR("failed to unmarshall subdir block %.*s", idbuf_size + 1, idbuf);

        dir->subdirs[i] = subdir;
        dir->total_elems += subdir->total_elems;
        block_offset += numval;
    }
    return knd_OK;
}

static int unmarshall_block(struct kndSharedSet *self, struct kndSharedSetDir *dir,
                            int fd, size_t block_size, char *idbuf, size_t idbuf_size,
                            elem_unmarshall_cb cb, struct kndTask *task)
{
    struct kndRepo *repo = task->repo;
    struct kndMemBlock *block;
    ssize_t num_bytes;
    size_t subdir_block_size;
    int err;
    if (DEBUG_SHARED_SET_GSP_LEVEL_2)
        knd_log(".. unmarshall block from offset %zu (size: %zu)", dir->global_offset, block_size);

    err = read_payload_size(dir, fd, dir->global_offset, block_size);
    KND_TASK_ERR("failed to read payload size");

    if (dir->payload_block_size) {
        if (DEBUG_SHARED_SET_GSP_LEVEL_2)
            knd_log("== alloc a payload block of size %zu", dir->payload_block_size);
        block = calloc(1, sizeof(struct kndMemBlock));
        if (!block) {
            err = knd_NOMEM;
            KND_TASK_ERR("block alloc failed");
        }
        block->buf_size = dir->payload_block_size + 1;

        char *b = malloc(block->buf_size);
        if (!b) {
            err = knd_NOMEM;
            KND_TASK_ERR("file memblock alloc failed");
        }
        b[dir->payload_block_size] = '\0';
        block->buf = b;

        lseek(fd, dir->global_offset, SEEK_SET);
        num_bytes = read(fd, b, dir->payload_block_size);
        if (num_bytes != (ssize_t)dir->payload_block_size) return knd_IO_FAIL;

        block->next = repo->blocks;
        repo->blocks = block;
        repo->num_blocks++;
        repo->total_block_size += block->buf_size;

        err = unmarshall_elems(dir, block->buf, dir->payload_block_size, idbuf, idbuf_size, cb, task);
        KND_TASK_ERR("failed to unmarshall elems");
        dir->total_elems = dir->num_elems;
    }
    subdir_block_size = block_size - (dir->payload_block_size + dir->payload_footer_size);
    if (subdir_block_size) {
        err = read_subdirs(self, dir, fd, dir->global_offset + dir->payload_block_size, subdir_block_size,
                           idbuf, idbuf_size, cb, task);
        KND_TASK_ERR("failed to unmarshall subdirs");
    }
    return knd_OK;
}

int knd_shared_set_unmarshall_file(struct kndSharedSet *self, const char *filename, size_t filename_size,
                                   size_t filesize, elem_unmarshall_cb cb, struct kndTask *task)
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
    memcpy(self->path, filename, filename_size);
    self->path_size = filename_size;

    err = knd_shared_set_dir_new(self->mempool, &dir);
    if (err) {
        KND_TASK_LOG("failed to alloc a set dir");
        goto final;
    }
    dir->total_size = filesize;
    dir->global_offset = offset;

    err = unmarshall_block(self, dir, fd, filesize - offset, idbuf, idbuf_size, cb, task);
    if (err) goto final;
    self->dir = dir;
    self->num_elems = dir->total_elems;

 final:
    close(fd);
    return err;
}

static int read_elem(struct kndSharedSet *self, int fd, struct kndSharedSetDir *dir,
                     const char *id, size_t id_size, elem_unmarshall_cb cb, void **result, struct kndTask *task)
{
    struct kndSharedSetDir *subdir;
    struct kndMemBlock *block;
    char *b;
    ssize_t num_bytes;
    int idx_pos;
    size_t elem_block_size, elem_offset = 0;
    int err;

    if (DEBUG_SHARED_SET_GSP_LEVEL_2)
        knd_log(".. read elem (id remainder: \"%.*s\")", id_size, id);

    assert(dir != NULL);

    idx_pos = obj_id_base[(unsigned char)*id];
    if (idx_pos == -1) {
        knd_log("-- invalid elem id");
        return knd_FORMAT;
    }

    if (id_size > 1) {
        subdir = atomic_load_explicit(&dir->subdirs[idx_pos], memory_order_relaxed);
        if (!subdir) return knd_NO_MATCH;

        err = read_elem(self, fd, subdir, id + 1, id_size - 1, cb, result, task);
        if (err) return err;
        return knd_OK;
    }

    if (dir->elems_linear_scan) {
        if (DEBUG_SHARED_SET_GSP_LEVEL_3)
            knd_log(".. elem block linear scan from %zu (payload block size:%zu)",
                    dir->global_offset, dir->payload_block_size);

        elem_block_size = dir->payload_block_size;
    } else {
        for (size_t i = 0; i < KND_RADIX_BASE; i++) {
            elem_block_size = dir->elem_block_sizes[i];
            if (i == (size_t)idx_pos) break;
            elem_offset += elem_block_size;
        }
        if (DEBUG_SHARED_SET_GSP_LEVEL_3)
            knd_log("== elem block offset: %zu  size:%zu", elem_offset, elem_block_size);
    }

    /* alloc and read a payload block */
    block = calloc(1, sizeof(struct kndMemBlock));
    if (!block) {
        err = knd_NOMEM;
        KND_TASK_ERR("block alloc failed");
    }
    block->buf_size = elem_block_size + 1;
    b = malloc(block->buf_size);
    if (!b) {
        err = knd_NOMEM;
        KND_TASK_ERR("file memblock alloc failed");
    }
    b[elem_block_size] = '\0';
    block->buf = b;
    lseek(fd, dir->global_offset + elem_offset, SEEK_SET);
    num_bytes = read(fd, b, elem_block_size);
    if (num_bytes != (ssize_t)elem_block_size) return knd_IO_FAIL;
    
    if (DEBUG_SHARED_SET_GSP_LEVEL_3)
        knd_log("PAYLOAD BLOCK: %s (size: %zu)", b, elem_block_size);

    if (dir->elems_linear_scan) {
        err = fetch_elem_linear_scan(b, elem_block_size, id, id_size, cb, result, task);
        KND_TASK_ERR("failed to fetch elem \"%.*s\"", id_size, id);
        return knd_OK;
    }

    err = cb(id, id_size, b, elem_block_size, result, task);
    KND_TASK_ERR("failed to unmarshall elem \"%.*s\"", id_size, id);
    return knd_OK;
}

int knd_shared_set_unmarshall_elem(struct kndSharedSet *self, const char *id, size_t id_size,
                                   elem_unmarshall_cb cb, void **result, struct kndTask *task)
{
    int fd;
    knd_task_spec_type orig_task_type = task->type;
    int err;

    if (DEBUG_SHARED_SET_GSP_LEVEL_2)
        knd_log(".. unmarshall elem \"%.*s\" from GSP \"%s\"", id_size, id, self->path);

    // TODO: check cache

    fd = open(self->path, O_RDONLY);
    if (fd == -1) {
        err = knd_IO_FAIL;
        KND_TASK_ERR("failed to open file %s", self->path);
    }

    task->type = KND_UNFREEZE_STATE;
    err = read_elem(self, fd, self->dir, id, id_size, cb, result, task);
    if (err) {
        KND_TASK_LOG("failed to read GSP elem %.*s", id_size, id);
    }
    close(fd);
    task->type = orig_task_type;
    return err;
}

