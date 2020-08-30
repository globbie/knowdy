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
                            struct kndClassInst *inst, struct kndTask *task)
{
    struct kndTextIdx *idx = NULL;
    struct kndTextLoc *loc = NULL;
    struct kndMemPool *mempool = task->mempool;
    if (task->user_ctx)
        mempool = task->shard->user->mempool;
    int err;

    if (DEBUG_TEXT_IDX_LEVEL_3) {
        knd_log(">> index class inst of \"%.*s\" (repo:%.*s)  => %.*s::%.*s par:%zu sent:%zu decl:%.*s",
                entry->name_size, entry->name,  entry->repo->name_size, entry->repo->name,
                inst->blueprint->name_size, inst->blueprint->name, inst->name_size, inst->name,
                par->numid, sent->numid, decl->entry->name_size, decl->entry->name);
        knd_class_entry_str(entry, 1);
    }

    for (idx = entry->text_idxs; idx; idx = idx->next) {
        if (idx->entry == inst->blueprint) break;
    }

    if (!idx) {
        err = knd_text_idx_new(mempool, &idx);
        if (err) return err;
        idx->entry = inst->blueprint;
        idx->attr = var->attr;
        idx->next = entry->text_idxs;
        entry->text_idxs = idx;
    }

    err = knd_text_loc_new(mempool, &loc);
    if (err) return err;
    loc->type = KND_STATE_CLASS_INST;
    loc->src = inst;
    loc->par_id = par->numid;
    loc->sent_id = sent->numid;
    loc->obj = (void*)decl->entry;

    // knd_class_entry_str(entry, 1);

    if (idx->num_locs < KND_MAX_TEXT_LOCS) {
        loc->next = idx->locs;
        idx->locs = loc;
        idx->num_locs++;
        return knd_OK;
    }

    // construct sets

    
    return knd_OK;
}

static int index_class_declar(struct kndClassDeclaration *decl, struct kndSentence *sent, struct kndPar *par, 
                              struct kndAttrVar *var, struct kndClassInst *inst, struct kndRepo *repo, struct kndTask *task)
{
    struct kndClassRef *ref;
    struct kndAttrIdx *idx = inst->entry->attr_idxs;
    struct kndMemPool *mempool = task->mempool;
    if (task->user_ctx)
        mempool = task->shard->user->mempool;
    struct kndTextLoc *loc;
    struct kndClassEntry *entry;
    int err;

    /* update local inst index */
    // TODO
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

    /* global concept index */
    entry = decl->entry;
    if (decl->entry->repo != repo) {
        err = knd_class_entry_clone(decl->entry, repo, &entry, task);
        KND_TASK_ERR("failed to clone class entry");
        decl->entry = entry;
    }
    
    err = index_class_inst(entry, decl, sent, par, var, inst, task);
    KND_TASK_ERR("failed to index text class inst");

    for (ref = decl->entry->ancestors; ref; ref = ref->next) {
        err = knd_get_class_entry(repo, ref->entry->name, ref->entry->name_size, &entry, task);
        KND_TASK_ERR("class \"%.*s\" not found in repo %.*s", ref->entry->name_size, ref->entry->name,
                     repo->name_size, repo->name);

        if (entry->repo != repo) {
            err = knd_class_entry_clone(ref->entry, repo, &entry, task);
            KND_TASK_ERR("failed to clone class entry");
        }

        err = index_class_inst(entry, decl, sent, par, var, inst, task);
        if (err) return err;

        /* local idx */
        if (idx) {
            // knd_log(">> update local attr idx with \"%.*s\"", ref->entry->name_size, ref->entry->name);
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
    }
    return knd_OK;
}

int knd_text_index(struct kndText *self, struct kndRepo *repo, struct kndTask *task)
{
    struct kndAttrVar *var = self->attr_var;
    struct kndClassInst *inst = var->class_var->inst;
    struct kndMemPool *mempool = task->mempool;
    if (task->user_ctx)
        mempool = task->shard->user->mempool;
    struct kndPar *par;
    struct kndSentence *sent;
    struct kndClassDeclaration *decl;
    struct kndProcDeclaration *proc_decl;
    struct kndAttrIdx *idx;
    int err;

    // max threshold
    if (self->total_props) {
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
                //err = index_proc_declar(decl, sent, par, var, inst, repo, task);
                //KND_TASK_ERR("faile to index proc declar");
            }
        }
    }
    return knd_OK;
}
