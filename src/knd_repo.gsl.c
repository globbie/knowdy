#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <stdatomic.h>

#include "knd_repo.h"
#include "knd_attr.h"
#include "knd_set.h"
#include "knd_shared_set.h"
#include "knd_user.h"
#include "knd_query.h"
#include "knd_task.h"
#include "knd_dict.h"
#include "knd_shared_dict.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_proc.h"
#include "knd_mempool.h"
#include "knd_state.h"
#include "knd_commit.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_REPO_GSL_LEVEL_0 0
#define DEBUG_REPO_GSL_LEVEL_1 0
#define DEBUG_REPO_GSL_LEVEL_2 0
#define DEBUG_REPO_GSL_LEVEL_3 0
#define DEBUG_REPO_GSL_LEVEL_TMP 1

static inline void append_memblock(struct kndRepoSnapshot *self, struct kndMemBlock *block)
{
    block->next = self->blocks;
    self->blocks = block;
    self->num_blocks++;
    self->total_block_size += block->buf_size;
}

static gsl_err_t parse_class_import(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndUserContext *ctx = task->user_ctx;
    struct kndRepo *repo = ctx->repo ? ctx->repo : task->repo;
    struct kndCommit *commit = task->ctx->commit;
    int err;

    if (task->type != KND_BULK_LOAD_STATE) {
        task->type = KND_COMMIT_STATE;
        if (!commit) {
            err = knd_commit_new(task->mempool, &commit);
            if (err) return make_gsl_err_external(err);

            commit->orig_state_id = atomic_load_explicit(&task->snapshot->num_commits,
                                                         memory_order_relaxed);
            task->ctx->commit = commit;
        }
    }
    return knd_class_import(repo, rec, total_size, task);
}

static gsl_err_t parse_class_select(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndUserContext *ctx = task->user_ctx;
    struct kndRepo *repo = ctx->repo ? ctx->repo : task->repo;
    // struct kndCommit *commit = task->ctx->commit;

    return knd_class_select(repo, rec, total_size, task);
}

static gsl_err_t parse_proc_import(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndUserContext *ctx = task->user_ctx;
    struct kndRepo *repo = ctx->repo ? ctx->repo : task->repo;
    int err;

    if (task->type != KND_BULK_LOAD_STATE) {
        task->type = KND_COMMIT_STATE;

        if (!task->ctx->commit) {
            err = knd_commit_new(task->mempool, &task->ctx->commit);
            if (err) return make_gsl_err_external(err);

            task->ctx->commit->orig_state_id = atomic_load_explicit(&task->snapshot->num_commits,
                                                                    memory_order_relaxed);
        }
    }
    return knd_proc_import(repo, rec, total_size, task);
}

static gsl_err_t run_get_schema(void *obj, const char *name, size_t name_size)
{
    struct kndTask *self = obj;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    if (DEBUG_REPO_GSL_LEVEL_2)
        knd_log(".. select repo schema: \"%.*s\"..", name_size, name);

    self->repo->schema_name = name;
    self->repo->schema_name_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_schema(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;

    if (DEBUG_REPO_GSL_LEVEL_2)
        knd_log(".. parse schema REC: \"%.*s\"..", 64, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_get_schema,
          .obj = task
        },
        { .type = GSL_SET_STATE,
          .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_import,
          .obj = task
        },
        { .type = GSL_SET_STATE,
          .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc_import,
          .obj = task
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t confirm_init_data(void *unused_var(obj),
                                   const char *unused_var(name), size_t unused_var(name_size))
{
    // TODO: reject empty files
    if (DEBUG_REPO_GSL_LEVEL_TMP) {
        knd_log("-- warning: empty GSL file?");
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_logic_clause(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndLogicClause *clause;
    struct kndMemPool *mempool = task->user_ctx->mempool;
    int err;

    if (DEBUG_REPO_GSL_LEVEL_2)
        knd_log(".. parsing logic clause: \"%.*s\"", 32, rec);

    err = knd_logic_clause_new(mempool, &clause);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    err = knd_logic_clause_parse(clause, rec, total_size, task);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_init_data(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;

    if (DEBUG_REPO_GSL_LEVEL_2)
        knd_log(".. parse init data REC: \"%.*s\"..", 64, rec);

    struct gslTaskSpec specs[] = {
        { .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_select,
          .obj = task
        },
        { .type = GSL_SET_STATE,
          .name = "stm",
          .name_size = strlen("stm"),
          .parse = parse_logic_clause,
          .obj = task
        },
        { .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc_import,
          .obj = task
        },
        { .is_default = true,
          .run = confirm_init_data,
          .obj = task
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t run_read_include(void *obj, const char *name, size_t name_size)
{
    struct kndTask *task = obj;
    struct kndConcFolder *folder;
    struct kndMemPool *mempool = task->mempool;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);

    if (DEBUG_REPO_GSL_LEVEL_2)
        knd_log(".. include {file %.*s}", name_size, name);

    err = knd_conc_folder_new(mempool, &folder);
    if (err) {
        knd_log("failed to alloc a conc folder");
        return make_gsl_err_external(knd_NOMEM);
    }
    folder->name = name;
    folder->name_size = name_size;

    folder->next = task->folders;
    task->folders = folder;
    task->num_folders++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_include(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;

    if (DEBUG_REPO_GSL_LEVEL_2)
        knd_log(".. parse include REC: \"%.*s\"..", 64, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_read_include,
          .obj = task
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static int parse_GSL(struct kndTask *task, const char *rec, size_t *total_size)
{
    struct gslTaskSpec specs[] = {
        { .name = "schema",
          .name_size = strlen("schema"),
          .parse = parse_schema,
          .obj = task
        },
        { .name = "init",
          .name_size = strlen("init"),
          .parse = parse_init_data,
          .obj = task
        },
        { .name = "include",
          .name_size = strlen("include"),
          .parse = parse_include,
          .obj = task
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    return knd_OK;
}

static int write_filepath(struct kndOutput *out, struct kndConcFolder *folder)
{
    int err;
    if (folder->parent) {
        err = write_filepath(out, folder->parent);
        if (err) return err;
    }
    OUT(folder->name, folder->name_size);
    return knd_OK;
}

static int read_GSL_file(struct kndRepo *repo, struct kndConcFolder *parent_folder,
                         const char *filename, size_t filename_size,
                         knd_content_type content_type, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndOutput *file_out = task->file_out;
    struct kndRepoSnapshot *snapshot = task->snapshot;
    struct kndMemBlock *block;
    struct kndConcFolder *folder, *folders;
    const char *c;
    size_t folder_name_size;
    const char *index_folder_name = "index";
    size_t index_folder_name_size = strlen("index");
    const char *file_ext = ".gsl";
    size_t file_ext_size = strlen(".gsl");
    size_t chunk_size = 0;
    int err;

    out->reset(out);
    switch (content_type) {
    case KND_GSL_SCHEMA:
        OUT(repo->schema_path, repo->schema_path_size);
        break;
    case KND_GSL_INIT_DATA:
        OUT(repo->data_path, repo->data_path_size);
        break;
    default:
        break;
    }
    OUT("/", 1);

    if (parent_folder) {
        err = write_filepath(out, parent_folder);
        KND_TASK_ERR("failed to write a filepath");
    }
    OUT(filename, filename_size);
    OUT(file_ext, file_ext_size);

    if (DEBUG_REPO_GSL_LEVEL_3) {
        knd_log(".. reading GSL {file %.*s} {content-type %d}",
                out->buf_size, out->buf, content_type);
    }
    file_out->reset(file_out);
    err = file_out->write_file_content(file_out, (const char*)out->buf);
    if (err) {
        knd_log("failed to read GSL {file %.*s}", out->buf_size, out->buf);
        return err;
    }
    err = knd_memblock_new(&block, snapshot->num_blocks, file_out->buf_size + 1);
    KND_TASK_ERR("failed to alloc a memblock");

    block->buf_size = file_out->buf_size;
    memcpy(block->buf, file_out->buf, file_out->buf_size);
    block->buf[block->buf_size] = '\0';

    append_memblock(snapshot, block);

    if (DEBUG_REPO_GSL_LEVEL_2) {
        knd_log("== total GSL source files: %zu", snapshot->num_blocks);
    }
    task->input = block->buf;
    task->input_size = block->buf_size;

    /* actual parsing */
    err = parse_GSL(task, (const char*)block->buf, &chunk_size);
    if (err) {
        knd_log("-- parsing of GSL source {file %.*s} failed, err: %d",
                out->buf_size, out->buf, err);
        return err;
    }

    /* high time to read our folders */
    folders = task->folders;
    task->folders = NULL;
    task->num_folders = 0;

    FOREACH (folder, folders) {
        folder->parent = parent_folder;

        /* reading a subfolder */
        if (folder->name_size > index_folder_name_size) {
            folder_name_size = folder->name_size - index_folder_name_size;
            c = folder->name + folder_name_size;
            if (!memcmp(c, index_folder_name, index_folder_name_size)) {
                /* right trim the folder's name */
                folder->name_size = folder_name_size;

                err = read_GSL_file(repo, folder,
                                    index_folder_name, index_folder_name_size,
                                    content_type, task);
                if (err) {
                    c = "/";
                    folder_name_size = 1;
                    if (parent_folder) {
                        c = parent_folder->name;
                        folder_name_size = parent_folder->name_size;
                    }
                    KND_TASK_LOG("failed to include \"%.*s\" (parent folder: %.*s)",
                                 folder->name_size, folder->name, folder_name_size, c);
                    return err;
                }
                continue;
            }
        }
        err = read_GSL_file(repo, parent_folder, folder->name, folder->name_size,
                            content_type, task);
        if (err) {
            c = "/";
            folder_name_size = 1;
            if (parent_folder) {
                c = parent_folder->name;
                folder_name_size = parent_folder->name_size;
            }
            KND_TASK_LOG("failed to include \"%.*s\" (parent folder: %.*s)",
                         folder->name_size, folder->name, folder_name_size, c);
            return err;
        }
    }
    return knd_OK;
}

static int resolve_class(void *obj, const char *unused_var(elem_id),
                         size_t unused_var(elem_id_size),
                         size_t unused_var(count), void *elem)
{
    struct kndTask *task = obj;
    struct kndClassEntry *entry = elem;
    struct kndClass *c;
    int err;

    err = knd_class_acquire(entry, &c, task);
    KND_TASK_ERR("failed to acquire class %.*s", entry->name_size, entry->name);

    if (c->phase >= KND_CLASS_RESOLVED) return knd_OK;

    err = knd_class_resolve(c, task);
    KND_TASK_ERR("failed to resolve {class %.*s}", entry->name_size, entry->name);

    return knd_OK;
}

static int index_class(void *obj, const char *unused_var(elem_id),
                       size_t unused_var(elem_id_size),
                       size_t unused_var(count), void *elem)
{
    struct kndTask *task = obj;
    struct kndClassEntry *entry = elem;
    struct kndClass *c;
    struct kndSharedSet *class_idx = task->idxs->class_idx;
    int err;

    err = knd_class_acquire(entry, &c, task);
    KND_TASK_ERR("failed to acquire {class %.*s}", entry->name_size, entry->name);

    if (c->is_indexed) return knd_OK;

    err = knd_class_index(c, task);
    KND_TASK_ERR("failed to index {class %.*s}", entry->name_size, entry->name);

    err = knd_shared_set_add(class_idx, entry->id, entry->id_size, (void*)entry);
    KND_TASK_ERR("failed to register {class %.*s} in class idx",
                 entry->name_size, entry->name);
    return knd_OK;
}

#if 0
static int index_class_insts(struct kndClass *c, struct kndTask *task)
{
    struct kndClassInstEntry *entry;
    struct kndSharedDictItem *item, *items;
    struct kndSharedDict *name_idx = c->inst_name_idx;
    int err;

    if (DEBUG_REPO_GSL_LEVEL_2)
        knd_log(".. resolving insts of {class %.*s}..", c->name_size, c->name);

    // TODO: iterate func in kndSharedDict
    for (size_t i = 0; i < name_idx->size; i++) {
        items = atomic_load_explicit(&name_idx->hash_array[i], memory_order_relaxed);
        FOREACH (item, items) {
            entry = item->data;

            err = knd_class_inst_index(entry->inst, task);
            KND_TASK_ERR("failed to resolve {class %.*s {inst %.*s}}",
                         c->name_size, c->name, entry->name_size, entry->name);
        }
    }
    return knd_OK;
}

static int index_repo_class_insts(struct kndRepo *self, struct kndTask *task)
{
    struct kndClass *c;
    struct kndClassEntry *entry;
    struct kndSharedDictItem *item, *items;
    struct kndSharedDict *name_idx = task->idxs->class_name_idx;
    int err;

    if (DEBUG_REPO_GSL_LEVEL_2)
        knd_log(".. indexing class instances in {repo %.*s}..", self->name_size, self->name);

    // TODO: iterate func in kndSharedDict
    for (size_t i = 0; i < name_idx->size; i++) {
        items = atomic_load_explicit(&name_idx->hash_array[i], memory_order_relaxed);
        FOREACH (item, items) {
            entry = item->data;

            if (!entry->cached_version) {
                knd_log("-- unresolved {class-entry %.*s}", entry->name_size, entry->name);
                return knd_FAIL;
            }
            
            err = knd_class_acquire(entry, &c, task);
            KND_TASK_ERR("failed to acquire class %.*s", entry->name_size, entry->name);

            if (!c->inst_name_idx) continue;

            err = index_class_insts(c, task);
            KND_TASK_ERR("failed to index insts of {class %.*s}", entry->name_size, entry->name);
        }
    }
    return knd_OK;
}
#endif

int knd_repo_read_sources(struct kndRepo *self, struct kndTask *task)
{
    int err;

    if (DEBUG_REPO_GSL_LEVEL_TMP) {
        knd_log(".. initial loading of schema source files for {repo %.*s}",
                self->name_size, self->name);
    }
    /* read a system-wide schema */
    task->type = KND_BULK_LOAD_STATE;
    err = read_GSL_file(self, NULL,
                        KND_PACKAGE_INDEX_NAME, strlen(KND_PACKAGE_INDEX_NAME),
                        KND_GSL_SCHEMA, task);
    KND_TASK_ERR("schema import failed");

    /* resolve all cross references */
    err = knd_shared_dict_map(task->idxs->class_name_idx, resolve_class, (void*)task);
    KND_TASK_ERR("failed to resolve all entries in class name idx");

    //err = resolve_procs(self, task);
    //KND_TASK_ERR("proc resolving failed");

    /* build indices */
    err = knd_shared_dict_map(task->idxs->class_name_idx, index_class, (void*)task);
    KND_TASK_ERR("failed to index all entries in class name idx");

    if (self->data_path_size) {
        if (DEBUG_REPO_GSL_LEVEL_3)
            knd_log(".. initial loading of data files");
        task->type = KND_BULK_LOAD_STATE;
        err = read_GSL_file(self, NULL,
                            KND_PACKAGE_INDEX_NAME, strlen(KND_PACKAGE_INDEX_NAME),
                            KND_GSL_INIT_DATA, task);
        KND_TASK_ERR("init data import failed");

        //err = resolve_class_insts(self, task);
        //KND_TASK_ERR("class insts resolving failed");

        //err = index_repo_class_insts(self, task);
        //KND_TASK_ERR("class insts indexing failed");
    }
    return knd_OK;
}

