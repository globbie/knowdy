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

#define DEBUG_REPO_COMMIT_LEVEL_0 0
#define DEBUG_REPO_COMMIT_LEVEL_1 0
#define DEBUG_REPO_COMMIT_LEVEL_2 0
#define DEBUG_REPO_COMMIT_LEVEL_3 0
#define DEBUG_REPO_COMMIT_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepo *repo;
};

static int export_commit_GSL(struct kndRepo *self, struct kndCommit *commit, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndStateRef *ref;
    struct kndState *state;
    struct kndClassEntry *entry;
    struct kndClassInst *user_inst;
    int err;

    out->reset(out);
    task->ctx->max_depth = KND_MAX_DEPTH;
    OUT("{task", strlen("{task"));

    switch (task->user_ctx->type) {
    case KND_USER_AUTHENTICATED:
        user_inst = task->user_ctx->inst;
        OUT("{user ", strlen("{user "));
        OUT(user_inst->name, user_inst->name_size);
        break;
    default:
        break;
    }

    OUT("{repo ", strlen("{repo "));
    OUT(self->name, self->name_size);

    FOREACH (ref, commit->class_state_refs) {
        entry = ref->obj;
        if (!entry) continue;

        state = ref->state;

        err = out->writec(out, '{');                                              RET_ERR();
        if (state->phase == KND_CREATED) {
            err = out->writec(out, '!');                                          RET_ERR();
        }

        OUT("class ", strlen("class "));
        OUT(entry->name, entry->name_size);

        if (state->phase == KND_REMOVED) {
            OUT("_rm ", strlen("_rm"));
            err = out->writec(out, '}');                                          RET_ERR();
            continue;
        }

        if (state->phase == KND_SELECTED) {
            err = knd_class_inst_export_commit(state->children, task);
            KND_TASK_ERR("failed to export class inst commit");
        }
        OUT("}", 1);
    }    

    OUT("}", 1);
    if (task->user_ctx) {
        OUT("}", 1);
    }
    OUT("}", 1);

    return knd_OK;
}

static int check_class_conflicts(struct kndRepo *unused_var(self),
                                 struct kndCommit *new_commit, struct kndTask *unused_var(task))
{
    struct kndStateRef *ref;
    struct kndClassEntry *entry;
    struct kndState *state;
    //knd_commit_confirm confirm;
    //int err;

    FOREACH (ref, new_commit->class_state_refs) {
        entry = ref->obj;
        state = ref->state;

        if (DEBUG_REPO_COMMIT_LEVEL_2)
            knd_log(".. checking \"%.*s\" class conflicts, state phase: %d",
                    entry->name_size, entry->name, state->phase);

        switch (state->phase) {
        case KND_SELECTED:
            // TODO: check instances
            break;
        case KND_CREATED:

            // TODO: check class name idx
            // check dedups

            // knd_log(".. any new states in class name idx?");

            /*state = atomic_load_explicit(&entry->dict_item->states, memory_order_acquire);

            for (; state; state = state->next) {
                if (state->commit == new_commit) continue;

                confirm = atomic_load_explicit(&state->commit->confirm, memory_order_acquire);
                switch (confirm) {
                case KND_VALID_STATE:
                case KND_PERSISTENT_STATE:
                    atomic_store_explicit(&new_commit->confirm, KND_CONFLICT_STATE, memory_order_release);
                    err = knd_FAIL;
                    KND_TASK_ERR("%.*s class already registered", entry->name_size, entry->name);
                default:
                    break;
                }
                }*/
            
            break;
        default:
            break;
        }
    }
    return knd_OK;
}

static int check_commit_conflicts(struct kndRepo *self, struct kndCommit *commit, struct kndTask *task)
{
    struct kndCommit *head_commit = NULL;
    struct kndRepoSnapshot *snapshot;
    int err;

    if (DEBUG_REPO_COMMIT_LEVEL_TMP) {
        knd_log(".. new commit #%zu (%p) to check any commit conflicts since state #%zu",
                commit->numid, commit, commit->orig_state_id);
    }
    snapshot = task->snapshot;
    do {
        head_commit = atomic_load_explicit(&snapshot->commits, memory_order_relaxed);
        if (head_commit) {
            if (DEBUG_REPO_COMMIT_LEVEL_TMP)
                knd_log("== head commit %p #%zu", head_commit, head_commit->numid);

            commit->prev = head_commit;
            commit->numid = head_commit->numid + 1;
        } else {
            knd_log("no head commit found?");
        }
        err = check_class_conflicts(self, commit, task);
        KND_TASK_ERR("class level conflicts detected");

    } while (!atomic_compare_exchange_weak(&snapshot->commits, &head_commit, commit));

    atomic_store_explicit(&commit->confirm, KND_VALID_STATE, memory_order_release);
    atomic_fetch_add_explicit(&snapshot->num_commits, 1, memory_order_relaxed);

    if (DEBUG_REPO_COMMIT_LEVEL_TMP)
        knd_log("++ no conflicts found, commit %p #%zu confirmed!", commit, commit->numid);

    return knd_OK;
}

static int update_indices(struct kndRepo *self, struct kndCommit *commit, struct kndTask *task)
{
    struct kndStateRef *ref;
    struct kndClassEntry *entry;
    //struct kndProcEntry *proc_entry;
    struct kndRepo *repo = self;
    struct kndDict *name_idx = task->idxs.cls_name_idx;
    int err;

    if (DEBUG_REPO_COMMIT_LEVEL_2) {
        knd_log(".. commit #%zu to update the indices of %.*s [task role:%d]",
                commit->numid, self->name_size, self->name, task->role);
    }
    if (task->user_ctx) {
        repo = task->user_ctx->repo;
    }

    FOREACH (ref, commit->class_state_refs) {
        entry = ref->obj;
        if (DEBUG_REPO_COMMIT_LEVEL_2) {
            knd_log(".. idx update of {cls %.*s {phase %d}}",
                    entry->name_size, entry->name, ref->state->phase);
        }
        switch (ref->state->phase) {
        case KND_CREATED:
            if (DEBUG_REPO_COMMIT_LEVEL_3) {
                knd_log(".. class name idx of {repo %.*s} to register {class %.*s}",
                        self->name_size, self->name, entry->name_size, entry->name);
            }
            /* register new class */
            err = knd_dict_set(name_idx, entry->name,  entry->name_size, (void*)entry, task);
            KND_TASK_ERR("failed to register {cls %.*s}", entry->name_size, entry->name);
            continue;
        case KND_REMOVED:
            entry->phase = KND_REMOVED;
            continue;
        case KND_UPDATED:
            entry->phase = KND_UPDATED;

            err = knd_class_update_indices(self, entry, ref->state, task);
            KND_TASK_ERR("failed to update indices of {cls %.*s}",
                         entry->name_size, entry->name);
            continue;
        default:
            // KND_SELECTED
            if (ref->state->children != NULL) {
                err = knd_class_inst_update_indices(self, entry, ref->state->children, task);
                KND_TASK_ERR("failed to update inst indices of {cls %.*s}",
                             entry->name_size, entry->name);
            }
            break;
        }
    }

    /*
    name_idx = task->idxs->proc_name_idx;

    FOREACH (ref, commit->proc_state_refs) {
        proc_entry = ref->obj;
        switch (ref->state->phase) {
        case KND_REMOVED:
            proc_entry->phase = KND_REMOVED;
            err = knd_shared_dict_remove(name_idx, proc_entry->name, proc_entry->name_size);
            RET_ERR();
            continue;
        case KND_UPDATED:
            proc_entry->phase = KND_UPDATED;
            continue;
        default:
            break;
        }
        err = knd_shared_dict_set(name_idx, proc_entry->name, proc_entry->name_size, (void*)proc_entry);
        RET_ERR();
        }*/
    return knd_OK;
}

static int build_journal_filename(struct kndRepoSnapshot *snapshot,
                                  char *filename, size_t *filename_size,
                                  struct kndTask *task)
{
    struct kndOutput *out = task->out;
    const char *path;
    size_t path_size;
    int err;

    out->reset(out);
    OUT(snapshot->repo->path, snapshot->repo->path_size);
    OUTF("snapshot_%zu", snapshot->numid);
    OUTF("agent_%d/", task->id);
    path = out->buf;
    path_size = out->buf_size;

    err = knd_mkpath(path, path_size, 0755, false);
    KND_TASK_ERR("mkpath %.*s failed", path_size, path);

    OUTF("journal_%zu.log", snapshot->num_journals[task->id]);

    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("journal filename too long");
    }
    memcpy(filename, out->buf, out->buf_size);
    *filename_size = out->buf_size;
    filename[out->buf_size] = '\0';
    return knd_OK;
}

static int build_commit_WAL(struct kndRepo *self, struct kndCommit *commit, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndOutput *file_out = task->file_out;
    struct kndRepoSnapshot *snapshot = task->snapshot;
    char filename[KND_PATH_SIZE + 1];
    size_t filename_size = 0;
    size_t planned_journal_size = 0;
    struct stat st;
    int err;
    
    commit->timestamp = time(NULL);
    if (DEBUG_REPO_COMMIT_LEVEL_TMP) {
        knd_log(".. kndTask #%zu to build a WAL entry {snapshot #%zu} commit #%zu)",
                task->id, snapshot->numid, commit->numid);
    }
    err = build_journal_filename(snapshot, filename, &filename_size, task);
    KND_TASK_ERR("failed to build journal filename");

    if (stat(out->buf, &st)) {
        if (DEBUG_REPO_COMMIT_LEVEL_TMP)
            knd_log(".. initializing the journal: \"%.*s\"", filename_size, filename);
        err = knd_write_file((const char*)filename, "{WAL\n", strlen("{WAL\n"));
        KND_TASK_ERR("failed writing to file %.*s", filename, filename_size);
        goto append_wal_rec;
    }

    planned_journal_size = st.st_size + out->buf_size;
    if (planned_journal_size > snapshot->max_journal_size) {
        if (DEBUG_REPO_COMMIT_LEVEL_TMP)
            knd_log("NB: journal size limit reached!");
        /* switch to a new journal */
        snapshot->num_journals[task->id]++;

        err = build_journal_filename(snapshot, filename, &filename_size, task);
        KND_TASK_ERR("failed to build journal filename");
        err = knd_write_file((const char*)filename, "{WAL\n", strlen("{WAL\n"));
        KND_TASK_ERR("failed writing to file %.*s", filename, filename_size);
    }

 append_wal_rec:

    err = export_commit_GSL(self, commit, task);
    KND_TASK_ERR("failed to export commit");

    file_out->reset(file_out);
    err = file_out->writef(file_out, "{commit %zu{_ts %zu}{_size %zu}",
                           commit->numid, (size_t)commit->timestamp, out->buf_size);
    KND_TASK_ERR("commit header output failed");

    err = file_out->write(file_out, out->buf, out->buf_size);
    KND_TASK_ERR("commit body output failed");

    err = file_out->write(file_out, "}\n", strlen("}\n"));
    KND_TASK_ERR("commit output failed");

    /*if (task->keep_local_WAL) {
        err = knd_append_file((const char*)filename, file_out->buf, file_out->buf_size);
        KND_TASK_ERR("WAL file append failed");
        atomic_store_explicit(&commit->confirm, KND_PERSISTENT_STATE, memory_order_relaxed);
        }*/
    return knd_OK;
}

int knd_confirm_commit(struct kndRepo *self, struct kndTask *task)
{
    struct kndTaskContext *ctx = task->ctx;
    struct kndCommit *commit = ctx->commit;
    int err;
    assert(commit != NULL);

    if (DEBUG_REPO_COMMIT_LEVEL_TMP) {
        knd_log(">> {repo %.*s} repo to confirm {commit #%zu}",
                self->name_size, self->name, commit->numid);
    }
    commit->repo = self;

    err = knd_commit_resolve(commit, task);
    KND_TASK_ERR("failed to resolve commit #%zu", commit->numid);

    err = knd_commit_dedup(commit, task);
    KND_TASK_ERR("failed to dedup commit #%zu", commit->numid);

    switch (task->role) {
    case KND_AGENT_ARBITER:
        err = update_indices(self, commit, task);
        KND_TASK_ERR("index update failed");

        err = check_commit_conflicts(self, commit, task);
        KND_TASK_ERR("commit conflicts detected, please get the latest repo updates");

        err = build_commit_WAL(self, commit, task);
        KND_TASK_ERR("WAL build failed");
        break;
    default:
        /* delegate commit confirmation to an Arbiter */
        err = export_commit_GSL(self, commit, task);
        KND_TASK_ERR("failed to export commit");
        ctx->phase = KND_CONFIRM_COMMIT;
    }
    return knd_OK;
}

int knd_apply_commit(void *elem, void *ctx, struct kndTask *task)
{
    struct kndCommit *commit = elem;
    struct kndUserContext *user_ctx = task->user_ctx;
    struct kndMemPool *mempool = task->mempool;
    struct kndCommit *head_commit;
    struct kndRepo *repo = task->repo;
    struct kndRepoSnapshot *snapshot = task->snapshot;
    gsl_err_t parser_err;
    size_t total_size = commit->rec_size;
    int err;

    if (DEBUG_REPO_COMMIT_LEVEL_2) {
        knd_log(".. applying {commit #%zu}", commit->numid);
    }
    task->mempool = NULL;
    knd_task_reset(task);

    task->type = KND_TASK_RESTORE;
    task->ctx->commit = commit;
    task->user_ctx = user_ctx;
    task->mempool = mempool;

    struct gslTaskSpec specs[] = {
        { .name = "commit",
          .name_size = strlen("commit"),
          .parse = knd_commit_run,
          .obj = task
        }
    };

    parser_err = gsl_parse_task(commit->rec, &total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    err = knd_commit_resolve(commit, task);
    KND_TASK_ERR("failed to resolve {commit #%zu}", commit->numid);

    err = update_indices(repo, commit, task);
    KND_TASK_ERR("index update failed");

    do {
        head_commit = atomic_load_explicit(&snapshot->commits, memory_order_acquire);
        commit->prev = head_commit;
    } while (!atomic_compare_exchange_weak(&snapshot->commits, &head_commit, commit));

    /* restore repo ref */
    task->repo = repo;
    return knd_OK;
}

int knd_repo_transfer_commits(struct kndRepo *repo, struct kndTask *unused_var(task))
{
    struct kndCommit *commit = NULL;
    struct kndRepoSnapshot *snapshot;

    assert (repo->snapshot_temp != NULL);

    snapshot = repo->snapshot;    

    if (DEBUG_REPO_COMMIT_LEVEL_TMP) {
        knd_log(".. transfer the remaining delta of latest commits in {repo %.*s}",
                repo->name_size, repo->name);
    }

    commit = atomic_load_explicit(&snapshot->commits, memory_order_relaxed);
    while (commit) {
        if (commit->numid <= snapshot->start_from_commit_id) break;

        knd_log(">> commit #%zu", commit->numid);
        //= repo->snapshot_temp
       
        commit = commit->prev;
    }    
    
    return knd_OK;
}
