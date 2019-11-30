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

void knd_text_str(struct kndText *self, size_t depth)
{
    struct kndState *state;
    struct kndStateVal *val;
    struct kndPar *par;
    struct kndSentence *sent;

    state = atomic_load_explicit(&self->states,
                                 memory_order_relaxed);
    if (!state) {
        if (self->seq_size) {
            knd_log("%*stext: \"%.*s\" (lang:%.*s)",
                    depth * KND_OFFSET_SIZE, "",
                    self->seq_size, self->seq, self->locale_size, self->locale);
            return;
        }
        if (self->num_pars) {
            knd_log("%*stext (lang:%.*s) [par",
                    depth * KND_OFFSET_SIZE, "",
                    self->locale_size, self->locale);
            for (par = self->pars; par; par = par->next) {
                knd_log("%*s#%zu:", (depth + 1) * KND_OFFSET_SIZE, "", par->numid);

                for (sent = par->sents; sent; sent = sent->next) {
                    knd_log("%*s#%zu: \"%.*s\"",
                            (depth + 2) * KND_OFFSET_SIZE, "",
                            sent->numid, sent->seq_size, sent->seq);
                }
            }
            knd_log("%*s]", depth * KND_OFFSET_SIZE, "");
        }
        return;
    }
    val = state->val;
    knd_log("%*stext: \"%.*s\" (lang:%.*s)",
            depth * KND_OFFSET_SIZE, "",
            val->val_size, val->val, self->locale_size, self->locale);
}

static int export_class_declars(struct kndClassDeclaration *decl,
                                struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndClass *c;
    struct kndClassInstEntry *entry;
    struct kndClassInst *inst;
    int err;

    for (; decl; decl = decl->next) {
        c = decl->class;

        err = out->write(out, "{class ", strlen("{class "));         RET_ERR();
        err = out->write(out, c->name, c->name_size);                RET_ERR();

        for (entry = decl->insts; entry; entry = entry->next) {
            inst = entry->inst;

            err = out->write(out, "{!inst ", strlen("{!inst "));         RET_ERR();
            err = out->write(out, inst->name, inst->name_size);          RET_ERR();

            if (inst->alias_size) {
                err = out->write(out, "{_as ", strlen("{_as "));         RET_ERR();
                err = out->write(out, inst->alias, inst->alias_size);          RET_ERR();
                err = out->writec(out, '}');                                 RET_ERR();
            }
            err = out->writec(out, '}');                                 RET_ERR();
        }
        err = out->writec(out, '}');                                 RET_ERR();
    }
    return knd_OK;
}

static int export_proc_declars(struct kndProcDeclaration *decl,
                               struct kndTask *task)
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
            err = knd_proc_inst_export_GSL(inst, false, task);                    RET_ERR();
            err = out->writec(out, '}');                                          RET_ERR();
        }
        err = out->writec(out, '}');                                              RET_ERR();
    }
    return knd_OK;
}

static int stm_export_GSL(struct kndStatement *stm,
                          struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;

    err = out->write(out, "{stm ", strlen("{stm "));                              RET_ERR();
    err = out->write(out, stm->name, stm->name_size);                             RET_ERR();

    if (stm->class_declars) {
        err = export_class_declars(stm->class_declars, task);                      RET_ERR();
    }
    if (stm->proc_declars) {
        err = export_proc_declars(stm->proc_declars, task);                        RET_ERR();
    }
    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}

static int sent_export_GSL(struct kndSentence *sent,
                           struct kndTask *task)
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

static int export_GSL(struct kndText *self,
                      struct kndTask *task)
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

    state = atomic_load_explicit(&self->states,
                                 memory_order_relaxed);
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

int knd_text_export(struct kndText *self,
                    knd_format format,
                    struct kndTask *task)
{
    int err;

    switch (format) {
    case KND_FORMAT_GSL:
        err = export_GSL(self, task);                            RET_ERR();
        break;
    case KND_FORMAT_JSON:
        err = export_JSON(self, task);                           RET_ERR();
        break;
        /*case KND_FORMAT_GSP:
        err = export_GSP(self, task, task->out);  RET_ERR();
        break;*/
    default:
        break;
    }

    return knd_OK;
}
