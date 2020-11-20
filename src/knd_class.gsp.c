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

#define DEBUG_CLASS_GSP_LEVEL_1 0
#define DEBUG_CLASS_GSP_LEVEL_2 0
#define DEBUG_CLASS_GSP_LEVEL_3 0
#define DEBUG_CLASS_GSP_LEVEL_4 0
#define DEBUG_CLASS_GSP_LEVEL_5 0
#define DEBUG_CLASS_GSP_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepo *repo;
    struct kndAttrVar *attr_var;
    struct kndClass *class;
    struct kndClass *baseclass;
    struct kndClassRef *class_ref;
    struct kndClassInst *class_inst;
    struct kndClassVar *class_var;
};

int knd_class_inst_idx_fetch(struct kndClass *self, struct kndSharedDict **result, struct kndTask *task)
{
    struct kndOutput *out = task->file_out;
    struct kndSharedSet *idx, *new_idx;
    struct kndSharedDict *name_idx, *new_name_idx;
    struct stat st;
    int err;

    out->reset(out);
    OUT(task->path, task->path_size);
    OUT(task->repo->path, task->repo->path_size);
    err = out->writef(out, "snapshot_%zu/", task->repo->snapshots->numid);
    KND_TASK_ERR("snapshot path construction failed");

    OUT("inst_", strlen("inst_"));
    OUT(self->entry->id, self->entry->id_size);
    OUT(".gsp", strlen(".gsp"));

    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log(">> open class inst storage in %.*s", out->buf_size, out->buf);

    if (stat(out->buf, &st)) {
        return knd_NO_MATCH;
    }

    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log(".. reading class inst storage: %.*s [%zu]", out->buf_size, out->buf, (size_t)st.st_size);

    do {
        name_idx = atomic_load_explicit(&self->inst_name_idx, memory_order_acquire);
        if (name_idx) {
            // TODO free new_name_idx if (new_name_idx != NULL) 
            *result = name_idx;
            break;
        }
        err = knd_shared_dict_new(&new_name_idx, KND_MEDIUM_DICT_SIZE);
        KND_TASK_ERR("failed to create inst name idx");
        *result = new_name_idx;
    } while (!atomic_compare_exchange_weak(&self->inst_name_idx, &name_idx, new_name_idx));

    do {
        idx = atomic_load_explicit(&self->inst_idx, memory_order_acquire);
        if (idx) {
            // TODO free new_idx if (new_idx != NULL) 
            break;
        }
        err = knd_shared_set_new(NULL, &new_idx);
        KND_TASK_ERR("failed to create inst idx");

    } while (!atomic_compare_exchange_weak(&self->inst_idx, &idx, new_idx));

    task->blueprint = self->entry;
    err = knd_shared_set_unmarshall_file(self->inst_idx, out->buf, out->buf_size,
                                         (size_t)st.st_size, knd_class_inst_entry_unmarshall, task);
    KND_TASK_ERR("failed to unmarshall class inst storage GSP file");
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

static int export_baseclass_vars(struct kndClass *self, struct kndTask *task, struct kndOutput *out)
{
    struct kndClassVar *item;
    struct kndClass *c;
    int err;

    err = out->write(out, "[is", strlen("[is"));                              RET_ERR();
    for (item = self->baseclass_vars; item; item = item->next) {
        err = out->writec(out, '{');                                              RET_ERR();
        c = item->entry->class;
        err = out->write(out, c->entry->id, c->entry->id_size);             RET_ERR();
        if (item->attrs) {
            err = knd_attr_vars_export_GSP(item->attrs, out, task, 0, false);
            if (err) return err;
        }
        err = out->writec(out, '}');                                              RET_ERR();
    }
    err = out->writec(out, ']');                                                  RET_ERR();
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

#if 0
static int export_descendants_GSP(struct kndClass *self, struct kndTask *task)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndOutput *out = task->out;
    struct kndSet *set;
    int err;

    set = self->entry->descendants;

    err = out->write(out, "{_desc", strlen("{_desc"));                            RET_ERR();
    buf_size = sprintf(buf, "{tot %zu}", set->num_elems);
    err = out->write(out, buf, buf_size);                                         RET_ERR();

    err = out->write(out, "[c", strlen("[c"));                                    RET_ERR();
    err = set->map(set, export_conc_id_GSP, (void*)out);
    if (err) return err;
    err = out->writec(out, ']');                                                  RET_ERR();

    /*    if (set->facets) {
        err = export_facets_GSP(set, task);                                       RET_ERR();
        } */

    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}
#endif

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

    if (self->baseclass_vars) {
        err = export_baseclass_vars(self, task, out);                                   RET_ERR();
    }

    if (self->attrs) {
        for (attr = self->attrs; attr; attr = attr->next) {
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
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size = 0;
    struct kndOutput *out = task->out;
    struct kndAttr *attr;
    struct kndClassEntry *entry = self->entry;
    int err;

    assert(entry->seq != NULL);

    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log(".. GSP export of \"%.*s\" [%.*s]",
                entry->name_size, entry->name, entry->id_size, entry->id);

    knd_uid_create(entry->seq->numid, idbuf, &idbuf_size);
    OUT(idbuf, idbuf_size);

    if (self->tr) {
        err = export_glosses(self, out);
        KND_TASK_ERR("failed to export glosses");
    }
    if (self->baseclass_vars) {
        err = export_baseclass_vars(self, task, out);
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

    // insts
    if (self->inst_idx) {
        err = out->writef(out, "{insts %zu}", self->inst_idx->num_elems);
        KND_TASK_ERR("failed to export num insts GSP");
    }
    
    return knd_OK;
}

int knd_class_marshall(void *elem, size_t *output_size, struct kndTask *task)
{
    struct kndClassEntry *entry = elem;
    struct kndOutput *out = task->out;
    size_t orig_size = out->buf_size;
    int err;
    assert(entry->class != NULL);

    err = knd_class_export_GSP(entry->class, task);
    KND_TASK_ERR("failed to export class GSP");

    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log("== GSP of %.*s (%.*s)  size:%zu", 
                entry->class->name_size,  entry->class->name, entry->id_size, entry->id, out->buf_size - orig_size);

    *output_size = out->buf_size - orig_size;
    return knd_OK;
}

int knd_class_entry_unmarshall(const char *elem_id, size_t elem_id_size, const char *rec, size_t rec_size,
                               void **result, struct kndTask *task)
{
    struct kndMemPool *mempool = task->user_ctx ? task->user_ctx->mempool : task->mempool;
    struct kndClassEntry *entry = NULL;
    struct kndRepo *repo = task->repo;
    struct kndCharSeq *seq;
    struct kndSharedDictItem *item;
    const char *c, *name = rec;
    size_t name_size;
    int err;

    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log(">> GSP class entry \"%.*s\" => \"%.*s\"", elem_id_size, elem_id, rec_size, rec);

    err = knd_class_entry_new(mempool, &entry);
    KND_TASK_ERR("failed to alloc a class entry");
    entry->repo = task->repo;
    memcpy(entry->id, elem_id, elem_id_size);
    entry->id_size = elem_id_size;

    /* get name numid */
    c = name;
    while (*c) {
        if (*c == '{' || *c == '[') break;
        c++;
    }
    name_size = c - name;
    if (!name_size) {
        err = knd_FORMAT;
        KND_TASK_ERR("anonymous class entry in GSP");
    }
    if (name_size > KND_ID_SIZE) {
        err = knd_FORMAT;
        KND_TASK_ERR("invalid class name numid in GSP");
    }

    err = knd_charseq_decode(repo, name, name_size, &seq, task);
    KND_TASK_ERR("failed to decode a charseq");

    entry->name = seq->val;
    entry->name_size = seq->val_size;
    entry->seq = seq;

    err = knd_shared_dict_set(repo->class_name_idx, entry->name, entry->name_size,
                              (void*)entry, task->mempool, NULL, &item, false);
    KND_TASK_ERR("failed to register class name");
    entry->dict_item = item;

    err = knd_shared_set_add(repo->class_idx, entry->id, entry->id_size, (void*)entry);
    KND_TASK_ERR("failed to register class entry \"%.*s\"", entry->id_size, entry->id);

    if (DEBUG_CLASS_GSP_LEVEL_3)
        knd_log("== class name decoded \"%.*s\" => \"%.*s\" (repo:%.*s)",
                entry->id_size, entry->id, entry->name_size, entry->name, repo->name_size, repo->name);

    *result = entry;
    return knd_OK;
}

