#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>

#include "knd_text.h"
#include "knd_task.h"
#include "knd_repo.h"
#include "knd_class.h"
#include "knd_attr.h"
#include "knd_proc.h"
#include "knd_shard.h"
#include "knd_user.h"
#include "knd_utils.h"
#include "knd_mempool.h"
#include "knd_output.h"

#define DEBUG_TEXT_IDX_LEVEL_0 0
#define DEBUG_TEXT_IDX_LEVEL_1 0
#define DEBUG_TEXT_IDX_LEVEL_2 0
#define DEBUG_TEXT_IDX_LEVEL_3 0
#define DEBUG_TEXT_IDX_LEVEL_TMP 1

static int index_class_inst(struct kndClassEntry *entry, struct kndClassDeclaration *decl,
                            struct kndSentence *sent, struct kndPar *par, struct kndAttrVar *var,
                            struct kndClassInst *inst, struct kndClassIdx **result, struct kndTask *task)
{
    struct kndClassRef *ref = NULL;
    struct kndTextLoc *loc = NULL;
    struct kndMemPool *mempool = task->user_ctx ? task->user_ctx->mempool : task->mempool;
    int err;

    if (DEBUG_TEXT_IDX_LEVEL_2) {
        knd_log(">> index class inst of \"%.*s\" (repo:%.*s)  => %.*s::%.*s par:%zu sent:%zu decl:%.*s",
                entry->name_size, entry->name,  entry->repo->name_size, entry->repo->name,
                inst->blueprint->name_size, inst->blueprint->name, inst->name_size, inst->name,
                par->numid, sent->numid, decl->entry->name_size, decl->entry->name);
        knd_class_entry_str(entry, 1);
    }

    FOREACH (ref, entry->text_idxs) {
        if (ref->entry == inst->blueprint) break;
    }
    if (!ref) {
        err = knd_class_ref_new(mempool, &ref);
        if (err) return err;
        ref->entry = inst->blueprint;
        ref->attr = var->attr;
        ref->next = entry->text_idxs;
        err = knd_class_idx_new(mempool, &ref->idx);
        ref->idx->entry = entry;
        if (err) return err;

        // TODO atomic
        entry->text_idxs = ref;
    }

    *result = ref->idx;

    err = knd_text_loc_new(mempool, &loc);
    if (err) return err;
    loc->type = KND_STATE_CLASS_INST;
    loc->src = inst;
    loc->par_id = par->numid;
    loc->sent_id = sent->numid;
    loc->obj = (void*)decl->entry;

    if (ref->idx->num_locs < KND_MAX_TEXT_LOCS) {
        loc->next = ref->idx->locs;
        ref->idx->locs = loc;
        atomic_fetch_add_explicit(&ref->idx->num_locs, 1, memory_order_relaxed);
        return knd_OK;
    }

    // construct sets
    
    return knd_OK;
}

static int append_child_idx(struct kndClassIdx *idx, struct kndClassIdx *child_idx, struct kndTask *task)
{
    struct kndMemPool *mempool = task->user_ctx ? task->user_ctx->mempool : task->mempool;
    struct kndClassRef *orig_children, *ref = NULL;
    size_t num_children;
    int err;

    do {
        // CAS failed: concurrent competitor was quicker than us
        if (ref) {
            // free resources
        }
        orig_children = atomic_load_explicit(&idx->children, memory_order_relaxed);
        num_children = atomic_load_explicit(&idx->num_children, memory_order_relaxed);

        knd_log(">>  \"%.*s\" %p (num children:%zu)  => \"%.*s\"",
                idx->entry->name_size, idx->entry->name, idx, num_children,
                child_idx->entry->name_size, child_idx->entry->name);
        
        FOREACH (ref, orig_children) {
            // idx is already registered
            if (ref->entry == child_idx->entry) return knd_OK;
        }
        err = knd_class_ref_new(mempool, &ref);
        if (err) return err;
        ref->entry = child_idx->entry;
        ref->idx =   child_idx;
        ref->next =  orig_children;
    } while (!atomic_compare_exchange_weak(&idx->children, &orig_children, ref));

    atomic_fetch_add_explicit(&idx->num_children, 1, memory_order_relaxed);
    return knd_OK;
}

static int get_class_idx(struct kndClassEntry *entry, struct kndAttrVar *var, struct kndClassInst *src,
                         struct kndClassIdx **result, struct kndTask *task)
{
    struct kndMemPool *mempool = task->user_ctx ? task->user_ctx->mempool : task->mempool;
    struct kndClassRef *orig_idxs, *ref = NULL;
    int err;

    do {
        // CAS failed: concurrent competitor was quicker than us
        if (ref) {
            // free resources
        }
        orig_idxs = atomic_load_explicit(&entry->text_idxs, memory_order_relaxed);
        FOREACH (ref, orig_idxs) {
            if (ref->entry == src->blueprint) break;
        }
        if (!ref) {
            err = knd_class_ref_new(mempool, &ref);
            if (err) return err;
            ref->entry = src->blueprint;
            ref->attr = var->attr;
            ref->next = orig_idxs;

            err = knd_class_idx_new(mempool, &ref->idx);
            ref->idx->entry = entry;
        }
    }
    while (!atomic_compare_exchange_weak(&entry->text_idxs, &orig_idxs, ref));

    *result = ref->idx;
    return knd_OK;
}

static int update_ancestor_idx(struct kndClassEntry *base, struct kndClassDeclaration *decl,
                               struct kndAttrVar *var, struct kndClassInst *src,
                               struct kndClassIdx *term_idx, struct kndTask *task)
{
    struct kndClassEntry *entry = decl->entry;
    struct kndClassRef *ref;
    struct kndClassIdx *idx, *child_idx;
    int err;

    if (DEBUG_TEXT_IDX_LEVEL_2)
        knd_log(">> ancestor \"%.*s\" to update its class idx", base->name_size, base->name);

    err = get_class_idx(base, var, src, &idx, task);
    KND_TASK_ERR("failed to get a class idx");

    FOREACH (ref, base->children) {
        if (ref->class == entry->class) {
            err = append_child_idx(idx, term_idx, task);
            KND_TASK_ERR("failed to append terminal child idx");
            break;
        }
        err = knd_is_base(ref->class, entry->class);
        if (err) continue;

        err = get_class_idx(ref->entry, var, src, &child_idx, task);
        KND_TASK_ERR("failed to get a class idx");

        err = append_child_idx(idx, child_idx, task);
        KND_TASK_ERR("failed to append child");
    }
     // register terminal loc
    atomic_fetch_add_explicit(&idx->total_locs, 1, memory_order_relaxed);
   
    return knd_OK;
}

static int index_class_declar(struct kndClassDeclaration *decl, struct kndSentence *sent, struct kndPar *par, 
                              struct kndAttrVar *var, struct kndClassInst *inst, struct kndRepo *repo, struct kndTask *task)
{
    struct kndClassRef *ref;
    struct kndClassIdx *idx;
    // struct kndMemPool *mempool = task->user_ctx ? task->user_ctx->mempool : task->mempool;
    //struct kndTextLoc *loc;
    struct kndClassEntry *entry;
    int err;

    /* update local inst index */
    // TODO
#if 0
    if (idx) {
        knd_log(">> \"%.*\" (repo:%.*s) to update local attr idx with \"%.*s\"",
                inst->name_size, inst->name, inst->blueprint->repo->name_size,
                inst->blueprint->repo->name,
                decl->entry->name_size, decl->entry->name);
        err = knd_text_loc_new(mempool, &loc);
        if (err) return err;
        loc->type = KND_STATE_CLASS_INST;
        loc->src = inst;
        loc->par_id = par->numid;
        loc->sent_id = sent->numid;
        loc->obj = (void*)decl->entry;
        
        if (idx->num_locs < KND_MAX_TEXT_LOCS) {
            loc->next = idx->locs;
            idx->locs = loc;
            idx->num_locs++;
        }
    }
#endif
    
    /* global concept index */
    entry = decl->entry;
    if (decl->entry->repo != repo) {
        err = knd_get_class_entry(repo, decl->entry->name, decl->entry->name_size, false, &entry, task);
        if (err) {
            err = knd_class_entry_clone(decl->entry, repo, &entry, task);
            KND_TASK_ERR("failed to clone class entry");
        }
        decl->entry = entry;
    }
    
    err = index_class_inst(entry, decl, sent, par, var, inst, &idx, task);
    KND_TASK_ERR("failed to index text class inst");

    for (ref = decl->entry->ancestors; ref; ref = ref->next) {
        if (ref->entry->repo != repo) {
            err = knd_get_class_entry(repo, ref->entry->name, ref->entry->name_size, false, &entry, task);
            if (err) {
                err = knd_class_entry_clone(ref->entry, repo, &entry, task);
                KND_TASK_ERR("failed to clone class entry");
            }
            ref->entry = entry;
        }
        err = update_ancestor_idx(ref->entry, decl, var, inst, idx, task);
        if (err) return err;
    }
    return knd_OK;
}

static int index_proc_inst(struct kndProcEntry *entry, struct kndProcDeclaration *decl,
                           struct kndSentence *sent, struct kndPar *par, struct kndAttrVar *var,
                           struct kndClassInst *inst, struct kndTask *task)
{
    struct kndClassRef *ref = NULL;
    struct kndTextLoc *loc = NULL;
    struct kndMemPool *mempool = task->user_ctx ? task->user_ctx->mempool : task->mempool;
    int err;

    if (DEBUG_TEXT_IDX_LEVEL_3) {
        knd_log(">> index proc inst of \"%.*s\" (repo:%.*s)  => %.*s::%.*s par:%zu sent:%zu decl:%.*s",
                entry->name_size, entry->name,  entry->repo->name_size, entry->repo->name,
                inst->blueprint->name_size, inst->blueprint->name, inst->name_size, inst->name,
                par->numid, sent->numid, decl->entry->name_size, decl->entry->name);
    }

    FOREACH (ref, entry->text_idxs) {
        if (ref->entry == inst->blueprint) break;
    }

    if (!ref) {
        err = knd_class_ref_new(mempool, &ref);
        if (err) return err;
        ref->entry = inst->blueprint;
        ref->attr = var->attr;
        ref->next = entry->text_idxs;
        entry->text_idxs = ref;
    }

    err = knd_text_loc_new(mempool, &loc);
    if (err) return err;
    loc->type = KND_STATE_PROC_INST;
    loc->src = inst;
    loc->par_id = par->numid;
    loc->sent_id = sent->numid;
    loc->obj = (void*)decl->entry;

    // knd_proc_entry_str(entry, 1);
    /*if (idx->num_locs < KND_MAX_TEXT_LOCS) {
        loc->next = idx->locs;
        idx->locs = loc;
        idx->num_locs++;
        return knd_OK;
    }
    */
    // construct sets

    
    return knd_OK;
}

static int index_proc_declar(struct kndProcDeclaration *decl, struct kndSentence *sent, struct kndPar *par, 
                             struct kndAttrVar *var, struct kndClassInst *inst, struct kndRepo *repo, struct kndTask *task)
{
    struct kndProcRef *ref;
    //struct kndAttrIdx *idx = inst->entry->attr_idxs;
    //struct kndMemPool *mempool = task->user_ctx ? task->shard->user->mempool : task->mempool;
    //struct kndTextLoc *loc;
    struct kndProcEntry *entry;
    int err;

    /* update local inst index */
    // TODO
#if 0
    if (idx) {
        knd_log(">> \"%.*\" (repo:%.*s) to update local attr idx with \"%.*s\"",
                inst->name_size, inst->name, inst->blueprint->repo->name_size,
                inst->blueprint->repo->name,
                decl->entry->name_size, decl->entry->name);
        err = knd_text_loc_new(mempool, &loc);
        if (err) return err;
        loc->type = KND_STATE_PROC_INST;
        loc->src = inst;
        loc->par_id = par->numid;
        loc->sent_id = sent->numid;
        loc->obj = (void*)decl->entry;
        
        if (idx->num_locs < KND_MAX_TEXT_LOCS) {
            loc->next = idx->locs;
            idx->locs = loc;
            idx->num_locs++;
        }
    }
#endif

    /* global concept index */
    entry = decl->entry;
    if (decl->entry->repo != repo) {
        err = knd_proc_entry_clone(decl->entry, repo, &entry, task);
        KND_TASK_ERR("failed to clone proc entry");
        decl->entry = entry;
    }
    err = index_proc_inst(entry, decl, sent, par, var, inst, task);
    KND_TASK_ERR("failed to index text proc inst");

    for (ref = decl->entry->ancestors; ref; ref = ref->next) {
        err = knd_get_proc_entry(repo, ref->entry->name, ref->entry->name_size, &entry, task);
        KND_TASK_ERR("proc \"%.*s\" not found in repo %.*s", ref->entry->name_size, ref->entry->name,
                     repo->name_size, repo->name);

        if (entry->repo != repo) {
            err = knd_proc_entry_clone(ref->entry, repo, &entry, task);
            KND_TASK_ERR("failed to clone proc entry");
        }

        
    }
    return knd_OK;
}

int knd_text_index(struct kndText *self, struct kndRepo *repo, struct kndTask *task)
{
    struct kndAttrVar *var = self->attr_var;
    struct kndClassInst *inst = var->class_var->inst;
    struct kndMemPool *mempool = task->user_ctx ? task->user_ctx->mempool : task->mempool;
    struct kndPar *par;
    struct kndSentence *sent;
    struct kndClassDeclaration *decl;
    struct kndProcDeclaration *proc_decl;
    struct kndAttrIdx *idx;
    int err;

    // max threshold
    if (self->num_pars) {
        err = knd_attr_idx_new(mempool, &idx);
        if (err) return err;
        idx->attr = var->attr;
        idx->next = inst->entry->attr_idxs;
        inst->entry->attr_idxs = idx;
    }

    for (par = self->pars; par; par = par->next) {
        for (sent = par->sents; sent; sent = sent->next) {
            if (!sent->stm) continue;

            knd_log(">> repo \"%.*s\" to add a text idx rec: %.*s/%.*s/%.*s/P:%zu/S:%zu  \"%.*s\"",
                    repo->name_size, repo->name, inst->blueprint->name_size, inst->blueprint->name,
                    inst->name_size, inst->name,
                    var->name_size, var->name, par->numid, sent->numid, sent->seq_size, sent->seq);
            
            for (decl = sent->stm->class_declars; decl; decl = decl->next) {
                err = index_class_declar(decl, sent, par, var, inst, repo, task);
                KND_TASK_ERR("failed to index class declar");
            }

            for (proc_decl = sent->stm->proc_declars; proc_decl; proc_decl = proc_decl->next) {
                err = index_proc_declar(proc_decl, sent, par, var, inst, repo, task);
                KND_TASK_ERR("failed to index proc declar");
            }
        }
    }
    return knd_OK;
}
