#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>

#include "knd_text.h"
#include "knd_task.h"
#include "knd_repo.h"
#include "knd_shared_set.h"
#include "knd_shared_dict.h"
#include "knd_user.h"
#include "knd_class.h"
#include "knd_proc.h"
#include "knd_mempool.h"
#include "knd_output.h"
#include "knd_utils.h"

#define DEBUG_TEXT_GSP_LEVEL_1 0
#define DEBUG_TEXT_GSP_LEVEL_2 0
#define DEBUG_TEXT_GSP_LEVEL_TMP 1

int knd_charseq_marshall(void *elem, size_t *output_size, struct kndTask *task)
{
    struct kndCharSeq *seq = elem;
    struct kndOutput *out = task->out;
    size_t orig_size = out->buf_size;

    OUT(seq->val, seq->val_size);

    if (DEBUG_TEXT_GSP_LEVEL_2)
        knd_log("** %zu => \"%.*s\" (size:%zu)",  seq->numid,
                seq->val_size, seq->val, out->buf_size - orig_size);

    *output_size = out->buf_size - orig_size;
    return knd_OK;
}

int knd_charseq_unmarshall(const char *elem_id, size_t elem_id_size,
                           const char *val, size_t val_size, void **result, struct kndTask *task)
{
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndCharSeq *seq;
    struct kndSharedDict *str_dict = task->repo->str_dict;
    int err;

    if (DEBUG_TEXT_GSP_LEVEL_2)
        knd_log("charseq \"%.*s\" => \"%.*s\"", elem_id_size, elem_id, val_size, val);

    err = knd_charseq_new(mempool, &seq);
    KND_TASK_ERR("charseq alloc failed");
    seq->val = val;
    seq->val_size = val_size;

    err = knd_shared_set_add(task->repo->str_idx, elem_id, elem_id_size, (void*)seq);
    KND_TASK_ERR("failed to register charseq \"%.*s\" (err:%s)", val_size, val, knd_err_names[err]);

    err = knd_shared_dict_set(str_dict, val, val_size, (void*)seq, mempool, NULL, &seq->item, false);
    KND_TASK_ERR("failed to register charseq \"%.*s\" in str dict (err:%s)", val_size, val, knd_err_names[err]);
    
    *result = seq;
    return knd_OK;
}

static int export_declars(struct kndClassDeclar *decl, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndClassInstEntry *entry;
    int err;
    for (; decl; decl = decl->next) {
        OUT("{class ", strlen("{class "));
        OUT(decl->entry->name, decl->entry->name_size);

        for (entry = decl->insts; entry; entry = entry->next) {
            err = knd_class_inst_export_GSL(entry->inst, false, KND_CREATED, task, 0);
            KND_TASK_ERR("failed to export class inst GSL");
        }
        OUT("}", 1);
    }
    return knd_OK;
}

#if 0
static int export_proc_declars(struct kndProcDeclar *decl, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndProcInstEntry *entry;
    int err;

    for (; decl; decl = decl->next) {
        err = out->write(out, "{proc ", strlen("{proc "));                        RET_ERR();
        err = out->write(out, decl->entry->name, decl->entry->name_size);         RET_ERR();

        for (entry = decl->insts; entry; entry = entry->next) {
            err = knd_proc_inst_export_GSL(entry->inst, false, KND_CREATED, task, 0);    RET_ERR();
        }
        err = out->writec(out, '}');                                              RET_ERR();
    }
    return knd_OK;
}
#endif

static int stm_export_GSP(struct kndStatement *stm, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;

    OUT("{stm ", strlen("{stm "));
    OUT(stm->schema_name, stm->schema_name_size);

    if (stm->declars) {
        err = export_declars(stm->declars, task);                      RET_ERR();
    }

    //if (stm->proc_declars) {
    //    err = export_proc_declars(stm->proc_declars, task);                        RET_ERR();
    //}
    OUT("}", 1);

    return knd_OK;
}

static int sent_export_GSP(struct kndSentence *sent, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;
    err = out->writec(out, '{');                        RET_ERR();
    err = out->write(out, sent->seq->val, sent->seq->val_size);   RET_ERR();

    if (sent->stm) {
        err = stm_export_GSP(sent->stm, task);           RET_ERR();
    }

    err = out->writec(out, '}');                        RET_ERR();
   
    return knd_OK;
}

int knd_text_export_GSP(struct kndText *self, struct kndTask *task)
{
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size;
    struct kndOutput *out = task->out;
    struct kndPar *par;
    struct kndSentence *sent;
    struct kndState *state;
    struct kndCharSeq *seq = self->seq;
    struct kndText *trn;
    int err;

    state = atomic_load_explicit(&self->states, memory_order_relaxed);
    if (seq && state) {
        seq->val = state->val->val;
        seq->val_size = state->val->val_size;
        return knd_OK;
    }

    if (seq) {
       if (DEBUG_TEXT_GSP_LEVEL_2)
           knd_log("** %zu => \"%.*s\" (size:%zu)",  seq->numid,
                   seq->val_size, seq->val, seq->val_size);
     
        knd_uid_create(seq->numid, idbuf, &idbuf_size);
        OUT(idbuf, idbuf_size);
    }

    if (self->locale_size) {
        OUT("{", 1);
        OUT("_lang ", strlen("_lang "));
        OUT(self->locale, self->locale_size);
        OUT("}", 1);
    }

    if (self->trs) {
        OUT("[trn", strlen("[trn"));
        FOREACH (trn, self->trs) {
            OUT("{", 1);
            OUT(trn->locale, trn->locale_size);
            OUT("{t ", strlen("{t "));

            knd_uid_create(trn->seq->numid, idbuf, &idbuf_size);
            OUT(idbuf, idbuf_size);

            OUT("}", 1);
            OUT("}", 1);
        }
        OUT("]", 1);
    }

    if (self->num_pars) {
        OUT("[p", strlen("[p"));
        FOREACH (par, self->pars) {
            OUT("{", 1);
            OUT("[s", strlen("[s"));
            FOREACH (sent, par->sents) {
                err = sent_export_GSP(sent, task);
                KND_TASK_ERR("failed to export sent GSP");
            }
            OUT("]", 1);
            OUT("}", 1);
        }
        OUT("]", 1);
    }
    return knd_OK;
}

int knd_par_export_GSP(struct kndPar *par, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;

    err = out->writef(out, "{%zu", par->numid);         RET_ERR();

    //if (par->declars) {
    //    err = export_declars(par->declars, task);
    //    KND_TASK_ERR("failed to export class declars");
    //}

    //if (par->proc_declars) {
    //    err = export_proc_declars(par->proc_declars, task);                        RET_ERR();
    //}

    err = out->writec(out, '}');                        RET_ERR();
    return knd_OK;
}

#if 0
static int export_class_idx_GSP(struct kndClassIdx *self, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndTextLoc *loc;
    struct kndClassRef *ref;
    struct kndClassRef *sub_idxs = atomic_load_explicit(&self->children, memory_order_relaxed);
    int err;

    OUT("{tot ", strlen("{tot "));
    err = out->writef(out, "%zu", self->total_locs);
    RET_ERR();
    OUT("}", 1);

    OUT("{num-children ", strlen("{num-children "));
    err = out->writef(out, "%zu", self->num_children);
    RET_ERR();
    OUT("}", 1);

    if (sub_idxs) {
        OUT("[sub ", strlen("[sub "));
        FOREACH (ref, sub_idxs) {
            OUT("{", 1);
            OUT(ref->entry->name, ref->entry->name_size);
            //err = export_class_idx_GSP(ref->idx, task);
            //KND_TASK_ERR("failed to export class idx");
            OUT("}", 1);
        }
        OUT("]", 1);
    }

    if (self->locs) {
        OUT("[loc ", strlen("[loc "));
        FOREACH (loc, self->locs) {
            OUT("{", 1);
            err = out->writef(out, "%zu:%zu", loc->par_id, loc->sent_id);         RET_ERR();
            OUT("}", 1);
        }
        OUT("]", 1);
    }
    return knd_OK;
}
#endif

