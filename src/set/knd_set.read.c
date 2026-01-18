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
    struct kndSetDirBlock *block;
    int err;

    err = knd_set_dir_block_new(&block, task->mempool);
    KND_TASK_ERR("failed to alloc a set dir block");

    block->offset = offset;
    block->size = block_size;

    block->next = dir->blocks;
    dir->num_blocks++;
    dir->blocks = block;

    // TODO set range
    block->to_dir = KND_RADIX_BASE;
    block->to_elem = KND_RADIX_BASE;

    //if (range) {
    //}
    
    *result = block;
    return knd_OK;
}

static int elems_linear_scan(struct kndSetDir *dir, struct kndSetDirBlock *block,
                             const char *rec, size_t rec_size,
                             knd_set_elem_unmarshall_cb_t cb, void *cb_ctx, struct kndTask *task)
{
    const char *b, *c;
    const char *key = rec;
    size_t remainder_size = rec_size - 1;
    size_t val_size;
    struct kndSetElem *elem;
    void *result;
    size_t result_size;
    int err;

    if (DEBUG_SET_READ_LEVEL_2) {
        knd_log(".. linear parsing of {rec %.*s {size %zu}}", rec_size, rec, rec_size);
    }

    /* skip first key */
    c = rec + 1;
    b = c;

    while (remainder_size) {
        switch (*c) {
        case '\0':
            val_size = c - b;

            err = knd_set_elem_new(&elem, task->mempool);
            KND_TASK_ERR("failed to alloc a set elem");
            memcpy(elem->id, dir->id, dir->id_size);
            elem->id_size = dir->id_size;
            elem->id[elem->id_size] = *key;
            elem->id_size++;

            if (cb) {
                err = cb(elem->id, elem->id_size, b, val_size, cb_ctx, &rec_size, &result, task);
                KND_TASK_ERR("failed to unmarshall {elem %.*s}", elem->id_size, elem->id);
            }
            block->num_elems++;

            key = c++;

            remainder_size -= 2;
            // in_tag = true;
            b = c;
            continue;
        default:
            c++;
            remainder_size--;
        }
    }

    val_size = c - b;
    if (cb) {
        err = cb(elem->id, elem->id_size, b, val_size, cb_ctx, &result_size, &result, task);
        KND_TASK_ERR("failed to unmarshall {elem %.*s}", elem->id_size, elem->id);

        elem->size = result_size;
    }
    block->num_elems++;
    return knd_OK;
}

static int fetch_elem_linear_scan(const char *id, size_t id_size,
                                  const char *rec, size_t rec_size,
                                  knd_set_elem_unmarshall_cb_t cb,
                                  void *ctx, void **result, struct kndTask *task)
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

static int unmarshall_elems(struct kndSetDir *dir, struct kndSetDirBlock *block,
                            char *rec, size_t rec_size,
                            knd_set_elem_unmarshall_cb_t cb, void *cb_ctx,
                            struct kndTask *task)
{
    struct kndSetElem *elem;
    unsigned char spec;
    bool use_keys = false;
    size_t footer_size = 0;
    size_t cell_size = 0;
    size_t dir_size = 0;
    size_t dir_field_size = 0;
    char *b, *e;
    const char *key;
    const unsigned char *c;
    char separ;
    size_t numval;
    size_t remainder;
    size_t elem_id_val = 0;
    size_t num_term_elems = 0;
    void *result;
    size_t result_size;
    int err;

    if (DEBUG_SET_READ_LEVEL_2) {
        knd_log(".. unmarshall elems of {dir %.*s}", dir->id_size, dir->id);
    }
    // use keys
    spec = rec[rec_size - 1];
    footer_size++;
    if (spec) use_keys = true;

    // read cell size
    footer_size++;
    if (rec_size < footer_size) return knd_LIMIT;
    spec = rec[rec_size - footer_size];
    if (spec > KND_UINT_SIZE) return knd_LIMIT;

    cell_size = spec;

    if (use_keys && cell_size > 0) {
        footer_size++;
        spec = rec[rec_size - footer_size];
        num_term_elems = spec;
        if (num_term_elems > KND_RADIX_BASE)
            return knd_LIMIT;
    }

    if (DEBUG_SET_READ_LEVEL_2) {
        knd_log("== {dir %.*s} {use-keys %d}  {cell-size %zu}  {num-elems %zu}",
                dir->id_size, dir->id, use_keys, cell_size, num_term_elems);
    }

    /* linear scan of explicitly separated recs */
    if (use_keys && cell_size == 0) {
        err = elems_linear_scan(dir, block, rec, rec_size - footer_size, cb, cb_ctx, task);
        KND_TASK_ERR("failed to scan payload");
        //block->elems_linear_scan = true;
        return knd_OK;
    }

    /* iterate over directory */
    dir_field_size = cell_size;
    if (use_keys)
        dir_field_size = cell_size + 1; // add length of elem id
        
    dir_size = KND_RADIX_BASE * dir_field_size;
    if (use_keys) {
        dir_size = num_term_elems * dir_field_size;
    }
    if (rec_size < (dir_size + footer_size)) return knd_LIMIT;

    remainder = rec_size - (dir_size + footer_size);
    b = rec + remainder;
    e = rec;

    for (size_t i = 0; i < num_term_elems; i++) {
        c = (unsigned char*)b + (i * dir_field_size);

        err = knd_set_elem_new(&elem, task->mempool);
        KND_TASK_ERR("failed to alloc a set elem");
        memcpy(elem->id, dir->id, dir->id_size);
        elem->id_size = dir->id_size;

        if (use_keys) {
            numval = knd_unpack_int(c + 1, cell_size);  // skip over subdir's id
            elem->id[elem->id_size] = *c;
            elem->id_size++;

            elem_id_val = obj_id_base[*c];

            if (DEBUG_SET_READ_LEVEL_2) {
                knd_log("%zu of %zu: {elem %.*s} {numval %zu} {rec %.*s}",
                        i, num_term_elems, elem->id_size, elem->id, numval, numval, e);
            }
        } else {
            numval = knd_unpack_int(c, cell_size);
            key = &obj_id_seq[i];
            elem_id_val = i;

            elem->id[elem->id_size] = *key;
            elem->id_size++;
        }
        if (numval == 0) continue;
        if (numval > remainder) return knd_LIMIT;

        //dir->idx->elem_block_sizes[elem_id_val] = numval;
        elem->size = numval;

        // TODO null-terminated string for GSL parsing
        separ = e[numval];
        e[numval] = '\0';
        
        /* activate callback function */
        if (cb) {
            err = cb(elem->id, elem->id_size, e, numval, cb_ctx, &result_size, &result, task);
            KND_TASK_ERR("failed to unmarshall {elem %.*s}", elem->id_size, elem->id);
        }

        //dir->num_term_elems++;

        // restore value
        e[numval] = separ;

        e += numval;
        remainder -= numval;
    }
    return knd_OK;
}

static int read_elems_rec_size(struct kndSetDirBlock *block, int fd)
{
    unsigned char buf[KND_NAME_SIZE];
    char size_spec = 0;
    ssize_t num_bytes;
    size_t numval;
    size_t footer_size = 0;

    if (DEBUG_SET_READ_LEVEL_2) {
        knd_log(".. reading elems rec size from offset %zu", block->offset + block->size - 1);
    }

    /* the last byte denotes an offset size */
    lseek(fd, block->offset + block->size - 1, SEEK_SET);
    num_bytes = read(fd, &size_spec, 1);
    if (num_bytes != 1) return knd_IO_FAIL;
    footer_size = 1;

    if (size_spec == 0) {
        if (DEBUG_SET_READ_LEVEL_2) {
            knd_log("-- no payload in this block");
        }
        block->elems_rec_size = 0;
        block->elems_footer_size = footer_size;
        return knd_OK;
    }
    if (size_spec > KND_UINT_SIZE) return knd_LIMIT;

    footer_size += size_spec;
    lseek(fd, block->offset + block->size - footer_size, SEEK_SET);
    num_bytes = read(fd, &buf, (size_t)size_spec);
    if (num_bytes != (ssize_t)size_spec) return knd_IO_FAIL;
    numval = knd_unpack_int(buf, size_spec);
    if (numval > (block->size - footer_size)) return knd_LIMIT;

    if (DEBUG_SET_READ_LEVEL_TMP) {
        knd_log("== {elems {rec-size %zu} {footer-size %zu}}", numval, footer_size);
    }

    block->elems_rec_size = numval;
    block->elems_footer_size = footer_size;
    return knd_OK;
}

static int read_subdirs(struct kndSetDir *dir, struct kndSetDirBlock *block, struct kndSetRange *range,
                        int fd, knd_set_elem_unmarshall_cb_t cb, void *cb_ctx, struct kndTask *task)
{
    unsigned char buf[KND_NAME_SIZE];
    struct kndSetDir *subdir;
    struct kndSetDirBlock *subdir_block;
    size_t num_subdirs = KND_RADIX_BASE;
    size_t spec_size = 2;
    ssize_t num_bytes;
    size_t subdir_area_size;
    size_t subdir_footer_size;
    bool use_keys = false;
    size_t cell_size;
    const unsigned char *c;
    size_t block_offset = 0;
    size_t elem_id_val = 0;
    size_t offset = block->offset + block->elems_rec_size;
    size_t subdir_block_size;
    int err;

    if (DEBUG_SET_READ_LEVEL_2) {
        knd_log(".. reading subdirs of {dir %.*s {offset %zu + %zu}}",
                dir->id_size, dir->id, offset, block->size);
    }

    // GSP_SUBDIRS_SPEC_SIZE
    // the last three bytes contain a dir size spec:
    // num subdirs (if use_keys)  cell-size (1-4)   use-keys (0/1)
    // [2]                        [1]               [0]

    lseek(fd, offset + block->subdirs_rec_size - GSP_SUBDIRS_SPEC_SIZE, SEEK_SET);
    num_bytes = read(fd, buf, GSP_SUBDIRS_SPEC_SIZE);
    if (num_bytes != GSP_SUBDIRS_SPEC_SIZE) return knd_IO_FAIL;

    if (DEBUG_SET_READ_LEVEL_2) {
        knd_log(">> cell size:%d use keys: %d", buf[1], buf[2]);
    }

    if (buf[2]) use_keys = true;
    if (buf[1] > KND_UINT_SIZE || buf[1] == 0) return knd_LIMIT;
    cell_size = buf[1];

    subdir_footer_size = KND_RADIX_BASE * cell_size;
    if (use_keys) {
        num_subdirs = buf[0];
        subdir_footer_size = (num_subdirs * cell_size) + num_subdirs;
        spec_size = 3;
    }
    // make sure buf len is enough
    if (subdir_footer_size > KND_NAME_SIZE) return knd_LIMIT;

    if (DEBUG_SET_READ_LEVEL_2) {
        knd_log("== cell size:%zu footer} size:%zu", cell_size, subdir_footer_size);
    }

    subdir_area_size = block->subdirs_rec_size - (subdir_footer_size + spec_size);

    lseek(fd, offset + subdir_area_size, SEEK_SET);
    num_bytes = read(fd, buf, subdir_footer_size);
    if (num_bytes != (ssize_t)subdir_footer_size) return knd_IO_FAIL;

    if (use_keys) {
        if (DEBUG_SET_READ_LEVEL_2) {
            knd_log(">> {dir %.*s} subdir linear traversal needed {cell-size %zu} {footer-size %zu}",
                    dir->id_size, dir->id, cell_size, subdir_footer_size);
        }
        for (size_t i = 0; i < num_subdirs; i++) {
            c = (unsigned char*)buf + (i * (1 + cell_size));
            subdir_block_size = knd_unpack_int(c + 1, cell_size); // skip over subdir's id
            if (subdir_block_size == 0) continue;

            err = knd_set_dir_new(&subdir, dir->id, dir->id_size, (const char *)c, task->mempool);
            KND_TASK_ERR("failed to alloc a set subdir");

            err = create_dir_block(subdir, range, offset + block_offset, subdir_block_size, &subdir_block, task);
            KND_TASK_ERR("failed to create a subdir block");

            err = unmarshall_block(subdir, range, fd, cb, cb_ctx, task);
            KND_TASK_ERR("failed to unmarshall {subdir %.*s}", subdir->id_size, subdir->id);

            elem_id_val = obj_id_base[*c];
            dir->subdirs[elem_id_val] = subdir;

            dir->total_elems += subdir->total_elems;
            block_offset += subdir_block_size;
        }
        return knd_OK;
    }

    /* all subdirs are present in a footer */
    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        c = (unsigned char*)buf + (i * cell_size);
        subdir_block_size = knd_unpack_int(c, cell_size);
        if (subdir_block_size == 0) continue;

        err = knd_set_dir_new(&subdir, dir->id, dir->id_size, &obj_id_seq[i], task->mempool);
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
    size_t buf_size;
    int err;

    if (DEBUG_SET_READ_LEVEL_TMP) {
        knd_log(">> {dir %.*s} to unmarshall its {rec {offset %zu} {size %zu}}",
                dir->id_size, dir->id, block->offset, block->size);
    }

    err = read_elems_rec_size(block, fd);
    KND_TASK_ERR("failed to read elems rec size");

    if (block->elems_rec_size) {
        buf_size = block->elems_rec_size;

        file_out->reset(file_out);
        if (buf_size >= file_out->capacity) {
            KND_TASK_LOG("payload block size %zu exceeds a file input limit of %zu",
                         buf_size, file_out->capacity);
            return knd_LIMIT;
        }
        if (DEBUG_SET_READ_LEVEL_3) {
            knd_log(".. reading GSP elems rec of size %zu", buf_size);
        }
        buf = file_out->buf;

        lseek(fd, block->offset, SEEK_SET);
        num_bytes = read(fd, buf, buf_size);
        if (num_bytes != (ssize_t)buf_size) {
            return knd_IO_FAIL;
        }

        err = unmarshall_elems(dir, block, buf, buf_size, cb, cb_ctx, task);
        if (err) {
            free(buf);
            KND_TASK_ERR("failed to unmarshall elems");
        }
        dir->total_elems += block->num_elems;
    }

    block->subdirs_rec_size = block->size - (block->elems_rec_size + block->elems_footer_size);
    if (block->subdirs_rec_size) {
        err = read_subdirs(dir, block, range, fd, cb, cb_ctx, task);
        KND_TASK_ERR("failed to unmarshall subdirs");
    }
    return knd_OK;
}

int knd_set_read_leaf(struct kndSet *s, struct kndStorageLeaf *leaf,
                      struct kndSetRange *range,
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

    if (DEBUG_SET_READ_LEVEL_TMP) {
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

    if (!s->dir) {
        err = knd_set_dir_new(&dir, "", 0, "", s->mempool);
        if (err) {
            KND_TASK_LOG("failed to alloc a set dir");
            goto final;
        }
        s->dir = dir;

        err = create_dir_block(dir, range, offset, st.st_size - offset, &block, task);
        KND_TASK_ERR("failed to create a dir block");        
    }

    err = unmarshall_block(dir, range, fd, cb, cb_ctx, task);
    if (err) goto final;

    s->num_elems = dir->total_elems;
 
 final:
    close(fd);
    return err;
}
