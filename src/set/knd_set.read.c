#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "knd_class.h"
#include "knd_utils.h"
#include "knd_memblock.h"
#include "knd_mempool.h"
#include "knd_set.h"
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

static int unmarshall_block(struct kndSetDir *dir, struct kndSetRange *range, int fd,
                            knd_set_elem_unmarshall_cb_t cb, void *cb_ctx, struct kndTask *task);

static int create_dir_block(struct kndSetDir *dir, struct kndSetRange *unused_var(range),
                            size_t offset, size_t block_size,
                            struct kndSetDirBlock **result, struct kndTask *task)
{
    struct kndMemPool *mempool = task->cache.mempool;
    struct kndSetDirBlock *block;
    int err;

    err = knd_set_dir_block_new(&block, mempool);
    KND_TASK_ERR("failed to alloc a set dir block");

    block->offset = offset;
    block->size = block_size;

    block->next = dir->blocks;
    //dir->num_blocks++;
    dir->blocks = block;

    // TODO set range
    block->to_dir = KND_RADIX_BASE;
    block->to_elem = KND_RADIX_BASE;

    //if (range) {
    //}
    
    *result = block;
    return knd_OK;
}

static int read_elems_rec_size(struct kndSetDirBlock *block, const char *rec, size_t rec_size,
                               size_t *result_tail_size, struct kndTask *task)
{
    char size_spec = 0;
    size_t numval;
    size_t tail_size = 1;
    int err;

    if (DEBUG_SET_READ_LEVEL_2) {
        knd_log(".. reading elems block size specs");
    }

    size_spec = rec[rec_size - tail_size];
    if (size_spec == 0) {
        if (DEBUG_SET_READ_LEVEL_3) {
            knd_log("-- no payload in this block");
        }
        *result_tail_size = tail_size;
        return knd_OK;
    }
    if (size_spec > KND_UINT_SIZE) return knd_LIMIT;

    /* elems footer size */
    tail_size += size_spec;
    if (rec_size < tail_size) return knd_LIMIT;
    numval = knd_unpack_int((const unsigned char*)rec + (rec_size - tail_size), size_spec);
    if (numval > (block->size - tail_size)) return knd_LIMIT;
    block->elems_footer_size = numval;

    if (DEBUG_SET_READ_LEVEL_2) {
        knd_log("== {elems-footer-size %zu {byte-size-spec %d}}",
                block->elems_footer_size, size_spec);
    }

    /* elems rec size */
    tail_size++;
    size_spec = rec[rec_size - tail_size];
    if (size_spec == 0) {
        if (DEBUG_SET_READ_LEVEL_3) {
            knd_log("-- no payload in this block");
        }
        *result_tail_size = tail_size;
        return knd_OK;
    }
    if (size_spec > KND_UINT_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("{size-spec %d} exceeds uint limit", size_spec);
    }

    if (DEBUG_SET_READ_LEVEL_2) {
        knd_log("== {elems-recs {byte-size-spec %d}}", size_spec);
    }
    tail_size += size_spec;
    if (rec_size < tail_size) return knd_LIMIT;
    numval = knd_unpack_int((const unsigned char*)rec + (rec_size - tail_size), size_spec);
    if (numval > (block->size - tail_size)) return knd_LIMIT;
    block->elems_rec_size = numval;

    *result_tail_size = tail_size;

    if (DEBUG_SET_READ_LEVEL_3) {
        knd_log("== {elems {rec-size %zu} {footer-size %zu}}",
                block->elems_rec_size, block->elems_footer_size);
    }
    return knd_OK;
}

/*
        if (use_keys) {
            numval = knd_unpack_int(c + 1, cell_size);  // skip over subdir's id
            elem->id[elem->id_size] = *c;
            elem->id_size++;

            elem_id_val = obj_id_base[*c];

            if (DEBUG_SET_READ_LEVEL_TMP) {
                knd_log("%zu of %zu: {elem %.*s} {numval %zu} {rec %.*s}",
                        i, num_elems, elem->id_size, elem->id, numval, numval, e);
            }
        } else {
*/

static int elems_linear_scan(struct kndSetDir *dir, struct kndSetDirBlock *block,
                             char *rec, size_t rec_size, size_t num_elems, size_t cell_size,
                             knd_set_elem_unmarshall_cb_t cb, void *cb_ctx, struct kndTask *task)
{
    struct kndMemPool *mempool = task->cache.mempool;
    char *dir_entry;
    struct kndSetElem *elem;
    const char *elem_rec;
    size_t elem_rec_size;
    void *result;
    char separ;
    size_t parsed_rec_size;
    size_t rec_offset = 0;
    int err;

    if (DEBUG_SET_READ_LEVEL_2) {
        knd_log(".. linear scan of {rec %.*s {size %zu}}", rec_size, rec, rec_size);
    }

    dir_entry = rec + block->elems_rec_size;

    for (size_t i = 0; i < num_elems; i++) {
        elem_rec_size = knd_unpack_int((unsigned char*)dir_entry + 1, cell_size);
        if (elem_rec_size == 0) return knd_LIMIT;

        err = knd_set_elem_new(&elem, mempool);
        KND_TASK_ERR("failed to alloc a set elem");

        memcpy(elem->id, dir->id, dir->id_size);
        elem->id_size = dir->id_size;
        elem->id[elem->id_size] = *dir_entry;
        elem->id_size++;

        /* add null-sentinel for GSL parsing */
        separ = rec[rec_offset + elem_rec_size];
        rec[rec_offset + elem_rec_size] = '\0';
        elem_rec = rec + rec_offset;

        if (DEBUG_SET_READ_LEVEL_3) {
            knd_log(">> [%zu] %c {elem-rec %.*s {size %zu}}",
                    i, *dir_entry, elem_rec_size, elem_rec, elem_rec_size);
        }

        err = cb(elem->id, elem->id_size, elem_rec, elem_rec_size, cb_ctx,
                 &parsed_rec_size, &result, task);
        KND_TASK_ERR("failed to unmarshall {elem %.*s}", elem->id_size, elem->id);
        elem->val = result;

        // TODO assign elem

        rec[rec_offset + elem_rec_size] = separ;
        dir_entry += (cell_size + 1);
        rec_offset += elem_rec_size;
    }
    return knd_OK;
}

#if 0
static int fetch_elem_linear_scan(const char *id, size_t id_size, const char *rec, size_t rec_size,
                                  knd_set_elem_unmarshall_cb_t cb, void *ctx, void **result,
                                  struct kndTask *task)
{
    char curr_id;
    const char *b, *c;
    size_t remainder = rec_size - 1;
    size_t val_size;
    size_t result_size;
    int err;

    assert(cb != NULL);

    if (DEBUG_SET_READ_LEVEL_2) {
        knd_log(".. linear scan of {block {size %zu}} to fetch {elem %.*s}",
                rec_size, id_size, id);
    }
    curr_id = *rec;
    c = rec + 1;
    b = c;

    while (remainder) {
        switch (*c) {
        case '\0':
            val_size = c - b;
            if (curr_id == *id) {
                err = cb(id, id_size, b, val_size, ctx, &result_size, result, task);
                KND_TASK_ERR("failed to unmarshall {elem %.*s}", id_size, id);
                return knd_OK;
            }
            c++;
            curr_id = *c++;
            if (curr_id > *id) return knd_NO_MATCH;

            remainder -= 2;
            b = c;
            continue;
        default:
            c++;
            remainder--;
        }
    }
    val_size = c - b;
    if (curr_id == *id) {
        err = cb(id, id_size, b, val_size, ctx, &result_size, result, task);
        KND_TASK_ERR("failed to unmarshall elem \"%.*s\"", id_size, id);
        return knd_OK;
    }
    return knd_NO_MATCH;
}
#endif

static int unmarshall_elems(struct kndSetDir *dir, struct kndSetDirBlock *block,
                            char *rec, size_t rec_size,
                            knd_set_elem_unmarshall_cb_t cb, void *cb_ctx,
                            struct kndTask *task)
{
    struct kndMemPool *mempool = task->cache.mempool;
    struct kndSetElem *elem;
    unsigned char spec;
    bool use_keys = false;
    size_t cell_size = 0;
    char *dir_entry;
    char *elem_rec;
    const char *key;
    char separ;
    size_t numval;
    size_t remainder;
    size_t num_elems;
    size_t curr_num_elems = 0;
    void *result;
    size_t tail_size = 0;
    size_t parsed_rec_size = 0;
    int err;

    assert (cb != NULL);

    if (DEBUG_SET_READ_LEVEL_2) {
        knd_log(".. unmarshall elems of {dir %.*s} {rec %.*s {size %zu}}",
                dir->id_size, dir->id, rec_size, rec, rec_size);
    }

    err = read_elems_rec_size(block, rec, rec_size, &tail_size, task);
    KND_TASK_ERR("failed to read subdirs rec size");

    if (!block->elems_rec_size) return knd_OK;

    /* fixed length fields or marked keys? */
    tail_size++;
    if (rec_size < tail_size) return knd_LIMIT;
    spec = rec[rec_size - tail_size];
    if (!(spec == 0 || spec == 1)) {
        err = knd_FORMAT;
        KND_TASK_ERR("incorrect value for elems {use-keys %d}", spec);
    }
    if (spec) use_keys = true;

    /* cell size */
    tail_size++;
    if (rec_size < tail_size) return knd_LIMIT;
    spec = rec[rec_size - tail_size];
    if (spec == 0) {
        err =  knd_LIMIT;
        KND_TASK_ERR("GSP footer byte-spec cannot be 0");
    }
    if (spec > KND_UINT_SIZE) {
        err =  knd_LIMIT;
        KND_TASK_ERR("GSP footer {byte-spec %d} exceeds uint size", spec);
    }
    cell_size = spec;

    /* num of elems */
    tail_size++;
    if (rec_size < tail_size) return knd_LIMIT;
    spec = rec[rec_size - tail_size];
    num_elems = spec;
    if (num_elems > KND_RADIX_BASE) {
        err = knd_LIMIT;
        KND_TASK_ERR("too many elems specified, incorrect elems footer");
    }

    if (DEBUG_SET_READ_LEVEL_3) {
        knd_log("== {dir %.*s} {use-keys %d}  {cell-size %zu}"
                " {num-elems %zu} {tail-size %zu}",
                dir->id_size, dir->id, use_keys, cell_size, num_elems, tail_size);
    }

    if (use_keys) {
        err = elems_linear_scan(dir, block, rec, rec_size, num_elems, cell_size, cb, cb_ctx, task);
        KND_TASK_ERR("failed to scan elems");
        return knd_OK;
    }

    /* iterate over directory */
    dir_entry = rec + block->elems_rec_size;
    elem_rec = rec;
    remainder = block->elems_rec_size;

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        numval = knd_unpack_int((unsigned char*)dir_entry, cell_size);
        if (numval == 0) {
            dir_entry += cell_size;
            continue;
        }
        if (numval > remainder) return knd_LIMIT;

        key = &obj_id_seq[i];

        err = knd_set_elem_new(&elem, mempool);
        KND_TASK_ERR("failed to alloc a set elem");
        memcpy(elem->id, dir->id, dir->id_size);
        elem->id_size = dir->id_size;
        elem->id[elem->id_size] = *key;
        elem->id_size++;
        elem->size = numval;
        curr_num_elems++;

        // TODO add null-sentinel for GSL parsing
        separ = elem_rec[numval];
        elem_rec[numval] = '\0';

        //knd_log("== {elem %.*s {rec %.*s} {size %zu}}",
        //        elem->id_size, elem->id, numval, elem_rec, elem->size);

        /* activate callback function */
        err = cb(elem->id, elem->id_size, elem_rec, numval, cb_ctx, &parsed_rec_size, &result, task);
        KND_TASK_ERR("failed to unmarshall {elem %.*s}", elem->id_size, elem->id);
        elem->val = result;

        dir->elems[i] = elem;
        
        // restore value
        elem_rec[numval] = separ;
        elem_rec += numval;
        remainder -= numval;
        dir_entry += cell_size;
    }
    return knd_OK;
}

static int read_subdirs_rec_size(struct kndSetDirBlock *block, int fd, size_t *elems_block_size)
{
    unsigned char buf[KND_NAME_SIZE];
    char size_spec = 0;
    ssize_t num_bytes;
    size_t numval;
    size_t offset = block->offset + block->size;
    size_t tail_size;

    if (DEBUG_SET_READ_LEVEL_2) {
        knd_log(".. reading subdirs rec size from {offset %zu}",
                block->offset + block->size - 1);
    }

    /* get num value size */
    tail_size = 1;
    lseek(fd, offset - tail_size, SEEK_SET);
    num_bytes = read(fd, &size_spec, 1);
    if (num_bytes != 1) return knd_IO_FAIL;

    /* no subdirs present, just elems */
    if (size_spec == 0) {
        if (DEBUG_SET_READ_LEVEL_3) {
            knd_log("-- no subdirs found");
        }
        block->subdirs_rec_size = 0;
        *elems_block_size = block->size - tail_size;
        return knd_OK;
    }
    if (size_spec > KND_UINT_SIZE) return knd_LIMIT;

    tail_size += size_spec;
    lseek(fd, offset - tail_size, SEEK_SET);
    num_bytes = read(fd, &buf, (size_t)size_spec);
    if (num_bytes != (ssize_t)size_spec) return knd_IO_FAIL;
    numval = knd_unpack_int(buf, size_spec);
    if (numval > (block->size - tail_size)) return knd_LIMIT;

    block->subdirs_footer_size = numval - tail_size;

    //knd_log(".. {subdirs-footer-size %zu}", block->subdirs_footer_size);

    /* now read the subdirs-rec-size */
    tail_size++;
    lseek(fd, offset - tail_size, SEEK_SET);
    num_bytes = read(fd, &size_spec, 1);
    if (num_bytes != 1) return knd_IO_FAIL;
    if (size_spec == 0) {
        return knd_LIMIT;
    }
    if (size_spec > KND_UINT_SIZE) return knd_LIMIT;

    tail_size += size_spec;
    lseek(fd, offset - tail_size, SEEK_SET);
    num_bytes = read(fd, &buf, (size_t)size_spec);
    if (num_bytes != (ssize_t)size_spec) return knd_IO_FAIL;
    numval = knd_unpack_int(buf, size_spec);
    if (numval > (block->size - tail_size)) return knd_LIMIT;

    block->subdirs_rec_size = numval;

    *elems_block_size = block->size -\
        (block->subdirs_rec_size + block->subdirs_footer_size + tail_size);

    if (DEBUG_SET_READ_LEVEL_3) {
        knd_log("== {subdirs-rec-size %zu} {subdirs-footer-size %zu}"
                " {tail-size %zu} {elems-block-size %zu}\n",
                block->subdirs_rec_size, block->subdirs_footer_size,
                tail_size, *elems_block_size);
    }
    return knd_OK;
}

static int read_subdirs(struct kndSetDir *dir, struct kndSetDirBlock *block,
                        size_t global_offset, struct kndSetRange *range,
                        int fd, knd_set_elem_unmarshall_cb_t cb, void *cb_ctx, struct kndTask *task)
{
    struct kndMemPool *mempool = task->cache.mempool;
    unsigned char buf[KND_NAME_SIZE];
    struct kndSetDir *subdir;
    struct kndSetDirBlock *subdir_block;
    size_t num_subdirs;
    ssize_t num_bytes;
    bool use_keys = false;
    size_t cell_size;
    const unsigned char *c;
    size_t block_offset = 0;
    size_t elem_id_val = 0;
    size_t subdir_block_size;
    size_t offset = global_offset + block->subdirs_rec_size + block->subdirs_footer_size;
    int err;

    if (DEBUG_SET_READ_LEVEL_2) {
        knd_log(".. reading subdirs of {dir %.*s {global-offset %zu {local-offset %zu}}"
                "{subdirs-rec-size %zu} {subdirs-footer-size %zu}",
                dir->id_size, dir->id, global_offset, offset,
                block->subdirs_rec_size, block->subdirs_footer_size);
    }

    // GSP_SUBDIRS_SPEC_SIZE
    // the last three bytes contain a dir size spec:
    // num subdirs                cell-size (1-4)   use-keys (0/1)
    // [2]                        [1]               [0]

    lseek(fd, offset - GSP_SUBDIRS_SPEC_SIZE, SEEK_SET);
    num_bytes = read(fd, buf, GSP_SUBDIRS_SPEC_SIZE);
    if (num_bytes != GSP_SUBDIRS_SPEC_SIZE) {
        err =  knd_IO_FAIL;
        KND_TASK_ERR("failed to read subdirs cell sizes");
    }
    if (buf[2]) use_keys = true;
    cell_size   = buf[1];
    num_subdirs = buf[0];

    if (DEBUG_SET_READ_LEVEL_3) {
        knd_log("== {cell-size %d} {use-keys %d} {num-subdirs %d} {read-from %zu}",
                cell_size, use_keys, num_subdirs,
                offset - GSP_SUBDIRS_SPEC_SIZE);
    }

    if (cell_size == 0) {
        err =  knd_LIMIT;
        KND_TASK_ERR("subdir cell-size cannot be zero");
    }
    if (cell_size > KND_UINT_SIZE) {
        err =  knd_LIMIT;
        KND_TASK_ERR("subdir {cell-size %zu} exceeds uint size limit", cell_size);
    }

    offset = global_offset + block->subdirs_rec_size;

    lseek(fd, offset, SEEK_SET);
    num_bytes = read(fd, buf, block->subdirs_footer_size);
    if (num_bytes != (ssize_t)block->subdirs_footer_size) return knd_IO_FAIL;

    if (use_keys) {
        if (DEBUG_SET_READ_LEVEL_3) {
            knd_log(">> {dir %.*s {num-subdirs %zu}} "
                    "linear traversal needed {cell-size %zu} {footer-size %zu}",
                    dir->id_size, dir->id, num_subdirs, cell_size, block->subdirs_footer_size);
        }
        c = (unsigned char*)buf;

        for (size_t i = 0; i < num_subdirs; i++) {
            subdir_block_size = knd_unpack_int(c + 1, cell_size);

            if (DEBUG_SET_READ_LEVEL_3) {
                knd_log(">> [%zu] {subdir %c {size %zu}}", i, *c, subdir_block_size);
            }

            if (subdir_block_size == 0) return knd_LIMIT;
            if (subdir_block_size > block->subdirs_rec_size) return knd_LIMIT;

            err = knd_set_dir_new(&subdir, dir->id, dir->id_size, (const char *)c, mempool);
            KND_TASK_ERR("failed to alloc a set subdir");

            err = create_dir_block(subdir, range, global_offset + block_offset,
                                   subdir_block_size, &subdir_block, task);
            KND_TASK_ERR("failed to create a subdir block");

            err = unmarshall_block(subdir, range, fd, cb, cb_ctx, task);
            KND_TASK_ERR("failed to unmarshall {subdir %.*s}", subdir->id_size, subdir->id);

            elem_id_val = obj_id_base[*c];
            dir->subdirs[elem_id_val] = subdir;

            dir->total_elems += subdir->total_elems;

            block_offset += subdir_block_size;
            c += (cell_size + 1);
        }
        return knd_OK;
    }

    /* iterate over a fixed size footer */
    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        c = (unsigned char*)buf + (i * cell_size);
        subdir_block_size = knd_unpack_int(c, cell_size);
        if (subdir_block_size == 0) continue;

        err = knd_set_dir_new(&subdir, dir->id, dir->id_size, &obj_id_seq[i], mempool);
        KND_TASK_ERR("failed to alloc a set subdir");

        err = create_dir_block(subdir, range, offset + block_offset, subdir_block_size, &block, task);
        KND_TASK_ERR("failed to create a dir block");

        err = unmarshall_block(subdir, range, fd, cb, cb_ctx, task);
        KND_TASK_ERR("failed to unmarshall {subdir %.*s}", subdir->id_size, subdir->id);

        dir->subdirs[i] = subdir;
        dir->total_elems += subdir->total_elems;

        block_offset += subdir_block_size;
    }
    return knd_OK;
}

static int unmarshall_block(struct kndSetDir *dir, struct kndSetRange *range, int fd,
                            knd_set_elem_unmarshall_cb_t cb, void *cb_ctx, struct kndTask *task)
{
    struct kndOutput *file_out = task->file_out;
    ssize_t num_bytes;
    struct kndSetDirBlock *block = dir->blocks;
    char *buf;
    size_t elems_block_size = 0;
    int err;

    assert (block != NULL);

    if (DEBUG_SET_READ_LEVEL_2) {
        knd_log("\n.. {dir %.*s} to unmarshall its {block {offset %zu} {size %zu}}",
                dir->id_size, dir->id, block->offset, block->size);
    }

    err = read_subdirs_rec_size(block, fd, &elems_block_size);
    KND_TASK_ERR("failed to read subdirs rec size");

    if (DEBUG_SET_READ_LEVEL_3) {
        knd_log("== {subdirs {rec-size %zu} {elems {block-size %zu {rec-size %zu}}",
                block->subdirs_rec_size, elems_block_size, block->elems_rec_size);
    }

    /* start from elems */
    if (elems_block_size) {
        if (elems_block_size >= file_out->capacity) {
            KND_TASK_LOG("payload block size %zu exceeds a file input limit of %zu",
                         elems_block_size, file_out->capacity);
            return knd_LIMIT;
        }

        if (DEBUG_SET_READ_LEVEL_3) {
            knd_log(".. reading GSP {elems-block {size %zu}}", elems_block_size);
        }

        file_out->reset(file_out);
        buf = file_out->buf;
        lseek(fd, block->offset, SEEK_SET);
        num_bytes = read(fd, buf, elems_block_size);
        if (num_bytes != (ssize_t)elems_block_size) {
            err = knd_IO_FAIL;
            KND_TASK_ERR("failed to read elems rec from GSP file");
        }

        err = unmarshall_elems(dir, block, buf, elems_block_size, cb, cb_ctx, task);        
        KND_TASK_ERR("failed to unmarshall elems");

        dir->total_elems += block->num_elems;
    }

    if (block->subdirs_rec_size) {
        err = read_subdirs(dir, block, block->offset + elems_block_size,
                           range, fd, cb, cb_ctx, task);
        KND_TASK_ERR("failed to unmarshall subdirs");
    }
    return knd_OK;
}

int knd_set_read_leaf(struct kndSet *s, struct kndStorageLeaf *leaf, struct kndSetRange *range,
                      knd_set_elem_unmarshall_cb_t cb, void *cb_ctx, struct kndTask *task)
{
    struct stat st;
    struct kndSetDir *dir = s->dir;
    const char *filename = leaf->filepath;
    size_t filename_size = leaf->filepath_size;
    struct kndSetDirBlock *block;
    // TODO: check header
    size_t offset = strlen("GSP");
    int fd;
    int err;

    assert (filename_size != 0);

    if (DEBUG_SET_READ_LEVEL_2) {
        knd_log(".. open storage {leaf %.*s {filepath %.*s} {size %zu}}",
                leaf->name_size, leaf->name, filename_size, filename, leaf->curr_size);
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

    err = create_dir_block(dir, range, offset, st.st_size - offset, &block, task);
    KND_TASK_ERR("failed to create a dir block");

    err = unmarshall_block(dir, range, fd, cb, cb_ctx, task);
    if (err) goto final;

    s->num_elems = dir->total_elems;
 
 final:
    close(fd);
    return err;
}
