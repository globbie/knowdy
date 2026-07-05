#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <stdatomic.h>

#include "knd_repo.h"
#include "knd_attr.h"
#include "knd_facet.h"
#include "knd_set.h"
#include "knd_user.h"
#include "knd_query.h"
#include "knd_task.h"
#include "knd_dict.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_proc.h"
#include "knd_mempool.h"
#include "knd_state.h"
#include "knd_storage.h"
#include "knd_commit.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_REPO_GSL_LEVEL_0 0
#define DEBUG_REPO_GSL_LEVEL_1 0
#define DEBUG_REPO_GSL_LEVEL_2 0
#define DEBUG_REPO_GSL_LEVEL_3 0
#define DEBUG_REPO_GSL_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepoSnapshot *snapshot;
};

static int cls_import(const char *rec, size_t *total_size,
                      struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    struct kndClass *cls;
    struct kndClassEntry *entry;
    struct kndSet *cls_idx = task->idxs.cls_idx;
    int err;

    err = knd_class_import(rec, total_size, &cls, snapshot, task);
    KND_TASK_ERR("failed to import a cls");

    entry = cls->entry;
    assert (entry != NULL);

    /* assign a unique cls entry id */
    entry->numid = ++task->idxs.cls_id_count;
    knd_uid_create(entry->numid, entry->id, &entry->id_size);

    err = knd_set_add(cls_idx, entry->id, entry->id_size, (void*)entry, task);
    KND_TASK_ERR("failed to register {cls %.*s} in cls idx",
                 entry->name_size, entry->name);

    if (DEBUG_REPO_GSL_LEVEL_3) {
        knd_log(">> registered {cls %.*s {id %.*s}}", entry->name_size, entry->name,
                entry->id_size, entry->id);
    }
    return knd_OK;
}

static gsl_err_t parse_class_import(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndRepoSnapshot *snapshot = ctx->snapshot;
    int err;

#if 0
    if (task->type != KND_TASK_BULK_LOAD) {
        task->type = KND_TASK_COMMIT;
        if (!commit) {
            err = knd_commit_new(&commit, task->mempool);
            if (err) return make_gsl_err_external(err);

            //commit->orig_state_id = atomic_load_explicit(&task->snapshot->num_commits,
            //                                             memory_order_relaxed);
            task->ctx->commit = commit;
        }
    }
#endif

    err = cls_import(rec, total_size, snapshot, task);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

#if 0
static gsl_err_t parse_class_select(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;

    return knd_class_select(rec, total_size, ctx->repo, ctx->query, ctx->task);
}
#endif

static gsl_err_t parse_proc_import(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndRepoSnapshot *snapshot = ctx->snapshot;
    struct kndTask *task = ctx->task;
    //struct kndUserContext *ctx = task->user_ctx;
    //int err;

    if (task->type != KND_TASK_BULK_LOAD) {
        task->type = KND_TASK_COMMIT;

        if (!task->ctx->commit) {
            //err = knd_commit_new(&task->ctx->commit, task->mempool);
            ///if (err) return make_gsl_err_external(err);

            //task->ctx->commit->orig_state_id = atomic_load_explicit(&task->snapshot->num_commits,
            //                                                        memory_order_relaxed);
        }
    }
    return knd_proc_import(rec, total_size, snapshot, task);
}

static gsl_err_t run_get_schema(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndRepoSnapshot *snapshot = ctx->snapshot;
    struct kndRepo *repo = snapshot->repo;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    if (DEBUG_REPO_GSL_LEVEL_2) {
        knd_log(">> select {repo %.*s {schema %.*s}}",
                repo->name_size, repo->name, name_size, name);
    }

    // TODO
    repo->schema_name = name;
    repo->schema_name_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_schema(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;

    if (DEBUG_REPO_GSL_LEVEL_2) {
        knd_log(".. parse schema REC: \"%.*s\"..", 64, rec);
    }

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_get_schema,
          .obj = ctx
        },
        { .type = GSL_SET_STATE,
          .name = "cls",
          .name_size = strlen("cls"),
          .parse = parse_class_import,
          .obj = ctx
        },
        { .type = GSL_SET_STATE,
          .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc_import,
          .obj = ctx
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
    struct kndMemPool *mempool = task->mempool;
    int err;

    if (DEBUG_REPO_GSL_LEVEL_2) {
        knd_log(".. parsing logic clause: \"%.*s\"", 32, rec);
    }
    err = knd_logic_clause_new(&clause, mempool);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    err = knd_logic_clause_parse(clause, rec, total_size, task);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_init_data(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;

    if (DEBUG_REPO_GSL_LEVEL_2) {
        knd_log(".. parse init data REC: \"%.*s\"..", 64, rec);
    }
    struct gslTaskSpec specs[] = {
                                  /*{ .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_select,
          .obj = task
          },*/
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
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndConcFolder *folder;
    struct kndMemPool *mempool = task->mempool;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);

    if (DEBUG_REPO_GSL_LEVEL_2) {
        knd_log(".. include {file %.*s}", name_size, name);
    }

    err = knd_conc_folder_new(&folder, mempool);
    if (err) {
        knd_log("failed to alloc a conc folder");
        return make_gsl_err_external(knd_NOMEM);
    }

    if (name_size >= KND_PATH_SIZE) return make_gsl_err_external(knd_LIMIT);
    memcpy(folder->name, name, name_size);
    folder->name_size = name_size;

    folder->next = task->folders;
    task->folders = folder;
    task->num_folders++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_include(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;

    if (DEBUG_REPO_GSL_LEVEL_2) {
        knd_log(".. parse include REC: \"%.*s\"..", 64, rec);
    }
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_read_include,
          .obj = task
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static int parse_GSL(const char *rec, size_t *total_size,
                     struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    struct LocalContext ctx = {
        .snapshot = snapshot,
        .task = task
    };

    struct gslTaskSpec specs[] = {
        { .name = "schema",
          .name_size = strlen("schema"),
          .parse = parse_schema,
          .obj = &ctx
        },
        { .name = "init",
          .name_size = strlen("init"),
          .parse = parse_init_data,
          .obj = &ctx
        },
        { .name = "include",
          .name_size = strlen("include"),
          .parse = parse_include,
          .obj = &ctx
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
    struct kndMemBlock *block;
    struct kndConcFolder *folder, *folders;
    const char *c;
    size_t folder_name_size;
    struct stat st;
    const char *index_folder_name = "index";
    size_t index_folder_name_size = strlen("index");
    const char *file_ext = ".gsl";
    size_t file_ext_size = strlen(".gsl");
    size_t file_size;
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

    if (stat(out->buf, &st)) {
        err = knd_IO_FAIL;
        KND_TASK_ERR("no such {file %.*s}", out->buf_size, out->buf);        
    }

    file_size = st.st_size;

    err = knd_memblock_fetch(&block, file_size + 1, task);
    KND_TASK_ERR("failed to fetch a memblock of {size %zu}", file_size + 1);

    err = knd_memblock_read_file(block, out->buf, file_size, true, &task->input);
    KND_TASK_ERR("failed to read memblock from %s {size %zu}", out->buf, file_size);
    task->input_size = file_size;

    err = parse_GSL(task->input, &chunk_size, repo->snapshot, task);
    if (err) {
        knd_log("-- parsing of GSL source {file %.*s} failed, err: %d",
                out->buf_size, out->buf, err);
        return err;
    }

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

                err = read_GSL_file(repo, folder, index_folder_name, index_folder_name_size,
                                    content_type, task);
                if (err) {
                    c = "/";
                    folder_name_size = 1;
                    if (parent_folder) {
                        c = parent_folder->name;
                        folder_name_size = parent_folder->name_size;
                    }
                    KND_TASK_LOG("failed to include {folder %.*s {parent %.*s}}",
                                 folder->name_size, folder->name, folder_name_size, c);
                    return err;
                }
                continue;
            }
        }
        err = read_GSL_file(repo, parent_folder, folder->name, folder->name_size, content_type, task);
        if (err) {
            c = "/";
            folder_name_size = 1;
            if (parent_folder) {
                c = parent_folder->name;
                folder_name_size = parent_folder->name_size;
            }
            KND_TASK_LOG("failed to include {folder %.*s {parent %.*s}}",
                         folder->name_size, folder->name, folder_name_size, c);
            return err;
        }
    }
    return knd_OK;
}

static int resolve_cls(void *elem, void *ctx, struct kndTask *task)
{
    struct kndClassEntry *entry = elem;
    struct kndRepoSnapshot *snapshot = ctx;
    struct kndClass *c;
    int err;

    assert (entry != NULL);

    if (DEBUG_REPO_GSL_LEVEL_2) {
        knd_log(".. resolving {cls %.*s {id %.*s}}",
                entry->name_size, entry->name, entry->id_size, entry->id);
    }

    err = knd_class_acquire(entry, &c, snapshot, task);
    KND_TASK_ERR("failed to acquire {cls %.*s}", entry->name_size, entry->name);

    if (c->phase >= KND_CLASS_RESOLVED) return knd_OK;

    err = knd_class_resolve(c, snapshot, task);
    KND_TASK_ERR("failed to resolve {cls %.*s}", entry->name_size, entry->name);

    return knd_OK;
}

static int index_cls(void *elem, void *ctx, struct kndTask *task)
{
    struct kndClassEntry *entry = elem;
    struct kndRepoSnapshot *snapshot = ctx;
    struct kndClass *cls;
    int err;

    assert (entry != NULL);

    err = knd_class_acquire(entry, &cls, snapshot, task);
    KND_TASK_ERR("failed to acquire {cls %.*s}", entry->name_size, entry->name);

    if (cls->phase >= KND_CLASS_INDEXED) return knd_OK;

    err = knd_class_index(cls, snapshot, task);
    KND_TASK_ERR("failed to index {cls %.*s}", entry->name_size, entry->name);

    return knd_OK;
}

static int build_repo_path(struct kndRepoSnapshot *s,
                           const char *filename, size_t filename_size,
                           char *result, size_t *result_size,
                           struct kndTask *task)
{
    struct kndSteward *steward = task->steward;
    struct kndRepo *repo = s->repo;
    struct kndOutput *out = task->out;
    int err;

    out->reset(out);
    OUT(steward->path, steward->path_size);
    OUT(s->storage->name, s->storage->name_size);
    OUT("/", 1);

    if (repo->name_size == 1) {
        switch (*repo->name) {
        case '/':
            OUT(KND_BASE_REPO_DIR_NAME, strlen(KND_BASE_REPO_DIR_NAME));
            OUT("/", 1);    
            break;
        default:
            break;
        }
    } else {
        OUT(repo->name, repo->name_size);
        OUT("/", 1);
    }

    OUT(filename, filename_size);

    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("file path too long");
    }

    memcpy(result, out->buf, out->buf_size);
    *result_size = out->buf_size;
    result[out->buf_size] = '\0';

    return knd_OK;
}

static int write_meta_file(struct kndRepoSnapshot *s, const char *rec, size_t rec_size,
                           struct kndTask *task)
{
    char tmp_name[KND_PATH_SIZE + 1];
    size_t tmp_name_size;
    char target_name[KND_PATH_SIZE + 1];
    size_t target_name_size;
    int err;

    err = build_repo_path(s, "state.tmp", strlen("state.tmp"),
                          tmp_name, &tmp_name_size, task);
    KND_TASK_ERR("failed building a file path");


    err = knd_write_file((const char*)tmp_name, rec, rec_size);
    KND_TASK_ERR("failed writing to {file %.*s}", tmp_name_size, tmp_name);

    err = build_repo_path(s, "state.gsl", strlen("state.gsl"),
                          target_name, &target_name_size, task);
    KND_TASK_ERR("failed building a file path");

    // TODO: use digital signature?

    err = rename((const char*)tmp_name, (const char*)target_name);
    KND_TASK_ERR("failed renaming {file %.*s} to {file %.*s}",
                 tmp_name_size, tmp_name, target_name, target_name_size);

    if (DEBUG_REPO_GSL_LEVEL_3) {
        knd_log("++ new repo state {file %.*s}", target_name_size, target_name);
    }

    return knd_OK;
}

static int present_idx_meta(struct kndSet *idx, const char *name, size_t name_size,
                            size_t indent_size, size_t depth, struct kndTask *task)
{
    struct kndStorageLeaf *leaf;
    struct kndOutput *out = task->file_out;
    int err;

    assert (idx->store != NULL);

    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_indent(out, (depth + 1) * indent_size);
        RET_ERR();
    }

    OUT("{", 1);
    OUT(name, name_size);

    OUT("\n", 1);
    err = knd_print_indent(out, (depth + 2) * indent_size);
    RET_ERR();
    
    OUT("[leaf", strlen("[leaf"));

     for (size_t i = 0; i < idx->store->num_leaves; i++) {
        leaf = idx->store->leaves[i];
        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_indent(out, (depth + 3) * indent_size);
            RET_ERR();
        }
        err = knd_storage_leaf_export_GSL(leaf, out, indent_size, depth + 3, task);
        KND_TASK_ERR("failed to export storage leaf GSL");
    }
    OUT("]", 1);
    OUT("}", 1);
    return knd_OK;
}

int knd_repo_save_meta(struct kndRepoSnapshot *s, struct kndTask *main_task, struct kndTask *task)
{
    struct kndOutput *out = task->file_out;
    struct kndRepo *repo = s->repo;
    struct kndSet *target_idx;
    size_t indent_size = KND_INDENT_SIZE;
    size_t depth = 1;
    int err;

    out->reset(out);
    OUT("{repo ", strlen("{repo "));
    OUT(repo->name, repo->name_size);

    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_indent(out, (depth) * indent_size);
        RET_ERR();
    }

    OUT("{snapshot ", strlen("{snapshot "));
    OUTF("%zu", s->numid);

    err = present_idx_meta(main_task->idxs.attr_name_idx->idx, "attr-names", strlen("attr-names"),
                           indent_size, depth + 1, task);
    KND_TASK_ERR("failed to present attr name idx meta");

    target_idx = s->cache.attr_idx;
    err = present_idx_meta(target_idx, "attrs", strlen("attrs"),
                           indent_size, depth + 1, task);
    KND_TASK_ERR("failed to present attrs");

    err = present_idx_meta(main_task->idxs.cls_name_idx->idx, "cls-names", strlen("cls-names"),
                           indent_size, depth + 1, task);
    KND_TASK_ERR("failed to present cls name idx meta");

    target_idx = s->cache.cls_idx;
    err = present_idx_meta(target_idx, "cls-content", strlen("cls-content"),
                           indent_size, depth + 1, task);
    KND_TASK_ERR("failed to present cls content");

    target_idx = s->cache.cls_cache_idx;
    err = present_idx_meta(target_idx, "cls-cache", strlen("cls-cache"),
                           indent_size, depth + 1, task);
    KND_TASK_ERR("failed to present cls cache meta");

    target_idx = s->cache.str_idx;
    err = present_idx_meta(target_idx, "charseqs", strlen("charseqs"),
                           indent_size, depth + 1, task);
    KND_TASK_ERR("failed to present charseq idx meta");

    err = present_idx_meta(main_task->idxs.str_dict->idx, "charseq-dict", strlen("charseq-dict"),
                           indent_size, depth + 1, task);
    KND_TASK_ERR("failed to present charseq dict meta");

    if (DEBUG_REPO_GSL_LEVEL_3) {
        knd_log(">> update repo meta %.*s", out->buf_size, out->buf);
    }

    OUT("}", 1); // snapshot
    OUT("}", 1); // repo

    err = write_meta_file(s, out->buf, out->buf_size, task);
    KND_TASK_ERR("failed saving meta file of {repo %.*s}", repo->name_size, repo->name);

    return knd_OK;
}

int knd_repo_read_sources(struct kndRepo *repo, struct kndTask *task)
{
    struct kndRepoSnapshot *snapshot = repo->snapshot;
    int err;

    if (DEBUG_REPO_GSL_LEVEL_TMP) {
        knd_log(".. initial loading of system schema source files for {repo %.*s}",
                repo->name_size, repo->name);
    }

    task->type = KND_TASK_BULK_LOAD;

    err = read_GSL_file(repo, NULL, KND_PACKAGE_INDEX_NAME, strlen(KND_PACKAGE_INDEX_NAME),
                        KND_GSL_SCHEMA, task);
    KND_TASK_ERR("system schema import failed");

    if (DEBUG_REPO_GSL_LEVEL_TMP) {
        knd_log(".. resolving classes of {repo %.*s}", repo->name_size, repo->name);
    }

    /* resolve class references */
    err = knd_set_map(task->idxs.cls_idx, NULL, NULL, NULL, resolve_cls, snapshot, task);
    KND_TASK_ERR("failed to resolve all entries in class idx");

    //err = resolve_procs(repo, task);
    //KND_TASK_ERR("proc resolving failed");

    /* build reverse indices */
    err = knd_set_map(task->idxs.cls_idx, NULL, NULL, NULL, index_cls, snapshot, task);
    KND_TASK_ERR("failed to index all entries in class idx");

    /* any instances to load? */
    if (repo->data_path_size) {
        if (DEBUG_REPO_GSL_LEVEL_3) {
            knd_log(".. initial loading of data files");
        }
        task->type = KND_TASK_BULK_LOAD;
        err = read_GSL_file(repo, NULL, KND_PACKAGE_INDEX_NAME, strlen(KND_PACKAGE_INDEX_NAME),
                            KND_GSL_INIT_DATA, task);
        KND_TASK_ERR("init data import failed");

        //err = resolve_class_insts(repo, task);
        //KND_TASK_ERR("class insts resolving failed");

        //err = index_repo_class_insts(repo, task);
        //KND_TASK_ERR("class insts indexing failed");
    }
    return knd_OK;
}

