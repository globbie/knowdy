#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

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

static int fetch_elem(struct kndSet *s, int fd, size_t block_offset, size_t block_size,
                      const char *key, size_t key_size,
                      knd_set_elem_unmarshall_cb_t cb, void *cb_ctx,
                      void **result, struct kndTask *task);

static int decode_elem(struct kndSet *s, int fd, size_t block_offset, size_t block_size,
                       const char *key, size_t key_size,
                       knd_set_elem_unmarshall_cb_t cb, void *cb_ctx,
                       void **result, struct kndTask *task)
{
    struct kndOutput *file_out = task->file_out;
    char *buf;
    size_t parsed_rec_size;
    ssize_t num_bytes;
    int err;

    /* activate callback function */
    switch (s->format_t) {
    case KND_SET_ELEM_STR:
        if (block_size >= file_out->capacity) return knd_LIMIT;

        /* read rec to file buf */
        file_out->reset(file_out);
        buf = file_out->buf;
        lseek(fd, block_offset, SEEK_SET);
        num_bytes = read(fd, buf, block_size);
        if (num_bytes != (ssize_t)block_size) {
            err = knd_IO_FAIL;
            KND_TASK_ERR("failed to read elem rec from GSP file: num of bytes mismatch");
        }
        buf[block_size] = '\0';

        err = cb(key, key_size, buf, block_size, cb_ctx, &parsed_rec_size, result, task);
        KND_TASK_ERR("failed to unmarshall {elem %.*s}", key_size, key);

        return knd_OK;
    case KND_SET_ELEM_BIN:
        // pass fd to read from
        knd_log("-- not implemented");
        return knd_NO_MATCH;
    default:
        break;
    }

    return knd_NO_MATCH;
}

static int read_elems_idx_spec(int fd, size_t block_offset, size_t block_size,
                               size_t *elems_rec_size, size_t *elems_idx_size)
{
    unsigned char buf[KND_NAME_SIZE];
    unsigned char size_spec = 0;
    ssize_t num_bytes;
    size_t numval;
    size_t offset = block_offset + block_size;
    size_t tail_size;
    size_t footer_size;

    /* get num value size */
    tail_size = 1;
    lseek(fd, offset - tail_size, SEEK_SET);
    num_bytes = read(fd, &size_spec, 1);
    if (num_bytes != 1) return knd_IO_FAIL;
    if (size_spec == 0) return knd_LIMIT;
    if (size_spec > KND_UINT_SIZE) return knd_LIMIT;

    tail_size += size_spec;

    lseek(fd, offset - tail_size, SEEK_SET);
    num_bytes = read(fd, &buf, (size_t)size_spec);
    if (num_bytes != (ssize_t)size_spec) return knd_IO_FAIL;
    footer_size = knd_unpack_int(buf, size_spec);

    if (footer_size > (block_size - tail_size)) return knd_LIMIT;

    /* read the elems rec size */
    tail_size++;
    lseek(fd, offset - tail_size, SEEK_SET);
    num_bytes = read(fd, &size_spec, 1);
    if (num_bytes != 1) return knd_IO_FAIL;
    if (size_spec == 0) return knd_LIMIT;
    if (size_spec > KND_UINT_SIZE) return knd_LIMIT;

    tail_size += size_spec;
    lseek(fd, offset - tail_size, SEEK_SET);
    num_bytes = read(fd, &buf, (size_t)size_spec);
    if (num_bytes != (ssize_t)size_spec) return knd_IO_FAIL;
    numval = knd_unpack_int(buf, size_spec);
    if (numval > (block_size - tail_size)) return knd_LIMIT;

    *elems_rec_size = numval;
    *elems_idx_size = footer_size - (size_spec + 1);

    if (DEBUG_SET_FETCH_LEVEL_3) {
        knd_log("== {elems-rec-size %zu} {elems-idx-size %zu}",
                *elems_rec_size, *elems_idx_size);
    }
    return knd_OK;
}

static int iterate_elems(struct kndSet *s, int fd, size_t block_offset, size_t block_size, const char *key,
                         knd_set_elem_unmarshall_cb_t cb, void *cb_ctx,
                         void **result, struct kndTask *task)
{
    unsigned char buf[KND_NAME_SIZE];
    bool use_keys = false;
    size_t cell_size = 0;
    const unsigned char *c;
    size_t num_elems;
    size_t elems_rec_size;
    size_t elems_idx_size;
    size_t offset;
    size_t elems_offset = 0;
    ssize_t num_bytes;
    size_t elem_block_size;
    int err;

    assert (cb != NULL);

    if (DEBUG_SET_FETCH_LEVEL_2) {
        knd_log(".. iterate elems to fetch {key %.*s} {block {offset %zu {size %zu}}",
                1, key, block_offset, block_size);
    }

    err = read_elems_idx_spec(fd, block_offset, block_size, &elems_rec_size, &elems_idx_size);
    KND_TASK_ERR("failed to read elems idx spec {err %d}", err);

    // GSP_ELEMS_SPEC_SIZE
    // last three bytes contain an elem size spec:
    // num elems                  cell-size (1-4)   use-keys (0/1)
    // [2]                        [1]               [0]

    offset = block_offset + elems_rec_size + elems_idx_size;

    lseek(fd, offset - GSP_ELEMS_SPEC_SIZE, SEEK_SET);
    num_bytes = read(fd, buf, GSP_ELEMS_SPEC_SIZE);
    if (num_bytes != GSP_ELEMS_SPEC_SIZE) {
        err =  knd_IO_FAIL;
        KND_TASK_ERR("failed to read elems cell sizes");
    }
    if (buf[2]) use_keys = true;
    cell_size   = buf[1];
    num_elems = buf[0];

    if (DEBUG_SET_FETCH_LEVEL_3) {
        knd_log("== {key %.*s} {use-keys %d} {cell-size %zu}"
                " {num-elems %zu}", 1, key, use_keys, cell_size, num_elems);
    }
    if (cell_size == 0) {
        err =  knd_LIMIT;
        KND_TASK_ERR("elem cell-size cannot be zero");
    }
    if (cell_size > KND_UINT_SIZE) {
        err =  knd_LIMIT;
        KND_TASK_ERR("elem {cell-size %zu} exceeds uint size limit", cell_size);
    }

    if (elems_idx_size <= GSP_ELEMS_SPEC_SIZE) {
        err =  knd_LIMIT;
        KND_TASK_ERR("elem idx size cannot be zero");
    }
    elems_idx_size -= GSP_ELEMS_SPEC_SIZE;

    /* read elem idx */
    offset = block_offset + elems_rec_size;
    lseek(fd, offset, SEEK_SET);
    num_bytes = read(fd, buf, elems_idx_size);
    if (num_bytes != (ssize_t)elems_idx_size) return knd_IO_FAIL;

    if (use_keys) {
        c = (unsigned char*)buf;

        for (size_t i = 0; i < num_elems; i++) {
            elem_block_size = knd_unpack_int(c + 1, cell_size);

            if (DEBUG_SET_FETCH_LEVEL_3) {
                knd_log("    ** [%zu] {elem %c {size %zu}}", i, *c, elem_block_size);
            }

            if (elem_block_size == 0) return knd_LIMIT;
            if (elem_block_size > elems_rec_size) return knd_LIMIT;

            if (*key == *c) {
                err = decode_elem(s, fd, block_offset + elems_offset, elem_block_size,
                                  key, 1, cb, cb_ctx, result, task);
                switch (err) {
                case knd_OK:
                    return knd_OK;
                case knd_NO_MATCH:
                    break;
                default:
                    KND_TASK_ERR("failed to decode {elem %.*s}", 1, key);
                }
                return knd_NO_MATCH;
            }

            elems_offset += elem_block_size;
            c += (cell_size + 1);
        }
        return knd_NO_MATCH;
    }

    /* iterate over a fixed size idx */
    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        c = (unsigned char*)buf + (i * cell_size);
        elem_block_size = knd_unpack_int(c, cell_size);
        if (elem_block_size == 0) continue;

        if (*key == obj_id_seq[i]) {
            err = decode_elem(s, fd, block_offset + elems_offset, elem_block_size,
                              key, 1, cb, cb_ctx, result, task);
            switch (err) {
            case knd_OK:
                return knd_OK;
            case knd_NO_MATCH:
                break;
            default:
                KND_TASK_ERR("failed to decode {elem %.*s}", 1, key);
            }
            return knd_NO_MATCH;
        }
        elems_offset += elem_block_size;
    }
    return knd_NO_MATCH;
}

static int read_subdirs_rec_size(int fd, size_t block_offset, size_t block_size,
                                 size_t *subdirs_rec_size, size_t *subdirs_footer_size,
                                 size_t *elems_block_size)
{
    unsigned char buf[KND_NAME_SIZE];
    unsigned char size_spec = 0;
    ssize_t num_bytes;
    size_t numval;
    size_t offset = block_offset + block_size;
    size_t tail_size;
    size_t footer_size;

    if (DEBUG_SET_FETCH_LEVEL_2) {
        knd_log(".. reading subdirs rec size from {offset %zu {block-size %zu}}",
                offset, block_size);
        if (block_size < KND_NAME_SIZE) {
            lseek(fd, block_offset, SEEK_SET);
            num_bytes = read(fd, buf, block_size);
            knd_log(">> BUF {%.*s}", block_size, buf);
        }
    }

    /* get num value size */
    tail_size = 1;
    lseek(fd, offset - tail_size, SEEK_SET);
    num_bytes = read(fd, &size_spec, 1);
    if (num_bytes != 1) return knd_IO_FAIL;

    /* no subdirs present, just elems */
    if (size_spec == 0) {
        *subdirs_rec_size = 0;
        *elems_block_size = block_size - tail_size;
        if (DEBUG_SET_FETCH_LEVEL_3) {
            knd_log("-- no subdirs, just {elem-block {size %zu}}", *elems_block_size);
        }
        return knd_OK;
    }
    if (size_spec > KND_UINT_SIZE) return knd_LIMIT;

    tail_size += size_spec;
    lseek(fd, offset - tail_size, SEEK_SET);
    num_bytes = read(fd, &buf, (size_t)size_spec);
    if (num_bytes != (ssize_t)size_spec) return knd_IO_FAIL;
    footer_size = knd_unpack_int(buf, size_spec);
    if (footer_size > (block_size - tail_size)) return knd_LIMIT;

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
    if (numval > (block_size - tail_size)) return knd_LIMIT;

    *subdirs_rec_size = numval;
    *subdirs_footer_size = footer_size - (size_spec + 1);

    *elems_block_size = block_size -\
        (*subdirs_rec_size + *subdirs_footer_size + tail_size);

    if (DEBUG_SET_FETCH_LEVEL_3) {
        knd_log("== {subdirs-rec-size %zu} {subdirs-footer-size %zu}"
                " {tail-size %zu} {elems-block-size %zu}\n",
                *subdirs_rec_size, *subdirs_footer_size,
                tail_size, *elems_block_size);
    }
    return knd_OK;
}

static int iterate_subdirs(struct kndSet *s, int fd, size_t global_offset,
                           size_t block_subdirs_rec_size, size_t block_subdirs_footer_size,
                           const char *key, size_t key_size,
                           knd_set_elem_unmarshall_cb_t cb, void *cb_ctx,
                           void **result, struct kndTask *task)
{
    unsigned char buf[KND_NAME_SIZE];
    size_t num_subdirs;
    ssize_t num_bytes;
    bool use_keys = false;
    size_t cell_size;
    const unsigned char *c;
    size_t block_offset = 0;
    size_t subdir_block_size;
    size_t offset = global_offset + block_subdirs_rec_size + block_subdirs_footer_size;
    int err;

    if (DEBUG_SET_FETCH_LEVEL_2) {
        knd_log(".. reading subdirs of {key %.*s} {global-offset %zu {local-offset %zu}}"
                "{subdirs-rec-size %zu} {subdirs-footer-size %zu}",
                key_size, key, global_offset, offset,
                block_subdirs_rec_size, block_subdirs_footer_size);
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

    if (DEBUG_SET_FETCH_LEVEL_3) {
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

    offset = global_offset + block_subdirs_rec_size;

    lseek(fd, offset, SEEK_SET);
    num_bytes = read(fd, buf, block_subdirs_footer_size);
    if (num_bytes != (ssize_t)block_subdirs_footer_size) return knd_IO_FAIL;

    if (use_keys) {
        if (DEBUG_SET_FETCH_LEVEL_2) {
            knd_log(">> {dir %.*s {num-subdirs %zu}} "
                    "linear traversal needed {cell-size %zu} {footer-size %zu}",
                    key_size, key, num_subdirs, cell_size, block_subdirs_footer_size);
        }
        c = (unsigned char*)buf;

        for (size_t i = 0; i < num_subdirs; i++) {
            subdir_block_size = knd_unpack_int(c + 1, cell_size);

            if (DEBUG_SET_FETCH_LEVEL_3) {
                knd_log(">> [%zu] {subdir %c {size %zu}}", i, *c, subdir_block_size);
            }
            if (subdir_block_size == 0) return knd_LIMIT;
            if (subdir_block_size > block_subdirs_rec_size) return knd_LIMIT;

            if (*key == obj_id_seq[i]) {
                err = fetch_elem(s, fd, global_offset + block_offset, subdir_block_size,
                                 key + 1, key_size - 1, cb, cb_ctx, result, task);
                switch (err) {
                case knd_OK:
                    return knd_OK;
                case knd_NO_MATCH:
                    break;
                default:
                    KND_TASK_ERR("failed to fetch {subdir %.*s}", key_size, key);
                }
                return knd_NO_MATCH;
            }

            block_offset += subdir_block_size;
            c += (cell_size + 1);
        }
        return knd_NO_MATCH;
    }

    /* iterate over a fixed size footer */
    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        c = (unsigned char*)buf + (i * cell_size);
        subdir_block_size = knd_unpack_int(c, cell_size);
        if (subdir_block_size == 0) continue;

        if (*key == obj_id_seq[i]) {
            err = fetch_elem(s, fd, global_offset + block_offset, subdir_block_size,
                             key, key_size, cb, cb_ctx, result, task);
            KND_TASK_ERR("failed to fetch {subdir %.*s}", key_size, key);
            return knd_OK;
        }
        block_offset += subdir_block_size;
    }
    return knd_NO_MATCH;
}

static int fetch_elem(struct kndSet *s, int fd, size_t block_offset, size_t block_size,
                      const char *key, size_t key_size,
                      knd_set_elem_unmarshall_cb_t cb, void *cb_ctx,
                      void **result, struct kndTask *task)
{
    size_t subdirs_rec_size = 0;
    size_t subdirs_footer_size = 0;
    size_t elems_block_size = 0;
    int err;

    if (DEBUG_SET_FETCH_LEVEL_2) {
        knd_log(".. reading {block {offset %zu} {size %zu}}",
               block_offset, block_size);
    }

    err = read_subdirs_rec_size(fd, block_offset, block_size, &subdirs_rec_size,
                                &subdirs_footer_size, &elems_block_size);
    KND_TASK_ERR("failed to read subdirs rec size {err %d}", err);

    if (DEBUG_SET_FETCH_LEVEL_3) {
        knd_log("== {subdirs {rec-size %zu} {footer-size %zu} {elems {block-size %zu}}",
                subdirs_rec_size, subdirs_footer_size, elems_block_size);
    }

    /* terminal elem */
    if (key_size == 1) {
        if (elems_block_size == 0) return knd_NO_MATCH;

        err = iterate_elems(s, fd, block_offset, elems_block_size, key, cb, cb_ctx, result, task);
        switch (err) {
        case knd_OK:
            return knd_OK;
        case knd_NO_MATCH:
            break;
        default:
            KND_TASK_ERR("failed to iterate elems");
        }
        return knd_NO_MATCH;
    }

    if (subdirs_rec_size) {
        err = iterate_subdirs(s, fd, block_offset + elems_block_size,
                              subdirs_rec_size, subdirs_footer_size,
                              key, key_size, cb, cb_ctx, result, task);
        switch (err) {
        case knd_OK:
            return knd_OK;
        case knd_NO_MATCH:
            break;
        default:
            KND_TASK_ERR("failed to iterate GSP subdirs");
        }
        return knd_NO_MATCH;
    }
    return knd_NO_MATCH;
}

static int match_storage_leaf(struct kndSet *s, const char *key, size_t key_size,
                              struct kndStorageLeaf **result)
{
    struct kndStorageLeaf *leaf;
    size_t elem_numval;

    assert (s->store != NULL);

    knd_calc_num_id(key, key_size, &elem_numval);

    if (s->store->num_leaves == 0) return knd_NO_MATCH;

    // TODO binary search
    leaf = s->store->leaves[0];
    if (leaf->range_from < elem_numval) {
        *result = leaf;
        return knd_OK;
    }
    return knd_NO_MATCH;
}

int knd_set_fetch(struct kndSet *s, const char *key, size_t key_size,
                  knd_set_elem_unmarshall_cb_t cb, void *cb_ctx,
                  void **result, struct kndTask *task)
{
    struct kndStorageLeaf *leaf;
    const char *filename;
    size_t filename_size;
    struct stat st;
    size_t offset = strlen("GSP");
    int fd;
    int err;

    assert (cb != NULL);

    err = match_storage_leaf(s, key, key_size, &leaf);
    if (err == knd_NO_MATCH) return knd_NO_MATCH;

    filename = leaf->filepath;
    filename_size = leaf->filepath_size;

    if (DEBUG_SET_FETCH_LEVEL_2) {
        knd_log(".. fetching elem {key %.*s} from {leaf %zu}",
                key_size, key, leaf->numid);
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

    err = fetch_elem(s, fd, offset, leaf->curr_size - offset, key, key_size,
                     cb, cb_ctx, result, task);
    close(fd);
    return err;
}
