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
    struct kndClass *c;
    struct kndClassInstEntry *entry;
    struct kndClassInst *inst;

    for (; decl; decl = decl->next) {
        c = decl->entry->class;
        OUT("{class ", strlen("{class "));
        OUT(c->name, c->name_size);

        for (entry = decl->insts; entry; entry = entry->next) {
            inst = entry->inst;
            OUT("{!inst ", strlen("{!inst "));
            OUT(inst->name, inst->name_size);
            if (inst->alias_size) {
                OUT("{_as ", strlen("{_as "));
                OUT(inst->alias, inst->alias_size);
                OUT("}", 1);
            }
            OUT("}", 1);
        }
        OUT("}", 1);
    }
    return knd_OK;
}

static int export_proc_declars(struct kndProcDeclaration *decl, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndProc *proc;
    struct kndProcInstEntry *entry;
    struct kndProcInst *inst;
    int err;

    for (; decl; decl = decl->next) {
        proc = decl->proc;
        err = out->write(out, "{proc ", strlen("{proc "));                        RET_ERR();
        err = out->write(out, proc->name, proc->name_size);                       RET_ERR();

        for (entry = decl->insts; entry; entry = entry->next) {
            inst = entry->inst;

            err = out->write(out, "{!inst ", strlen("{!inst "));                  RET_ERR();
            err = knd_proc_inst_export_GSL(inst, false, task, 0);                    RET_ERR();
            err = out->writec(out, '}');                                          RET_ERR();
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

static int sent_export_GSL(struct kndSentence *sent, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;
    err = out->writec(out, '{');                        RET_ERR();
    err = out->write(out, sent->seq, sent->seq_size);   RET_ERR();

    if (sent->stm) {
        err = stm_export_GSL(sent->stm, task);           RET_ERR();
    }

    err = out->writec(out, '}');                        RET_ERR();
   
    return knd_OK;
}

static int export_GSL(struct kndText *self, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    //const char *locale = task->ctx->locale;
    // size_t locale_size = task->ctx->locale_size;
    struct kndPar *par;
    struct kndSentence *sent;
    struct kndState *state;
    const char *seq = self->seq;
    size_t seq_size = self->seq_size;
    //struct kndStateVal *val;
    // struct kndText *t;
    int err;

    state = atomic_load_explicit(&self->states, memory_order_relaxed);
    if (state) {
        seq = state->val->val;
        seq_size = state->val->val_size;
        return knd_OK;
    }

    if (seq_size) {
        err = out->write(out, seq, seq_size);                   RET_ERR();
    }
    if (self->locale_size) {
        err = out->writec(out, '{');                            RET_ERR();
        err = out->write(out, "_lang ", strlen("_lang "));      RET_ERR();
        err = out->write(out, self->locale, self->locale_size); RET_ERR();
        err = out->writec(out, '}');                            RET_ERR();
    }

    if (self->num_pars) {
        err = out->write(out, "[p", strlen("[p"));                  RET_ERR();
        for (par = self->pars; par; par = par->next) {
            err = out->writec(out, '{');                            RET_ERR();
            err = out->write(out, "[s", strlen("[s"));              RET_ERR();
            for (sent = par->sents; sent; sent = sent->next) {
                err = sent_export_GSL(sent, task);                  RET_ERR();
            }
            err = out->writec(out, ']');                            RET_ERR();
            err = out->writec(out, '}');                            RET_ERR();
        }
        err = out->writec(out, ']');                            RET_ERR();
    }
    
    return knd_OK;
}

static int export_JSON(struct kndText *self,
                       struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndState *state;
    int err;

    state = atomic_load_explicit(&self->states,
                                 memory_order_relaxed);
    if (!state) {
        err = out->write_escaped(out, self->seq, self->seq_size);
        if (err) return err;
        if (self->locale_size) {
            err = out->write(out, "\"_lang\":\"", strlen("\"_lang:\""));          RET_ERR();
            err = out->write(out, self->locale, self->locale_size);               RET_ERR();
            err = out->writec(out, '"');                                          RET_ERR();
        }
        return knd_OK;
    }

    err = out->write_escaped(out, state->val->val, state->val->val_size);
    if (err) return err;
    
    return knd_OK;
}

int knd_par_export_GSL(struct kndPar *par, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;

    err = out->writef(out, "{%zu", par->numid);         RET_ERR();

    if (par->class_declars) {
        err = export_class_declars(par->class_declars, task);                      RET_ERR();
    }

    if (par->proc_declars) {
        err = export_proc_declars(par->proc_declars, task);                        RET_ERR();
    }

    err = out->writec(out, '}');                        RET_ERR();
    return knd_OK;
}

int knd_text_export_query_report_GSL(struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndTextSearchReport *report;
    struct kndClassEntry *entry;
    struct kndTextLoc *loc;
    int err;
    OUT("[report ", strlen("[report "));

    for (report = task->ctx->reports; report; report = report->next) {
        entry = report->entry;
        OUT("{", 1);
        OUT(entry->name, entry->name_size);

        if (report->num_locs) {
            OUT("{tot ", strlen("{tot "));
            err = out->writef(out, "%zu", report->num_locs);         RET_ERR();
            OUT("}", 1);
        }
        OUT("[loc ", strlen("[loc "));
        for (loc = report->locs; loc; loc = loc->next) {
            OUT("{", 1);
            err = out->writef(out, "%zu:%zu", loc->par_id, loc->sent_id);         RET_ERR();
            OUT("}", 1);
        }
        OUT("]", 1);
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

int knd_text_export(struct kndText *self, knd_format format, struct kndTask *task)
{
    int err;

    switch (format) {
    case KND_FORMAT_JSON:
        err = export_JSON(self, task);                           RET_ERR();
        break;
    default:
        err = export_GSL(self, task);                            RET_ERR();
        break;
    }

    return knd_OK;
}
