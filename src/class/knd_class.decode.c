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
#include "knd_memblock.h"
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
#include "knd_ignore.h"
#include "knd_shared_dict.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
#include "knd_shared_set.h"
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#include <gsl-parser.h>

#define DEBUG_CLASS_DECODE_LEVEL_1 0
#define DEBUG_CLASS_DECODE_LEVEL_2 0
#define DEBUG_CLASS_DECODE_LEVEL_3 0
#define DEBUG_CLASS_DECODE_LEVEL_4 0
#define DEBUG_CLASS_DECODE_LEVEL_5 0
#define DEBUG_CLASS_DECODE_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepoSnapshot *snapshot;
    struct kndClass *class;
    struct kndClass *baseclass;
    struct kndClassBasePred *base_pred;
};

static int inherit_attr(void *elem, void *ctx_obj, struct kndTask *task)
{
    struct kndAttrRef *src_ref = elem;
    struct LocalContext *ctx = ctx_obj;
    struct kndMemPool *mempool = task->mempool;
    struct kndClass   *self = ctx->class;
    struct kndSet     *attr_idx = self->attr_idx;
    struct kndAttr    *attr    = src_ref->attr;
    struct kndAttrRef *ref = NULL;
    int err;

    err = knd_set_get(attr_idx, attr->id, attr->id_size, (void**)&ref, task);
    if (!err) {
        if (DEBUG_CLASS_DECODE_LEVEL_2) {
            knd_log("..  {attr %.*s {id %.*s}} already active in {cls %.*s}..",
                    attr->name_size, attr->name, attr->id_size, attr->id,
                    self->name_size, self->name);
        }

        /* override an existing attr stm */
        if (ref->attr_stm && src_ref->attr_stm) {
            if (DEBUG_CLASS_DECODE_LEVEL_3) {
                knd_log("..  {stm %.*s {id %.*s}} already set in \"%.*s\" => %.*s",
                        attr->name_size, attr->name, attr->id_size, attr->id,
                        self->name_size, self->name, ref->attr_stm->val_size, ref->attr_stm->val);
                knd_log("override with new val: %.*s",
                            src_ref->attr_stm->val_size, src_ref->attr_stm->val);
            }
            ref->attr_stm = src_ref->attr_stm;
            ref->cls_entry = src_ref->cls_entry;
            return knd_OK;
        }
    }

    if (DEBUG_CLASS_DECODE_LEVEL_3) {
        knd_log("..  {attr %.*s {id %.*s}} inherited by {cls %.*s}",
                attr->name_size, attr->name, attr->id_size, attr->id,
                self->name_size, self->name);
    }
    if (ref) {
        if (src_ref->attr_stm) {
            ref->attr_stm = src_ref->attr_stm;
            ref->cls_entry = src_ref->cls_entry;
        }
        return knd_OK;
    }

    err = knd_attr_ref_new(&ref, mempool);
    KND_TASK_ERR("failed to alloc an attr ref err:%d", err);

    ref->attr = attr;
    ref->attr_stm = src_ref->attr_stm;
    ref->cls_entry = src_ref->cls_entry;

    err = knd_set_add(attr_idx, attr->id, attr->id_size, (void*)ref, task);
    KND_TASK_ERR("failed to update attr idx of %.*s", self->name_size, self->name);

    return knd_OK;
}

static int inherit_attrs(struct kndClass *c, struct kndClass *base, struct kndTask *task)
{
    if (DEBUG_CLASS_DECODE_LEVEL_2) {
        knd_log(".. {cls %.*s} to inherit attrs from {cls %.*s} {total %zu}",
                c->entry->name_size, c->entry->name,
                base->name_size, base->name, base->attr_idx->num_elems);
    }
    struct LocalContext ctx = {
        .task = task,
        .class = c,
        .baseclass = base
    };
    int err;

    err = knd_set_map(base->attr_idx, NULL, NULL, NULL, inherit_attr, (void*)&ctx, task);
    KND_TASK_ERR("{cls %.*s} failed to inherit attrs from {cls %.*s}",
                 c->name_size, c->name, base->name_size, base->name);
    return knd_OK;
}

static int decode_baseclasses(struct kndClass *c, struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    struct kndClassBasePred *bp;
    struct kndClass *base;
    int err;

    assert (c->entry != NULL);

    if (DEBUG_CLASS_DECODE_LEVEL_2) {
        knd_log("{cls %.*s {id %.*s}} to decode its bases {num-base-preds %zu}",
                c->name_size, c->name, c->entry->id_size, c->entry->id,
                c->num_base_preds);
    }

    // TODO root class
    if (c->entry->id_size == 1 && *c->entry->id == '1') {
        c->phase = KND_CLASS_BASE_DECODED;
        return knd_OK;
    }

    if (c->phase >= KND_CLASS_BASE_DECODED) {
        knd_log("-- vicious circle detected in decoding bases of {cls %.*s}",
                c->name_size, c->name);
        return knd_FAIL;
    }

    FOREACH (bp, c->base_preds) {
        if (bp->is_root) break;

        err = knd_get_cls_entry_by_id(snapshot, bp->id, bp->id_size, &bp->entry, task);
        KND_TASK_ERR("failed to get {cls %.*s}", bp->id_size, bp->id);

        assert (bp->entry->name_size != 0);

        err = knd_class_acquire(bp->entry, &base, snapshot, task);
        KND_TASK_ERR("failed to acquire {cls %.*s}", bp->entry->name_size, bp->entry->name);

        if (base->phase < KND_CLASS_BASE_DECODED) {
            err = decode_baseclasses(base, snapshot, task);
            KND_TASK_ERR("failed to decode base classes of {cls %.*s}",
                         base->name_size, base->name);
        }

        /* check if subclass is already registered in the base class */
        /*err = knd_class_is_direct_child(base, c, &numid);
        if (err != knd_NO_MATCH) {
            if (DEBUG_CLASS_DECODE_LEVEL_2) {
                knd_log("-- child {cls %.*s} already registered in base {cls %.*s}?",
                        c->name_size, c->name, base->name_size, base->name);
            }
            continue;
        }
        err = knd_class_link_base(c, base, snapshot, task);
        KND_TASK_ERR("failed to link {cls %.*s} to base {cls %.*s}",
                     c->name_size, c->name, base->name_size, base->name);
        */
    }

    if (DEBUG_CLASS_DECODE_LEVEL_3) {
        knd_log("++ {cls %.*s {id %.*s}} bases decoded!",
                c->name_size, c->name, c->entry->id_size,
                c->entry->id, c->entry->numid);
    }

    c->phase = KND_CLASS_BASE_DECODED;
    return knd_OK;
}

static int decode_attr_refs(struct kndClass *cls, struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    struct kndAttrRef *ref;
    int err;

    FOREACH (ref, cls->attr_refs) {
        err = knd_attr_ref_decode(ref, snapshot, task);
        KND_TASK_ERR("failed to decode {cls %.*s {attr %.*s}}",
                     cls->name_size, cls->name, ref->id_size, ref->id);

        assert (ref->id_size != 0);

        if (DEBUG_CLASS_DECODE_LEVEL_3) {
            knd_log(".. {cls %.*s} to register {attr %.*s}",
                    cls->name_size, cls->name, ref->id_size, ref->id);
        }

        err = knd_set_add(cls->attr_idx, ref->id, ref->id_size, (void*)ref, task);
        KND_TASK_ERR("failed to register {cls %.*s {attr %.*s}}",
                     cls->name_size, cls->name, ref->attr->name_size, ref->attr->name);
    }
    return knd_OK;
}

static int decode_base_preds(struct kndClass *cls, struct kndRepoSnapshot *snapshot,
                             struct kndTask *task)
{
    struct kndClassBasePred *bp;
    struct kndClass *base;
    int err;

    FOREACH (bp, cls->base_preds) {
        if (bp->is_root) break;

        assert (bp->entry != NULL);

        err = knd_class_acquire(bp->entry, &base, snapshot, task);
        KND_TASK_ERR("failed to acquire {base %.*s} of {cls %.*s}",
                     bp->entry->name_size, bp->entry->name, cls->name_size, cls->name);

        err = inherit_attrs(cls, base, task);
        KND_TASK_ERR("failed to inherit attrs from {cls %.*s}",
                     base->name_size, base->name);
        if (bp->attr_stms) {
            err = knd_decode_attr_stms(base, bp->attr_stms, snapshot, task);
            KND_TASK_ERR("failed to decode attr stms of {cls %.*s}",
                         base->name_size, base->name);
        }
    }
    return knd_OK;
}

int knd_class_decode(struct kndClass *c, struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    int err;

    if (DEBUG_CLASS_DECODE_LEVEL_TMP) {
        knd_log(">> decoding {cls %.*s {num-bases %zu}}",
                c->name_size, c->name, c->num_base_preds);
    }

    if (c->phase >= KND_CLASS_DECODED) {
        knd_log("-- vicious circle detected in decoding {cls %.*s}", c->name_size, c->name);
        return knd_FAIL;
    }

    if (c->phase < KND_CLASS_BASE_DECODED) {
        err = decode_baseclasses(c, snapshot, task);
        KND_TASK_ERR("failed to decode base classes of {cls %.*s}", c->name_size, c->name);
    }

    err = decode_attr_refs(c, snapshot, task);
    KND_TASK_ERR("failed to decode attr refs of {cls %.*s}", c->name_size, c->name);

    err = decode_base_preds(c, snapshot, task);
    KND_TASK_ERR("failed to decode base preds of {cls %.*s}", c->name_size, c->name);

    c->phase = KND_CLASS_DECODED;
    return knd_OK;
}

static int decode_glosses(struct kndClassEntry *entry, struct kndSet *str_idx,
                          struct kndTask *task)
{
    struct kndText *t;
    int err;

    FOREACH (t, entry->glosses) {
        err = knd_charseq_decode(str_idx, t->id, t->id_size, &t->seq, task);
        KND_TASK_ERR("failed to decode {cls %.*s {gloss %.*s}}",
                     entry->name_size, entry->name, t->id_size, t->id);
    }
    return knd_OK;
}

int knd_cls_entry_decode(struct kndClassEntry *entry, struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    struct kndSet *str_idx = snapshot->cache.str_idx;
    struct kndCharSeq *seq;
    int err;

    err = knd_charseq_decode(str_idx, entry->name_id, entry->name_id_size, &seq, task);
    KND_TASK_ERR("failed to decode cls entry name {id %.*s}",
                 entry->name_id_size, entry->name_id);

    entry->name = seq->val;
    entry->name_size = seq->val_size;
    entry->seq = seq;

    assert (entry->name_size != 0);

    err = decode_glosses(entry, str_idx, task);
    KND_TASK_ERR("failed to decode glosses of {cls %.*s}", entry->name_size, entry->name);
  
    return knd_OK;
}
