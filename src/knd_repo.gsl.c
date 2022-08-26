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
#include "knd_shard.h"
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

            commit->orig_state_id = atomic_load_explicit(&task->repo->snapshots->num_commits,
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

            task->ctx->commit->orig_state_id = atomic_load_explicit(&repo->snapshots->num_commits,
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

    if (DEBUG_REPO_GSL_LEVEL_2)
        knd_log(".. include file name: \"%.*s\" [%zu]", (int)name_size, name, name_size);
    if (!name_size) return make_gsl_err(gsl_FORMAT);

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
    struct kndConcFolder *folder, *folders;
    const char *c;
    char *rec;
    char **recs;
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

    if (DEBUG_REPO_GSL_LEVEL_3)
        knd_log(".. reading GSL {file %.*s} {content-type %d}",
                out->buf_size, out->buf, content_type);

    file_out->reset(file_out);
    err = file_out->write_file_content(file_out, (const char*)out->buf);
    if (err) {
        knd_log("failed to read GSL file \"%.*s\"", out->buf_size, out->buf);
        return err;
    }

    // TODO: find another place for storage
    rec = malloc(file_out->buf_size + 1);
    if (!rec) return knd_NOMEM;
    memcpy(rec, file_out->buf, file_out->buf_size);
    rec[file_out->buf_size] = '\0';

    recs = (char**)realloc(repo->source_files, (repo->num_source_files + 1) * sizeof(char*));
    if (!recs) return knd_NOMEM;
    recs[repo->num_source_files] = rec;

    repo->source_files = recs;
    repo->num_source_files++;

    if (DEBUG_REPO_GSL_LEVEL_3)
        knd_log("== total GSL files: %zu", repo->num_source_files);

    task->input = rec;
    task->input_size = file_out->buf_size;

    /* actual parsing */
    err = parse_GSL(task, (const char*)rec, &chunk_size);
    if (err) {
        knd_log("-- parsing of \"%.*s\" failed, err: %d", out->buf_size, out->buf, err);
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

static int index_classes(struct kndRepo *self, struct kndTask *task)
{
    struct kndClass *c;
    struct kndClassEntry *entry;
    struct kndSharedDictItem *item;
    struct kndSharedDict *name_idx = self->class_name_idx;
    struct kndSharedSet *class_idx = self->class_idx;
    int err;

    if (DEBUG_REPO_GSL_LEVEL_2)
        knd_log(".. indexing classes in \"%.*s\"..", self->name_size, self->name);

    // TODO iterate func
    for (size_t i = 0; i < name_idx->size; i++) {
        item = atomic_load_explicit(&name_idx->hash_array[i], memory_order_relaxed);
        for (; item; item = item->next) {
            entry = item->data;
            c = atomic_load_explicit(&entry->class, memory_order_relaxed);
            if (!c) {
                knd_log("-- unresolved class entry: %.*s", entry->name_size, entry->name);
                return knd_FAIL;
            }
            err = knd_class_index(c, task);
            if (err) {
                knd_log("failed to index the \"%.*s\" class", c->entry->name_size, c->entry->name);
                return err;
            }
            err = knd_shared_set_add(class_idx, entry->id, entry->id_size, (void*)entry);
            if (err) return err;
        }
    }
    return knd_OK;
}

static int resolve_classes(struct kndRepo *self, struct kndTask *task)
{
    struct kndClass *c;
    struct kndClassEntry *entry;
    struct kndSharedDictItem *item;
    struct kndSharedDict *name_idx = self->class_name_idx;
    int err;

    if (DEBUG_REPO_GSL_LEVEL_2)
        knd_log(".. resolving classes in \"%.*s\"..", self->name_size, self->name);

    // TODO: iterate func in kndSharedDict
    for (size_t i = 0; i < name_idx->size; i++) {
        item = atomic_load_explicit(&name_idx->hash_array[i], memory_order_relaxed);
        for (; item; item = item->next) {
            entry = item->data;
            if (!entry->class) {
                knd_log("-- unresolved class entry: %.*s", entry->name_size, entry->name);
                return knd_FAIL;
            }
            c = entry->class;
            if (c->is_resolved) continue;

            err = knd_class_resolve(c, task);
            KND_TASK_ERR("failed to resolve {class %.*s}", c->entry->name_size, c->entry->name);
            if (DEBUG_REPO_GSL_LEVEL_2)
                c->str(c, 1);
        }
    }
    return knd_OK;
}

static int resolve_procs(struct kndRepo *self, struct kndTask *task)
{
    struct kndProcEntry *entry;
    struct kndSharedDictItem *item;
    struct kndSharedDict *proc_name_idx = self->proc_name_idx;
    struct kndSet *proc_idx = self->proc_idx;
    int err;

    if (DEBUG_REPO_GSL_LEVEL_2)
        knd_log(".. resolving procs of repo \"%.*s\"..",
                self->name_size, self->name);

    for (size_t i = 0; i < proc_name_idx->size; i++) {
        item = atomic_load_explicit(&proc_name_idx->hash_array[i], memory_order_relaxed);
        for (; item; item = item->next) {
            entry = item->data;

            if (entry->proc->is_resolved) {
                continue;
            }

            err = knd_proc_resolve(entry->proc, task);
            if (err) {
                knd_log("-- couldn't resolve the \"%.*s\" proc",
                        entry->proc->name_size, entry->proc->name);
                return err;
            }
            entry->numid = atomic_fetch_add_explicit(&self->proc_id_count, 1, \
                                                     memory_order_relaxed);
            entry->numid++;
            knd_uid_create(entry->numid, entry->id, &entry->id_size);
            
            err = proc_idx->add(proc_idx,
                                entry->id, entry->id_size, (void*)entry);
            if (err) return err;
            if (DEBUG_REPO_GSL_LEVEL_2) {
                knd_proc_str(entry->proc, 1);
            }
        }
    }
    /*for (size_t i = 0; i < proc_name_idx->size; i++) {
        item = atomic_load_explicit(&proc_name_idx->hash_array[i],
                                    memory_order_relaxed);
        for (; item; item = item->next) {
            entry = item->data;
            if (!entry->proc->is_computed) {
                err = knd_proc_compute(entry->proc, task);
                if (err) {
                    knd_log("-- couldn't compute the \"%.*s\" proc",
                            entry->proc->name_size, entry->proc->name);
                    return err;
                }
            }
        }
        }*/

    return knd_OK;
}

static int iterate_class_insts(struct kndClass *c, struct kndTask *task)
{
    struct kndClassInstEntry *entry;
    struct kndSharedDictItem *item;
    struct kndSharedDict *name_idx = c->inst_name_idx;
    int err;

    if (DEBUG_REPO_GSL_LEVEL_2)
        knd_log(".. resolving insts of {class %.*s}..", c->name_size, c->name);

    // TODO: iterate func in kndSharedDict
    for (size_t i = 0; i < name_idx->size; i++) {
        item = atomic_load_explicit(&name_idx->hash_array[i], memory_order_relaxed);
        for (; item; item = item->next) {
            entry = item->data;

            err = knd_class_inst_resolve(entry->inst, task);
            KND_TASK_ERR("failed to resolve {class %.*s {inst %.*s}}",
                         c->name_size, c->name, entry->name_size, entry->name);
        }
    }
    return knd_OK;
}

static int resolve_class_insts(struct kndRepo *self, struct kndTask *task)
{
    struct kndClass *c;
    struct kndClassEntry *entry;
    struct kndSharedDictItem *item;
    struct kndSharedDict *name_idx = self->class_name_idx;
    int err;

    if (DEBUG_REPO_GSL_LEVEL_2)
        knd_log(".. resolving class instances in {repo %.*s}..", self->name_size, self->name);

    // TODO: iterate func in kndSharedDict
    for (size_t i = 0; i < name_idx->size; i++) {
        item = atomic_load_explicit(&name_idx->hash_array[i], memory_order_relaxed);
        for (; item; item = item->next) {
            entry = item->data;
            if (!entry->class) {
                knd_log("-- unresolved class entry: %.*s", entry->name_size, entry->name);
                return knd_FAIL;
            }
            c = entry->class;
            if (!c->inst_name_idx) continue;

            if (DEBUG_REPO_GSL_LEVEL_3) {
                knd_log(".. resolving insts of {class %.*s}..",
                        entry->name_size, entry->name);
                c->str(c, 1);
            }
            err = iterate_class_insts(c, task);
            KND_TASK_ERR("failed to iterate insts of class %.*s",
                         entry->name_size, entry->name);
        }
    }
    return knd_OK;
}

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
    struct kndSharedDict *name_idx = self->class_name_idx;
    int err;

    if (DEBUG_REPO_GSL_LEVEL_TMP)
        knd_log(".. indexing class instances in {repo %.*s}..", self->name_size, self->name);

    // TODO: iterate func in kndSharedDict
    for (size_t i = 0; i < name_idx->size; i++) {
        items = atomic_load_explicit(&name_idx->hash_array[i], memory_order_relaxed);
        FOREACH (item, items) {
            entry = item->data;
            if (!entry->class) {
                knd_log("-- unresolved class entry: %.*s", entry->name_size, entry->name);
                return knd_FAIL;
            }
            c = entry->class;
            if (!c->inst_name_idx) continue;

            if (DEBUG_REPO_GSL_LEVEL_TMP) {
                knd_log(".. indexing insts of {class %.*s}..",
                        entry->name_size, entry->name);
            }
            err = index_class_insts(c, task);
            KND_TASK_ERR("failed to iterate insts of class %.*s",
                         entry->name_size, entry->name);

        }
    }
    return knd_OK;
}

int knd_repo_read_source_files(struct kndRepo *self, struct kndTask *task)
{
    int err;

    if (DEBUG_REPO_GSL_LEVEL_TMP)
        knd_log(".. initial loading of schema files");

    /* read a system-wide schema */
    task->type = KND_BULK_LOAD_STATE;
    err = read_GSL_file(self, NULL, "index", strlen("index"), KND_GSL_SCHEMA, task);
    KND_TASK_ERR("schema import failed");

    err = resolve_classes(self, task);
    KND_TASK_ERR("class resolving failed");
        
    err = resolve_procs(self, task);
    KND_TASK_ERR("proc resolving failed");

    err = index_classes(self, task);
    KND_TASK_ERR("class indexing failed");

    if (self->data_path_size) {
        if (DEBUG_REPO_GSL_LEVEL_TMP)
            knd_log(".. initial loading of data files");
        task->type = KND_BULK_LOAD_STATE;
        err = read_GSL_file(self, NULL, "index", strlen("index"), KND_GSL_INIT_DATA, task);
        KND_TASK_ERR("init data import failed");

        err = resolve_class_insts(self, task);
        KND_TASK_ERR("class insts resolving failed");

        err = index_repo_class_insts(self, task);
        KND_TASK_ERR("class insts indexing failed");
    }
    return knd_OK;
}

