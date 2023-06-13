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

#define DEBUG_TEXT_JSON_LEVEL_0 0
#define DEBUG_TEXT_JSON_LEVEL_1 0
#define DEBUG_TEXT_JSON_LEVEL_2 0
#define DEBUG_TEXT_JSON_LEVEL_3 0
#define DEBUG_TEXT_JSON_LEVEL_TMP 1

static int export_class_declars(struct kndClassDeclaration *decls, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndClassInstEntry *entry;
    struct kndClassDeclaration *decl;
    int count = 0;
    int err;

    FOREACH (decl, decls) {
        if (count) {
            OUT(",", 1);
        }
        OUT("{\"name\":\"", strlen("{\"name\":\""));
        OUT(decl->entry->name, decl->entry->name_size);
        OUT("\"", 1);

        FOREACH (entry, decl->insts) {
            err = knd_class_inst_export_JSON(entry->inst, false, KND_CREATED, task, 0);
            KND_TASK_ERR("failed to export class inst JSON");
        }
        OUT("}", 1);
        count++;
    }
    return knd_OK;
}

static int export_proc_declars(struct kndProcDeclaration *decls, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndProcInstEntry *entry;
    struct kndProcDeclaration *decl;
    int err;

    FOREACH (decl, decls) {
        err = out->write(out, "{proc ", strlen("{proc "));                        RET_ERR();
        err = out->write(out, decl->entry->name, decl->entry->name_size);         RET_ERR();

        FOREACH (entry, decl->insts) {
            err = knd_proc_inst_export_JSON(entry->inst, false, KND_CREATED, task, 0);    RET_ERR();
        }
        err = out->writec(out, '}');                                              RET_ERR();
    }
    return knd_OK;
}

static int stm_export_JSON(struct kndStatement *stm, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;

    OUT("{", 1);
    if (stm->schema_name_size) {
        OUT(stm->schema_name, stm->schema_name_size);
    }

    if (stm->class_declars) {
        OUT("\"classes\":[", strlen("\"classes\":["));
        err = export_class_declars(stm->class_declars, task);
        KND_TASK_ERR("failed to export class declar JSON");
        OUT("]", 1);
    }

    if (stm->proc_declars) {
        OUT("\"procs\":[", strlen("\"procs\":["));
        err = export_proc_declars(stm->proc_declars, task);
        KND_TASK_ERR("failed to export proc declar JSON");
        OUT("]", 1);
    }
    OUT("}", 1);

    return knd_OK;
}

static int export_gloss(struct kndText *glosses, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndText *tr;
    int err;

    FOREACH (tr, glosses) {
        if (task->ctx->locale_size != tr->locale_size) continue;
        if (memcmp(task->ctx->locale, tr->locale, tr->locale_size)) {
            continue;
        }
        OUT(",\"gloss\":\"", strlen(",\"gloss\":\""));
        err = out->write_escaped(out, tr->seq->val,  tr->seq->val_size);
        KND_TASK_ERR("failed to export a charseq");
        OUT("\"", 1);
        if (tr->abbr) {
            OUT(",\"abbr\":\"", strlen(",\"abbr\":\""));
            err = out->write_escaped(out, tr->abbr->val,  tr->abbr->val_size);
            KND_TASK_ERR("failed to export an abbr charseq");
            OUT("\"", 1);
        }
        break;
    }
    return knd_OK;
}

static int export_translation(struct kndText *trs, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndText *tr;
    int err;
    FOREACH (tr, trs) {
        if (task->ctx->locale_size != tr->locale_size) continue;
        if (memcmp(task->ctx->locale, tr->locale, tr->locale_size)) {
            continue;
        }
        OUT("\"txt\":\"", strlen("\"txt\":\""));
        err = out->write_escaped(out, tr->seq->val,  tr->seq->val_size);
        KND_TASK_ERR("failed to export a charseq");
        OUT("\"", 1);
        return knd_OK;
    }
    return knd_NO_MATCH;
}

static int synode_export_JSON(struct kndSyNode *syn, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndSyNodeSpec *spec;
    int err;

    OUT("{", 1);
    OUT("\"name\":\"", strlen("\"name\":\""));
    OUT(syn->name, syn->name_size);
    OUT("\"", 1);
    err = export_gloss(syn->class->tr, task);
    KND_TASK_ERR("failed to export a gloss");
    if (syn->is_terminal) {
        err = out->writef(out, ",\"pos\":%zu,\"len\":%zu", syn->linear_pos, syn->linear_len);
        if (err) return err;
        OUT("}", 1);
        return knd_OK;
    }

    if (syn->topic) {
        OUT(",\"topic\":", strlen(",\"topic\":"));
        err = synode_export_JSON(syn->topic, task);
        KND_TASK_ERR("failed to export a synode");
    }
    if (syn->spec) {
        spec = syn->spec;
        OUT(",\"spec\":{", strlen(",\"spec\":{"));
        OUT("\"name\":\"", strlen("\"name\":\""));
        OUT(spec->name, spec->name_size);
        OUT("\"", 1);
        err = export_gloss(spec->class->tr, task);
        KND_TASK_ERR("failed to export a spec gloss");

        if (spec->synode) {
            OUT(",\"topic\":", strlen(",\"topic\":"));
            err = synode_export_JSON(spec->synode, task);
            KND_TASK_ERR("failed to export a spec synode");
        }
        OUT("}", 1);
    }
    OUT("}", 1);
    return knd_OK;
}

static int sent_export_JSON(struct kndSentence *sent, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;
    OUT("{", 1);
    OUT("\"txt\":\"", strlen("\"txt\":\""));
    OUT(sent->seq, sent->seq_size);
    OUT("\"", 1);

    if (sent->stm) {
        OUT(",\"stm\":", strlen(",\"stm\":"));
        err = stm_export_JSON(sent->stm, task);           RET_ERR();
    }

    if (sent->clause) {
        OUT(",\"clause\":", strlen(",\"clause\":"));
        err = synode_export_JSON(sent->clause->subj, task);
        KND_TASK_ERR("failed to export clause synode");
    }

    OUT("}", 1);
    return knd_OK;
}

int knd_text_export_JSON(struct kndText *self, struct kndTask *task, size_t unused_var(depth))
{
    struct kndOutput *out = task->out;
    struct kndPar *par;
    struct kndSentence *sent;
    struct kndCharSeq *seq = self->seq;
    bool separ_needed = false;
    int err;

    OUT("{", 1);
    
    err = export_translation(self->trs, task);
    switch (err) {
    case knd_NO_MATCH:
        if (seq && seq->val_size) {
            OUT("\"txt\":\"", strlen("\"txt\":\""));
            err = out->write_escaped(out, seq->val,  seq->val_size);
            KND_TASK_ERR("failed to export a charseq");
            OUT("\"", 1);
            separ_needed = true;
        }
        if (self->locale_size) {
            OUT("\"lang\":", strlen("\"lang\":"));
            OUT(self->locale, self->locale_size);
            OUT("\"", 1);
        }
        break;
    case knd_OK:
        break;
    default:
        KND_TASK_LOG("failed to export a localized translation");
        return err;
    }

    if (self->num_pars) {
        if (separ_needed) {
            OUT(",", 1);
        }
        OUT("\"pars\":[", strlen("\"pars\":["));
        FOREACH (par, self->pars) {
            OUT("{", 1);
            OUT("\"sents\":[", strlen("\"sents\":["));
            FOREACH (sent, par->sents) {
                err = sent_export_JSON(sent, task);
                if (err) return err;
            }
            OUT("]", 1);
            OUT("}", 1);
        }
        OUT("]", 1);
    }
    OUT("}", 1);

    return knd_OK;
}

static int export_class_idx_JSON(struct kndClassIdx *self, struct kndTask *task)
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
            //err = export_class_idx_JSON(ref->idx, task);
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

int knd_text_export_query_report_JSON(struct kndTask *task)
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
            err = export_class_idx_JSON(report->idx, task);
            KND_TASK_ERR("failed to export class idx");
        }
        OUT("}", 1);
    }
    OUT("]", 1);
    return knd_OK;
}

int knd_text_gloss_export_JSON(struct kndText *text, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndText *tr;
    size_t indent_size = task->ctx->format_indent;
    int err;

    FOREACH (tr, text) {
        if (task->ctx->locale_size != tr->locale_size) continue;
        if (memcmp(task->ctx->locale, tr->locale, tr->locale_size)) {
            continue;
        }
        OUT(",", 1);
        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, depth * indent_size);
            RET_ERR();
        }
        OUT("\"gloss\":", strlen("\"gloss\":"));
        if (indent_size) {
            OUT(" ", 1);
        }
        OUT("\"", 1);
        err = out->write_escaped(out, tr->seq->val,  tr->seq->val_size);
        RET_ERR();
        OUT("\"", 1);

        if (tr->abbr) {
            OUT(",", 1);
            if (indent_size) {
                OUT("\n", 1);
                err = knd_print_offset(out, depth * indent_size);
                RET_ERR();
            }
            OUT("\"abbr\":", strlen("\"abbr\":"));
            if (indent_size) {
                OUT(" ", 1);
            }
            OUT("\"", 1);
            OUT(tr->abbr->val, tr->abbr->val_size);
            OUT("\"", 1);
        }
        break;
    }
    return knd_OK;
}

int knd_text_build_JSON(const char *rec, size_t rec_size, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndText *text;
    struct kndMemPool *mempool = task->mempool;
    gsl_err_t parser_err;
    int err;

    out->reset(out);

    err = knd_text_new(mempool, &text);
    KND_TASK_ERR("failed to alloc text");

    parser_err = knd_text_import(text, rec, &rec_size, task);
    if (parser_err.code) {
        KND_TASK_LOG("text parsing failed: %d %.*s", parser_err.code, task->log->buf_size, task->log->buf);
        return knd_FAIL;
    }

    err = knd_text_export(text, KND_FORMAT_JSON, task, 0);
    KND_TASK_ERR("failed to export text JSON");

    if (DEBUG_TEXT_JSON_LEVEL_2)
         knd_log("== JSON: %.*s", task->out->buf_size, task->out->buf);

    task->output = task->out->buf;
    task->output_size = task->out->buf_size;
    return knd_OK;
}

