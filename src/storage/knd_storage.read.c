#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <stdatomic.h>

#include "knd_repo.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_proc.h"
#include "knd_state.h"
#include "knd_commit.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_STORAGE_READ_LEVEL_0 0
#define DEBUG_STORAGE_READ_LEVEL_1 0
#define DEBUG_STORAGE_READ_LEVEL_2 0
#define DEBUG_STORAGE_READ_LEVEL_3 0
#define DEBUG_STORAGE_READ_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndStorageWal *wal;
    struct kndStateRange *range;
};

static int check_commit_body(struct kndStorageLeaf *unused_var(leaf), int fd,
                             size_t commit_id, size_t offset,
                             size_t commit_body_size, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    char *buf;
    ssize_t num_bytes;
    int err;

    out->reset(out);
    buf = out->buf;

    lseek(fd, offset, SEEK_SET);
    num_bytes = read(fd, buf, commit_body_size);
    if (num_bytes != (ssize_t)commit_body_size) {
        err = knd_IO_FAIL;
        KND_TASK_ERR("failed to read commit body");
    }

    knd_log("{commit %zu {rec %.*s}}", commit_id, commit_body_size, buf);

    // TODO get commit numid and try to match it with a given commit_id

    return knd_OK;
}

static int check_wal_commits(struct kndStorageLeaf *leaf,
                             const char *idx_filename, size_t idx_filename_size,
                             size_t commit_id_size, size_t max_commit_size,
                             size_t num_commits, struct kndTask *task)
{
    struct kndOutput *file_out = task->file_out;
    const unsigned char *rec;
    size_t commit_id;
    size_t commit_body_size = 0;
    size_t total_size = 0;
    int fd;
    int err;

    file_out->reset(file_out);
    err = file_out->write_file_content(file_out, idx_filename);
    KND_TASK_ERR("failed to copy content from {file %.s}", idx_filename_size, idx_filename);

    /* NB: don't forget to close fd */
    fd = open(leaf->filename, O_RDONLY);
    if (fd == -1) {
        err = knd_IO_FAIL;
        KND_TASK_ERR("failed to open leaf {file %.*s}", leaf->filename_size, leaf->filename);
    }
    
    rec = (unsigned char*)file_out->buf;

    for (size_t i = 0; i < num_commits; i++) {
        commit_id = knd_unpack_int(rec, commit_id_size);
        rec += commit_id_size;

        commit_body_size = knd_unpack_int(rec, max_commit_size);
        rec += max_commit_size;

        err = check_commit_body(leaf, fd, commit_id, total_size, commit_body_size, task);
        if (err) {
            KND_TASK_LOG("failed to check {commit %zu} body", commit_id);
            goto final;
        }
        total_size += commit_body_size;
    }

    if (total_size > leaf->curr_size) {
        err = knd_IO_FAIL;
        KND_TASK_LOG("{leaf-file %.*s} size mismatch: commit body {sum-size %zu} "
                     " is greater than actual {leaf-file-size %zu}",
                     leaf->filename_size, leaf->filename, total_size, leaf->curr_size);
        goto final;
    }

 final:
    close(fd);
    return knd_OK;
}

static int check_wal_idx_size(const char *filename, size_t filename_size,
                              size_t idx_rec_size, size_t *num_commits, struct kndTask *task)
{
    struct stat st;
    ldiv_t result;
    int err;

    if (stat(filename, &st)) {
        err = knd_IO_FAIL;
        KND_TASK_ERR("failed to open leaf idx {file %.*s}", filename_size, filename);
    }

    result = ldiv((unsigned long)st.st_size, idx_rec_size);

    if (DEBUG_STORAGE_READ_LEVEL_2) {
        knd_log("{leaf-idx-size %zu} {idx-rec-size %zu} {num-commits %lu {remainder %lu}}",
                st.st_size, idx_rec_size, result.quot, result.rem);
    }

    if (result.rem != 0) {
        err = knd_FORMAT;
        KND_TASK_ERR("{file %.*s} has incorrect format, idx record size should be %zu bytes",
                     filename_size, filename, idx_rec_size);        
    }

    *num_commits = result.quot;
    return knd_OK;
}

static int check_wal_leaf(struct kndStorageWal *wal, struct kndStorageLeaf *leaf, struct kndTask *task)
{
    char buf[KND_PATH_SIZE + 1];
    size_t buf_size;
    struct kndOutput *out = task->out;
    size_t commit_id_size = knd_min_bytes(wal->snapshot->max_wal_commits);
    size_t max_commit_size = knd_min_bytes(leaf->max_size);
    size_t idx_rec_size = commit_id_size + max_commit_size;
    size_t num_commits;
    struct stat st;
    int err;

    if (stat(leaf->filename, &st)) {
        err = knd_IO_FAIL;
        KND_TASK_ERR("failed to open leaf {file %.*s}", leaf->filename_size, leaf->filename);
    }
    leaf->curr_size = st.st_size;

    /* check wal leaf idx file */
    out->reset(out);
    OUT(leaf->filename, leaf->filename_size);
    OUT(KND_IDX_FILE_EXT_NAME, strlen(KND_IDX_FILE_EXT_NAME));
    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("file path name size %zu exceeds limit", out->buf_size);
    }
    memcpy(buf, out->buf, out->buf_size);
    buf_size = out->buf_size;
    buf[buf_size] = '\0';

    err = check_wal_idx_size(buf, buf_size, idx_rec_size, &num_commits, task);
    KND_TASK_ERR("incorrect wal idx size {file %.*s}", buf_size, buf);

    err = check_wal_commits(leaf, buf, buf_size, commit_id_size, max_commit_size, num_commits, task);
    KND_TASK_ERR("failed to check wal commits {file %.*s}", buf_size, buf);

    leaf->num_elems = num_commits;
    wal->num_commits += num_commits;

    return knd_OK;
}

static int check_wal_leaves(struct kndStorageWal *wal, struct kndTask *task)
{
    struct kndStorageLeaf *leaf;
    int err;

    FOREACH (leaf, wal->leaves) {
        err = check_wal_leaf(wal, leaf, task);
        KND_TASK_ERR("failed to check {leaf %zu}", leaf->numid);
    }
    return knd_OK;
}

static gsl_err_t add_wal_leaf(void *obj, const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndStorageWal *wal = ctx->wal;
    struct kndStorageLeaf *leaf;
    char buf[KND_SHORT_NAME_SIZE];
    long numval;
    int err;

    if (val_size >= KND_SHORT_NAME_SIZE) {
        err = knd_LIMIT;
        KND_TASK_LOG("failed to build a snapshot path");
        return make_gsl_err_external(err);
    }

    memcpy(buf, val, val_size);
    buf[val_size] = '\0';

    err = knd_parse_int(buf, &numval);
    if (err) {
        return make_gsl_err_external(err);
    }

    err = knd_storage_leaf_new(&leaf, numval, wal->path, wal->path_size,
                               0, KND_MAX_WAL_LEAF_SIZE, KND_LEAF_WAL, KND_STORAGE_MODE_READ_ONLY);
    if (err) {
        KND_TASK_LOG("failed to alloc a storage wal");
        return make_gsl_err_external(err);
    }

    knd_wal_append_leaf(wal, leaf);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_wal_leaf(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndStorageLeaf *leaf;
    size_t num_elems = 0;
    char range_from[KND_ID_SIZE];
    size_t range_from_size = 0;
    char range_to[KND_ID_SIZE];
    size_t range_to_size = 0;
    size_t phase_numid = 0;
    size_t idx_file_size = 0;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = add_wal_leaf,
          .obj = obj
        },
        {   .name = "range-from",
            .name_size = strlen("range-from"),
            .buf = range_from,
            .buf_size = &range_from_size,
            .max_buf_size = KND_ID_SIZE
        },
        {   .name = "range-to",
            .name_size = strlen("range-to"),
            .buf = range_to,
            .buf_size = &range_to_size,
            .max_buf_size = KND_ID_SIZE
        },
        {   .name = "phase",
            .name_size = strlen("phase"),
            .parse = gsl_parse_size_t,
            .obj = &phase_numid
        },
        {   .name = "num-commits",
            .name_size = strlen("num-commits"),
            .parse = gsl_parse_size_t,
            .obj = &num_elems
        },
        {   .name = "file-size",
            .name_size = strlen("file-size"),
            .parse = gsl_parse_size_t,
            .obj = &idx_file_size
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    leaf = ctx->wal->leaves;
    assert (leaf != NULL);

    leaf->num_elems = num_elems;
    // TODO other params

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_wal_leaf_array(void *obj, const char *rec, size_t *total_size)
{
    struct gslTaskSpec spec = {
        .is_list_item = true,
        .parse = parse_wal_leaf,
        .obj = obj
    };
    return gsl_parse_array(&spec, rec, total_size);
}

static gsl_err_t check_wal_name(void *obj, const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    struct kndStorageWal *wal = ctx->wal;
    struct kndTask *task = ctx->task;
    size_t agent_id;
    int err;

    knd_calc_num_id(val, val_size, &agent_id);

    if (agent_id != wal->agent_id) {
        err = knd_CONFLICT;
        KND_TASK_LOG("wal agent id mismatch: {wal %zu} vs %zu", wal->agent_id, agent_id);
        return make_gsl_err_external(err);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_wal(void *obj, const char *rec, size_t *total_size)
{
    size_t wal_max_size = 0;

    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .run = check_wal_name,
            .obj = obj
        },
        {   .name = "max-leaf-size",
            .name_size = strlen("max-leaf-size"),
            .parse = gsl_parse_size_t,
            .obj = &wal_max_size
        },
        {   .name = "leaf",
            .name_size = strlen("leaf"),
            .type = GSL_GET_ARRAY_STATE,
            .parse = parse_wal_leaf_array,
            .obj = obj
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- config parse error: %d", parser_err.code);
        return parser_err;
    }
    return make_gsl_err(gsl_OK);
}

int knd_wal_state_read(struct kndStorageWal *wal,
                       const char *filename, size_t filename_size,
                       struct kndStateRange *range, struct kndTask *task)
{
    struct kndOutput *file_out = task->file_out;
    struct stat st;
    size_t total_parsed;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_STORAGE_READ_LEVEL_2) {
        knd_log(">> {wal %.*s} to read its state from {file %.*s}}",
                wal->name_size, wal->name, filename_size, filename);
    }

    if (stat(filename, &st)) {
        err = knd_IO_FAIL;
        KND_TASK_ERR("failed to open {file %.*s}", filename_size, filename);
    }

    file_out->reset(file_out);
    err = file_out->write_file_content(file_out, filename);
    KND_TASK_ERR("failed to copy content from {file %.s}", filename_size, filename);

    total_parsed = file_out->buf_size;

    struct LocalContext ctx = {
       .task = task,
       .wal = wal,
       .range = range
    };

    struct gslTaskSpec specs[] = {
        {
            .name = "wal",
            .name_size = strlen("wal"),
            .parse = parse_wal,
            .obj = &ctx
        }
    };

    parser_err = gsl_parse_task(file_out->buf, &total_parsed, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code != gsl_OK) {
        KND_TASK_LOG("failed to read repo state config");
        return gsl_err_to_knd_err_codes(parser_err);
    }

    err = check_wal_leaves(wal, task);
    KND_TASK_ERR("failed to check wal leaves consistency");

    return knd_OK;
}

static int read_rec_body(struct kndStorageLeaf *unused_var(leaf), int fd,
                         size_t rec_id, size_t offset, size_t rec_body_size,
                         knd_leaf_rec_unmarshall_cb_t cb, void *cb_ctx, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    char *buf;
    ssize_t num_bytes;
    void *result;
    size_t parsed_rec_size = 0;
    int err;

    if (DEBUG_STORAGE_READ_LEVEL_2) {
        knd_log(".. reading {wal-rec-id %zu {body-size %zu}}", rec_id, rec_body_size);
    }

    out->reset(out);
    buf = out->buf;

    lseek(fd, offset, SEEK_SET);
    num_bytes = read(fd, buf, rec_body_size);
    if (num_bytes != (ssize_t)rec_body_size) {
        err = knd_IO_FAIL;
        KND_TASK_ERR("failed to read rec body");
    }

    err = cb(rec_id, buf, rec_body_size, cb_ctx, &parsed_rec_size, &result, task);
    KND_TASK_ERR("failed to unmarshall {wal-leaf-rec %zu}", rec_id);

    return knd_OK;
}

static int unmarshall_wal_leaf_recs(struct kndStorageLeaf *leaf, int fd,
                                    size_t rec_id_size, size_t max_rec_size,
                                    struct kndSetRange *unused_var(range),
                                    knd_leaf_rec_unmarshall_cb_t cb, void *cb_ctx,
                                    struct kndTask *task)
{
    struct kndOutput *file_out = task->file_out;
    struct kndOutput *out = task->out;
    size_t num_recs = leaf->num_elems;
    char buf[KND_PATH_SIZE + 1];
    size_t buf_size;
    const unsigned char *rec;
    size_t rec_id;
    size_t rec_body_size = 0;
    size_t total_size = 0;    
    int err;

    /* check wal leaf idx file */
    out->reset(out);
    OUT(leaf->filename, leaf->filename_size);
    OUT(KND_IDX_FILE_EXT_NAME, strlen(KND_IDX_FILE_EXT_NAME));
    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("file path name size %zu exceeds limit", out->buf_size);
    }
    memcpy(buf, out->buf, out->buf_size);
    buf_size = out->buf_size;
    buf[buf_size] = '\0';

    file_out->reset(file_out);
    err = file_out->write_file_content(file_out, buf);
    KND_TASK_ERR("failed to copy content from {file %.s}", buf_size, buf);

    rec = (unsigned char*)file_out->buf;

    for (size_t i = 0; i < num_recs; i++) {
        rec_id = knd_unpack_int(rec, rec_id_size);
        rec += rec_id_size;

        rec_body_size = knd_unpack_int(rec, max_rec_size);
        rec += max_rec_size;

        // TODO check range

        err = read_rec_body(leaf, fd, rec_id, total_size, rec_body_size, cb, cb_ctx, task);
        KND_TASK_ERR("failed to read {rec %zu} body", rec_id);
        total_size += rec_body_size;
    }

    if (total_size > leaf->curr_size) {
        err = knd_IO_FAIL;
        KND_TASK_ERR("{leaf-file %.*s} size mismatch: rec body {sum-size %zu} "
                     " is greater than actual {leaf-file-size %zu}",
                     leaf->filename_size, leaf->filename, total_size, leaf->curr_size);
    }

    return knd_OK;
}

int knd_wal_leaf_read(struct kndStorageLeaf *leaf,
                      size_t rec_id_size, size_t max_rec_size, struct kndSetRange *range,
                      knd_leaf_rec_unmarshall_cb_t cb, void *cb_ctx, struct kndTask *task)
{
    struct stat st;
    const char *filename = leaf->filename;
    size_t filename_size = leaf->filename_size;
    int fd;
    int err;

    if (DEBUG_STORAGE_READ_LEVEL_TMP) {
        knd_log(".. reading {leaf %.*s {filename %.*s} {size %zu}}",
                leaf->name_size, leaf->name, filename_size, filename, leaf->curr_size);
    }

    if (stat(filename, &st)) {
        err = knd_IO_FAIL;
        KND_TASK_ERR("no such {file %.*s}", filename_size, filename);
    }

    if (leaf->curr_size != (size_t)st.st_size) {
        err = knd_IO_FAIL;
        KND_TASK_ERR("{leaf-file %.*s} size mismatch: expected %zu, not %zu bytes",
                     filename_size, filename, leaf->curr_size, st.st_size);
    }

    fd = open(filename, O_RDONLY);
    if (fd == -1) {
        err = knd_IO_FAIL;
        KND_TASK_ERR("failed to open {file %.*s}", filename_size, filename);
    }

    err = unmarshall_wal_leaf_recs(leaf, fd, rec_id_size, max_rec_size, range, cb, cb_ctx, task);
    close(fd);
    return err;
}
