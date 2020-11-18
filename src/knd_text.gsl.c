#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>

#include "knd_text.h"
#include "knd_task.h"
#include "knd_repo.h"
#include "knd_shard.h"
#include "knd_user.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_proc.h"
#include "knd_utils.h"
#include "knd_mempool.h"
#include "knd_output.h"

#define DEBUG_TEXT_EXPORT_LEVEL_0 0
#define DEBUG_TEXT_EXPORT_LEVEL_1 0
#define DEBUG_TEXT_EXPORT_LEVEL_2 0
#define DEBUG_TEXT_EXPORT_LEVEL_3 0
#define DEBUG_TEXT_EXPORT_LEVEL_TMP 1

static int export_class_declars(struct kndClassDeclaration *decl, struct kndTask *task)
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

static int export_proc_declars(struct kndProcDeclaration *decl, struct kndTask *task)
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

static int stm_export_GSL(struct kndStatement *stm, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;

    OUT("{stm ", strlen("{stm "));
    OUT(stm->schema_name, stm->schema_name_size);

    if (stm->class_declars) {
        err = export_class_declars(stm->class_declars, task);                      RET_ERR();
    }

    if (stm->proc_declars) {
        err = export_proc_declars(stm->proc_declars, task);                        RET_ERR();
    }
    OUT("}", 1);

    return knd_OK;
}

static int synode_export_GSL(struct kndSyNode *syn, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndSyNodeSpec *spec;
    int err;

    OUT("{syn ", strlen("{syn "));
    OUT(syn->name, syn->name_size);
    if (syn->is_terminal) {
        OUT("{term ", strlen("{term "));
        OUT(syn->class->name, syn->class->name_size);
        err = out->writef(out, "{pos %zu}{len %zu}", syn->linear_pos, syn->linear_len);
        if (err) return err;
        OUT("}", 1);
    }
    if (syn->topic) {
        err = synode_export_GSL(syn->topic, task);
        KND_TASK_ERR("failed to export a synode");
    }
    if (syn->spec) {
        spec = syn->spec;
        OUT("{spec ", strlen("{spec "));
        OUT(spec->name, spec->name_size);
        err = synode_export_GSL(spec->synode, task);
        KND_TASK_ERR("failed to export a spec synode");
        OUT("}", 1);
    }
    OUT("}", 1);
    return knd_OK;
}

static int clause_export_GSL(struct kndClause *clause, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;

    OUT("{clause ", strlen("{clause "));
    err = synode_export_GSL(clause->subj, task);
    KND_TASK_ERR("failed to export clause synode");
    OUT("}", 1);

    return knd_OK;
}

static int sent_export_GSL(struct kndSentence *sent, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;
    err = out->writec(out, '{');                        RET_ERR();
    err = out->write(out, sent->seq, sent->seq_size);   RET_ERR();

    if (sent->stm) {
        err = stm_export_GSL(sent->stm, task);           RET_ERR();
    }
    if (sent->clause) {
        err = clause_export_GSL(sent->clause, task);           RET_ERR();
    }

    err = out->writec(out, '}');                        RET_ERR();
    return knd_OK;
}

int knd_text_export_GSL(struct kndText *self, struct kndTask *task, size_t unused_var(depth))
{
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

    if (seq && seq->val_size) {
        OUT(" ", 1);
        OUT(seq->val, seq->val_size);
    }
    if (self->locale_size) {
        OUT("{", 1);
        OUT("lang ", strlen("lang "));
        OUT(self->locale, self->locale_size);
        OUT("}", 1);
    }
    if (self->trs) {
        OUT("[trn", strlen("[trn"));
        FOREACH (trn, self->trs) {
            OUT("{", 1);
            OUT(trn->locale, trn->locale_size);
            OUT("{t ", strlen("{t "));
            OUT(trn->seq->val, trn->seq->val_size);
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
                err = sent_export_GSL(sent, task);
                KND_TASK_ERR("failed to export sent GSL");
            }
            OUT("]", 1);
            OUT("}", 1);
        }
        OUT("]", 1);
    }

    return knd_OK;
}

int knd_par_export_GSL(struct kndPar *par, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndSentence *sent;
    int err;

    err = out->writef(out, "{%zu", par->numid);
    RET_ERR();
    if (par->num_sents) {
        OUT("[s", strlen("[s"));
        for (sent = par->sents; sent; sent = sent->next) {
            err = sent_export_GSL(sent, task);
            KND_TASK_ERR("failed to export sentence GSL");
        }
        OUT("]", 1);
    }
    OUT("}", 1);
    return knd_OK;
}

static int export_class_idx_GSL(struct kndClassIdx *self, struct kndTask *task)
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
            //err = export_class_idx_GSL(ref->idx, task);
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

int knd_text_export_query_report_GSL(struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndTextSearchReport *report;
    struct kndClassEntry *entry;
    int err;

    OUT("[report ", strlen("[report "));
    FOREACH (report, task->ctx->reports) {
        entry = report->entry;
        OUT("{", 1);
        OUT(entry->name, entry->name_size);

        if (report->idx) {
            err = export_class_idx_GSL(report->idx, task);
            KND_TASK_ERR("failed to export class idx");
        }
        OUT("}", 1);
    }
    OUT("]", 1);
    return knd_OK;
}

int knd_text_export_query_report(struct kndTask *task)
{
    int err;

    switch (task->ctx->format) {
    case KND_FORMAT_JSON:
        //
        break;
     default:
         err = knd_text_export_query_report_GSL(task);
         if (err) return err;
         break;
    }
    return knd_OK;
}

int knd_text_gloss_export_GSL(struct kndText *tr, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    const char *locale = task->ctx->locale;
    size_t locale_size = task->ctx->locale_size;
    int err;

    for (; tr; tr = tr->next) {
        if (locale_size != tr->locale_size) continue;

        if (memcmp(locale, tr->locale, tr->locale_size)) {
            continue;
        }
        if (task->ctx->format_offset) {
            OUT("\n", 1);
            err = knd_print_offset(out, depth * task->ctx->format_offset);
            RET_ERR();
        }
        OUT("[gloss ", strlen("[gloss "));
        OUT("{", 1);
        err = out->write_escaped(out, tr->locale, tr->locale_size);
        RET_ERR();
        OUT("{t ", strlen("{t "));
        err = out->write_escaped(out, tr->seq->val,  tr->seq->val_size);
        RET_ERR();
        OUT("}", 1);

        if (tr->abbr) {
            OUT("{abbr ", strlen("{abbr "));
            OUT(tr->abbr->val, tr->abbr->val_size);
            OUT("}", 1);
        }
        OUT("}", 1);
        OUT("]", 1);
        break;
    }
    return knd_OK;
}

