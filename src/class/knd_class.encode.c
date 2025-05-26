#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

/* numeric conversion by strtol */
#include <errno.h>
#include <limits.h>

#include "knd_config.h"
#include "knd_mempool.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_attr_stm.h"
#include "knd_task.h"
#include "knd_user.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_shared_dict.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
#include "knd_shared_set.h"
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#include <gsl-parser.h>

#define DEBUG_CLASS_ENCODE_LEVEL_1 0
#define DEBUG_CLASS_ENCODE_LEVEL_2 0
#define DEBUG_CLASS_ENCODE_LEVEL_3 0
#define DEBUG_CLASS_ENCODE_LEVEL_4 0
#define DEBUG_CLASS_ENCODE_LEVEL_5 0
#define DEBUG_CLASS_ENCODE_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepo *repo;
    struct kndAttrStm *attr_stm;
    struct kndClass *class;
    struct kndClass *baseclass;
    struct kndClassRef *class_ref;
    struct kndClassInst *class_inst;
    struct kndClassBasePred *class_var;
};

int knd_class_inst_idx_fetch(struct kndClass *self, struct kndSharedDict **result,
                             struct kndTask *task)
{
    struct kndOutput *out = task->file_out;
    struct kndSharedSet *idx, *new_idx;
    struct kndSharedDict *name_idx, *new_name_idx;
    struct stat st;
    int err;

    out->reset(out);
    OUT(task->path, task->path_size);
    OUT(task->repo->path, task->repo->path_size);
    err = out->writef(out, "snapshot_%zu/", task->snapshot->numid);
    KND_TASK_ERR("snapshot path construction failed");

    OUTS("inst_");
    OUT(self->entry->id, self->entry->id_size);
    OUT(".gsp", strlen(".gsp"));

    if (DEBUG_CLASS_ENCODE_LEVEL_2)
        knd_log(">> open class inst storage in %.*s", out->buf_size, out->buf);

    if (stat(out->buf, &st)) {
        return knd_NO_MATCH;
    }

    if (DEBUG_CLASS_ENCODE_LEVEL_2)
        knd_log(".. reading class inst storage: %.*s [%zu]",
                out->buf_size, out->buf, (size_t)st.st_size);

    do {
        name_idx = atomic_load_explicit(&self->inst_name_idx, memory_order_acquire);
        if (name_idx) {
            // TODO free new_name_idx if (new_name_idx != NULL) 
            *result = name_idx;
            break;
        }
        err = knd_shared_dict_new(&new_name_idx, KND_MEDIUM_DICT_SIZE, task->mempool, false);
        KND_TASK_ERR("failed to create inst name idx");
        *result = new_name_idx;
    } while (!atomic_compare_exchange_weak(&self->inst_name_idx, &name_idx, new_name_idx));

    do {
        idx = atomic_load_explicit(&self->inst_idx, memory_order_acquire);
        if (idx) {
            // TODO free new_idx if (new_idx != NULL) 
            break;
        }
        err = knd_shared_set_new(&new_idx, task->mempool);
        KND_TASK_ERR("failed to create inst idx");

    } while (!atomic_compare_exchange_weak(&self->inst_idx, &idx, new_idx));

    task->payload = self->entry;

    //err = knd_shared_set_unmarshall_leaf(self->inst_idx, leaf, knd_class_inst_entry_unmarshall, task);
    //KND_TASK_ERR("failed to unmarshall class inst storage GSP file");
    return knd_OK;
}

static int export_glosses(struct kndClass *self, struct kndOutput *out)
{
    char idbuf[KND_ID_SIZE];
    size_t id_size = 0;
    struct kndText *t;
    OUT("[g", strlen("[g"));
    FOREACH (t, self->tr) {
        OUT("{", 1);
        OUT(t->locale, t->locale_size);
        OUT("{t ", strlen("{t "));
        knd_uid_create(t->seq->numid, idbuf, &id_size);
        OUT(idbuf, id_size);
        OUT("}", 1);
        if (t->abbr) {
            OUT("{abbr ", strlen("{abbr "));
            knd_uid_create(t->abbr->numid, idbuf, &id_size);
            OUT(idbuf, id_size);
            OUT("}", 1);
        }
        OUT("}", 1);
    }
    OUT("]", 1);
    return knd_OK;
}

static int export_base_preds(struct kndClass *self, struct kndTask *task, struct kndOutput *out)
{
    struct kndClassBasePred *bp;
    int err;

    OUT("[is", strlen("[is"));
    FOREACH (bp, self->base_preds) {
        OUT("{", 1);

        if (bp->entry->id_size == 0) {
            knd_log("unresolved base class ref %.*s in {class %.*s}?",
                    bp->entry->name_size, bp->entry->name,
                    self->name_size, self->name);
        }

        OUT(bp->entry->id, bp->entry->id_size);
        if (bp->attr_stms) {
            err = knd_attr_stms_export_GSP(bp->attr_stms, out, task, 0);
            if (err) return err;
        }
        OUTC('}');
    }
    OUTC(']');
    return knd_OK;
}

static int export_ancestors(struct kndClass *self, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndClassRef *ref;
    struct kndClassEntry *entry;

    /* ignore root concept */
    if (self->num_ancestors == 1) {
        if (!self->ancestors->entry->id_size) 
            return knd_OK;
    }

    OUT("[anc", strlen("[anc"));
    FOREACH (ref, self->ancestors) {
        entry = ref->entry;
        OUT("{", 1);
        OUT(entry->id, entry->id_size);
        OUT("}", 1);
    }
    OUT("]", 1);
    return knd_OK;
}

static int export_children(struct kndClass *self, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndClassRef *ref;
    struct kndClassEntry *entry;

    OUT("[c", strlen("[c"));
    FOREACH (ref, self->children) {
        entry = ref->entry;
        OUT("{", 1);
        OUT(entry->id, entry->id_size);
        OUT("}", 1);
    }
    OUT("]", 1);
    return knd_OK;
}

static int export_class_ref(void *elem, void *ctx)
{
    struct kndClassEntry *entry = elem;
    struct kndTask *task = ctx;
    struct kndOutput *out = task->out;
    OUT("{", 1);
    OUT(entry->id, entry->id_size);
    OUT("}", 1);
    return knd_OK;
}

static int export_descendants(struct kndClass *self, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;

    OUTF("{num-desc %zu}", self->descendants->num_elems);

    if (self->num_descendants == self->num_children) return knd_OK;
    if (self->num_descendants > KND_MAX_DESCENDANTS_IDX_SIZE) {
        return knd_OK;
    }

    OUT("[desc", strlen("[desc"));

    err = knd_set_map(self->descendants, export_class_ref, (void*)task);
    KND_TASK_ERR("failed to export descendants");

    OUT("]", 1);
    return knd_OK;
}

static int export_class_body_commits(struct kndClass *self,
                                     struct kndClassCommit *unused_var(class_commit),
                                     struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndState *state = self->states;
    struct kndAttr *attr;
    int err;

    switch (state->phase) {
    case KND_CREATED:
        err = out->write(out, "{_new}", strlen("{_new}"));                        RET_ERR();
        break;
    case KND_REMOVED:
        err = out->write(out, "{_rm}", strlen("{_rm}"));                          RET_ERR();
        break;
    default:
        break;
    }

    // TODO

    if (self->tr) {
        err = export_glosses(self, out);                                          RET_ERR();
    }

    if (self->base_preds) {
        err = export_base_preds(self, task, out);                                   RET_ERR();
    }

    if (self->attrs) {
        FOREACH (attr, self->attrs) {
            err = knd_attr_export(attr, KND_FORMAT_GSP, task);
            if (err) return err;
        }
    }
    
    return knd_OK;
}

static int export_class_inst_commits(struct kndClass *unused_var(self), struct kndClassCommit *class_commit,
                                     struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndClassInst *inst;
    int err;

    err = out->write(out, "[!inst", strlen("[!inst"));                            RET_ERR();
    for (size_t i = 0; i < class_commit->num_insts; i++) {
        inst = class_commit->insts[i];
        err = out->writec(out, '{');                                              RET_ERR();
        err = out->write(out, inst->entry->id, inst->entry->id_size);             RET_ERR();

        err = out->write(out, "{_n ", strlen("{_n "));                            RET_ERR();
        err = out->write(out, inst->name, inst->name_size);                       RET_ERR();
        err = out->writec(out, '}');                                              RET_ERR();

        //err = inst->export_state(inst, KND_FORMAT_GSP, out);                      RET_ERR();
        err = out->writec(out, '}');                                              RET_ERR();
    }
    err = out->writec(out, ']');                                                  RET_ERR();

    return knd_OK;
}

int knd_class_export_commits_GSP(struct kndClass *self, struct kndClassCommit *class_commit, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndCommit *commit = class_commit->commit;
    struct kndState *state = self->states;
    int err;
    
    err = out->writec(out, '{');                                                  RET_ERR();
    err = out->write(out, self->entry->id, self->entry->id_size);                 RET_ERR();
    err = out->write(out, "{_n ", strlen("{_n "));                                RET_ERR();
    err = out->write(out, self->name, self->name_size);                           RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();

    err = out->write(out, "{_st", strlen("{_st"));                                RET_ERR();

    if (state && state->commit == commit) {
        err = out->writec(out, ' ');                                              RET_ERR();

        // TODO
        //err = out->write(out, state->id, state->id_size);                         RET_ERR();

        /* any commits of the class body? */
        err = export_class_body_commits(self, class_commit, task);                 RET_ERR();
    }
    if (self->inst_states) {
        state = self->inst_states;
        /* any commits of the class insts? */
        if (state->commit == commit) {
            err = export_class_inst_commits(self, class_commit, task);             RET_ERR();
        }
    }

    err = out->writec(out, '}');                                                  RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();
    return knd_OK;
}

int knd_class_export_GSP(struct kndClass *self, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndAttr *attr;
    struct kndClassEntry *entry = self->entry;
    int err;

    assert(entry->seq != NULL);

    if (DEBUG_CLASS_ENCODE_LEVEL_2) {
        knd_log(".. GSP export of {class %.*s {id %.*s}}",
                entry->name_size, entry->name, entry->id_size, entry->id);
    }
    //knd_uid_create(entry->seq->numid, idbuf, &idbuf_size);
    //OUT(idbuf, idbuf_size);

    if (self->tr) {
        err = export_glosses(self, out);
        KND_TASK_ERR("failed to export glosses");
    }
    if (self->base_preds) {
        err = export_base_preds(self, task, out);
        KND_TASK_ERR("failed to export baseclass vars");
    }
    if (self->attrs) {
        FOREACH (attr, self->attrs) {
            err = knd_attr_export(attr, KND_FORMAT_GSP, task);
            KND_TASK_ERR("failed to export attr");
        }
    }

    if (self->num_ancestors) {
        err = export_ancestors(self, task);
        KND_TASK_ERR("failed to export ancestors GSP");
    }

    if (self->num_children) {
        err = export_children(self, task);
        KND_TASK_ERR("failed to export children GSP");
    }

    /* export descendants - check the max limit */
    if (self->descendants) {
        err = export_descendants(self, task);
        KND_TASK_ERR("failed to export descendants GSP");
    }

    /* instances */
    if (self->inst_idx) {
        err = out->writef(out, "{insts %zu}", self->inst_idx->num_elems);
        KND_TASK_ERR("failed to export num insts GSP");
    }
    return knd_OK;
}

int knd_class_names_marshall(void *elem, size_t *output_size, struct kndTask *task)
{
    struct kndSharedDictItem *item, *items = elem;
    struct kndClassEntry *entry;
    struct kndOutput *out = task->out;
    size_t orig_size = out->buf_size;
    size_t num_requests;

    OUT("[c", strlen("[c"));

    FOREACH (item, items) {
        entry = item->data;
        // TODO check commit version

        OUT("{", strlen("{"));
        OUT(entry->name, entry->name_size);
        OUT("{id ", strlen("{id "));
        OUT(entry->id, entry->id_size);
        OUT("}", strlen("}"));

        num_requests = atomic_load_explicit(&entry->num_requests, memory_order_relaxed);
        if (num_requests) {
            OUT("{freq ", strlen("{freq "));
            OUTF("%zu", num_requests);
            OUT("}", strlen("}"));
        }
        OUT("}", strlen("}"));
        if (DEBUG_CLASS_ENCODE_LEVEL_3) {
            knd_log("== {class %.*s {id %.*s}} {GSP {size %zu}}", 
                    entry->name_size,  entry->name, 
                    entry->id_size, entry->id, out->buf_size - orig_size);
        }
    }
    OUT("]", strlen("]"));

    *output_size = out->buf_size - orig_size;
    return knd_OK;
}

int knd_class_marshall(void *elem, size_t *output_size, struct kndTask *task)
{
    struct kndClassEntry *entry = elem;
    struct kndClass *c;
    struct kndOutput *out = task->out;
    size_t orig_size = out->buf_size;
    int err;

    err = knd_class_acquire(entry, &c, task);
    KND_TASK_ERR("failed to acquire class %.*s", entry->name_size, entry->name);

    err = knd_class_export_GSP(c, task);
    KND_TASK_ERR("failed to export class GSP");

    if (DEBUG_CLASS_ENCODE_LEVEL_3) {
        size_t numid = 0;
        knd_calc_num_id(entry->id, entry->id_size, &numid);

        knd_log("== {class %.*s {id %.*s {numid %zu}}} {GSP {size %zu}}", 
                entry->name_size,  entry->name, 
                entry->id_size, entry->id, numid, out->buf_size - orig_size);
    }
    *output_size = out->buf_size - orig_size;
    return knd_OK;
}
