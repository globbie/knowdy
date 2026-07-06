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

static int export_glosses(struct kndClass *cls, struct kndOutput *out)
{
    char idbuf[KND_ID_SIZE];
    size_t id_size = 0;
    struct kndText *t;

    OUT("[g", strlen("[g"));
    FOREACH (t, cls->entry->glosses) {
        OUT("{", 1);
        OUT(t->locale->id, t->locale->id_size);
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

static int export_base_preds(struct kndClass *cls, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndClassBasePred *bp = cls->base_preds;
    int err;

    assert (bp != NULL);

    if (bp->is_root) {
        OUT("{is-root}", strlen("{is-root}"));
        return knd_OK;
    }

    OUT("[is", strlen("[is"));
    FOREACH (bp, cls->base_preds) {
        OUT("{", 1);

        if (bp->entry->id_size == 0) {
            knd_log("unresolved base class ref %.*s in {cls %.*s}?",
                    bp->entry->name_size, bp->entry->name,
                    cls->name_size, cls->name);
            return knd_CONFLICT;
        }

        OUT(bp->entry->id, bp->entry->id_size);
        if (bp->attr_stms) {
            err = knd_attr_stms_export_GSP(bp->attr_stms, task, 0);
            if (err) return err;
        }
        OUTC('}');
    }
    OUTC(']');
    return knd_OK;
}

static int export_ancestors(struct kndClass *cls, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndClassRef *ref;
    struct kndClassEntry *entry;

    /* ignore root concept */
    if (cls->num_ancestors == 1) {
        if (!cls->ancestors->entry->id_size) 
            return knd_OK;
    }

    OUT("[anc", strlen("[anc"));
    FOREACH (ref, cls->ancestors) {
        entry = ref->entry;
        OUT("{", 1);
        OUT(entry->id, entry->id_size);
        OUT("}", 1);
    }
    OUT("]", 1);
    return knd_OK;
}

static int export_attrs(struct kndClass *cls, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndAttr *attr;

    OUT("[a", strlen("[a"));
    FOREACH (attr, cls->attrs) {
        OUT("{", 1);
        OUT(attr->id, attr->id_size);
        OUT("}", 1);
    }
    OUT("]", 1);
    return knd_OK;
}

static int export_children(struct kndClass *cls, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndClassRef *ref;

    OUT("[c", strlen("[c"));
    FOREACH (ref, cls->children) {
        OUT("{", 1);
        OUT(ref->entry->id, ref->entry->id_size);
        OUT("}", 1);
    }
    OUT("]", 1);

    return knd_OK;
}

static int export_class_ref(void *elem, void *unused_var(ctx), struct kndTask *task)
{
    struct kndClassEntry *entry = elem;
    struct kndOutput *out = task->out;
    OUT("{", 1);
    OUT(entry->id, entry->id_size);
    OUT("}", 1);
    return knd_OK;
}

static int export_descendants(struct kndClass *cls, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;

    OUTF("{num-desc %zu}", cls->descendants->num_elems);

    if (cls->num_descendants == cls->num_children) return knd_OK;
    if (cls->num_descendants > KND_MAX_DESCENDANTS_IDX_SIZE) {
        return knd_OK;
    }

    OUT("[desc", strlen("[desc"));
    err = knd_set_map(cls->descendants, NULL, NULL, NULL, export_class_ref, NULL, task);
    KND_TASK_ERR("failed to export descendants");
    OUT("]", 1);

    return knd_OK;
}

static int export_class_body_commits(struct kndClass *cls,
                                     struct kndClassCommit *unused_var(class_commit),
                                     struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndState *state = cls->states;
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

    err = export_glosses(cls, out);                                          RET_ERR();

    if (cls->base_preds) {
        err = export_base_preds(cls, task);
        RET_ERR();
    }

    if (cls->attrs) {
        FOREACH (attr, cls->attrs) {
            err = knd_attr_export(attr, KND_FORMAT_GSP, task);
            if (err) return err;
        }
    }    
    return knd_OK;
}

int knd_class_export_commits_GSP(struct kndClass *cls, struct kndClassCommit *class_commit,
                                 struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndCommit *commit = class_commit->commit;
    struct kndState *state = cls->states;
    int err;
    
    err = out->writec(out, '{');                                                  RET_ERR();
    err = out->write(out, cls->entry->id, cls->entry->id_size);                 RET_ERR();
    err = out->write(out, "{_n ", strlen("{_n "));                                RET_ERR();
    err = out->write(out, cls->name, cls->name_size);                           RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();

    err = out->write(out, "{_st", strlen("{_st"));                                RET_ERR();

    if (state && state->commit == commit) {
        err = out->writec(out, ' ');                                              RET_ERR();

        // TODO
        //err = out->write(out, state->id, state->id_size);                         RET_ERR();

        /* any commits of the class body? */
        err = export_class_body_commits(cls, class_commit, task);                 RET_ERR();
    }

    /*    if (cls->inst_states) {
        state = cls->inst_states;
        if (state->commit == commit) {
            err = export_class_inst_commits(cls, class_commit, task);             RET_ERR();
        }
        }*/

    err = out->writec(out, '}');                                                  RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();
    return knd_OK;
}

int knd_class_export_GSP(struct kndClass *cls, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size = 0;
    struct kndClassEntry *entry = cls->entry;
    int err;

    assert(entry->seq != NULL);
    assert(out != NULL);

    out->reset(out);
    knd_uid_create(entry->seq->numid, idbuf, &idbuf_size);
    OUT(idbuf, idbuf_size);

    if (cls->entry->glosses) {
        err = export_glosses(cls, out);
        KND_TASK_ERR("failed to export glosses");
    }

    if (cls->base_preds) {
        err = export_base_preds(cls, task);
        KND_TASK_ERR("failed to export baseclass attr stms");
    }

    if (cls->attrs) {
        err = export_attrs(cls, task);
        KND_TASK_ERR("failed to export attrs GSP");
    }

    if (cls->num_ancestors) {
        err = export_ancestors(cls, task);
        KND_TASK_ERR("failed to export ancestors GSP");
    }

    if (cls->num_children) {
        err = export_children(cls, task);
        KND_TASK_ERR("failed to export children GSP");
    }

    /* export descendants - check the max limit */
    if (cls->descendants) {
        err = export_descendants(cls, task);
        KND_TASK_ERR("failed to export descendants GSP");
    }

    if (DEBUG_CLASS_ENCODE_LEVEL_3) {
        knd_log(">> GSP export of {cls %.*s {id %.*s} {seq %.*s}} {rec %.*s {size %zu}}",
                entry->name_size, entry->name, entry->id_size, entry->id,
                idbuf_size, idbuf, out->buf_size, out->buf, out->buf_size);
    }

    return knd_OK;
}

int knd_cls_name_marshall(void *elem, void *unused_var(ctx), struct kndStorageLeaf *leaf,
                          size_t *output_size, struct kndTask *task)
{
    struct kndClassEntry *entry = elem;
    struct kndOutput *out = task->out;
    int err;

    out->reset(out);
    OUT(entry->name, entry->name_size);
    OUT("{id ", strlen("{id "));
    OUT(entry->id, entry->id_size);
    OUT("}", strlen("}"));

    if (DEBUG_CLASS_ENCODE_LEVEL_3) {
        knd_log("== {cls %.*s {id %.*s}} {GSP {size %zu}}", 
                entry->name_size,  entry->name, 
                entry->id_size, entry->id, out->buf_size);
    }

    switch (task->mode) {
    case KND_TASK_TRACE_MODE:
        //knd_log(".. write {cls %.*s} to {filepath %.*s}", entry->name_size, entry->name,
        //        leaf->filepath_size, leaf->filepath);
        break;
    default:
        err = knd_append_file((const char*)leaf->filepath, out->buf, out->buf_size);
        KND_TASK_ERR("cls name write failure");
        leaf->curr_size += out->buf_size;
        break;
    }

    *output_size = out->buf_size;
    return knd_OK;
}

int knd_cls_entry_ref_marshall(void *elem, void *unused_var(ctx), struct kndStorageLeaf *leaf,
                               size_t *output_size, struct kndTask *task)
{
    char idbuf[KND_ID_SIZE];
    size_t id_size = 0;
    struct kndClassEntry *entry = elem;
    struct kndOutput *out = task->out;
    int err;

    out->reset(out);
    knd_uid_create(entry->seq->numid, idbuf, &id_size);
    OUT(idbuf, id_size);
    OUT("{id ", strlen("{id "));
    OUT(entry->id, entry->id_size);
    OUT("}", strlen("}"));

    switch (task->mode) {
    case KND_TASK_TRACE_MODE:
        //knd_log(".. write {cls %.*s} to {filepath %.*s}", entry->name_size, entry->name,
        //        leaf->filepath_size, leaf->filepath);
        break;
    default:
        err = knd_append_file((const char*)leaf->filepath, out->buf, out->buf_size);
        KND_TASK_ERR("cls name write failure");
        leaf->curr_size += out->buf_size;
        break;
    }

    *output_size = out->buf_size;
    return knd_OK;
}

int knd_class_marshall(void *elem, void *ctx, struct kndStorageLeaf *leaf,
                       size_t *output_size, struct kndTask *task)
{
    struct kndClassEntry *entry = elem;
    struct kndClass *c;
    struct kndOutput *out = task->out;
    struct kndRepoSnapshot *snapshot = ctx;
    int err;

    assert (out != NULL);

    err = knd_class_acquire(entry, &c, snapshot, task);
    KND_TASK_ERR("failed to acquire {cls %.*s}", entry->name_size, entry->name);

    err = knd_class_export_GSP(c, task);
    KND_TASK_ERR("failed to export GSP of {cls %.*s}", c->name_size, c->name);

    if (out->buf_size > leaf->max_size - leaf->curr_size) {
        err = knd_LIMIT;
        KND_TASK_ERR("leaf output limit reached {leaf {max-size %zu} {curr-size %zu}}",
                     leaf->max_size, leaf->curr_size);
    }

    switch (task->mode) {
    case KND_TASK_TRACE_MODE:
        //knd_log(".. write cls GSP to {filepath %.*s}",
        //        leaf->filepath_size, leaf->filepath);
        break;
    default:
        err = knd_append_file((const char*)leaf->filepath, out->buf, out->buf_size);
        KND_TASK_ERR("cls GSP write failure");
        leaf->curr_size += out->buf_size;
    }

    *output_size = out->buf_size;
    return knd_OK;
}

int knd_cls_facet_key_encode(void *key, void *unused_var(ctx),
                             struct kndTask *task)
{
    struct kndClassEntry *entry = key;
    struct kndOutput *out = task->out;

    if (DEBUG_CLASS_ENCODE_LEVEL_2) {
        knd_log(".. building GSP of facet key {cls %.*s}", entry->name_size, entry->name);
    }

    OUT(entry->id, entry->id_size);

    return knd_OK;
}
