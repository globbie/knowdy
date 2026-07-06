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
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_state.h"
#include "knd_user.h"
#include "knd_repo.h"
#include "knd_mempool.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_shared_set.h"
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#define DEBUG_ATTR_GSP_LEVEL_1 0
#define DEBUG_ATTR_GSP_LEVEL_2 0
#define DEBUG_ATTR_GSP_LEVEL_3 0
#define DEBUG_ATTR_GSP_LEVEL_4 0
#define DEBUG_ATTR_GSP_LEVEL_5 0
#define DEBUG_ATTR_GSP_LEVEL_TMP 1

struct LocalContext {
    struct kndClassBasePred *class_var;
    struct kndAttr     *attr;
    struct kndRepo     *repo;
    struct kndTask     *task;
};

static int export_glosses(struct kndAttr *attr, struct kndOutput *out)
{
    char idbuf[KND_ID_SIZE];
    size_t id_size = 0;
    struct kndText *t;

    OUT("[g", strlen("[g"));
    FOREACH (t, attr->glosses) {
        OUT("{", 1);

        assert (t->locale != NULL);
        assert (t->seq != NULL);

        OUT(t->locale->id, t->locale->id_size);
        OUT("{t ", strlen("{t "));
        knd_uid_create(t->seq->numid, idbuf, &id_size);
        OUT(idbuf, id_size);
        OUT("}}", 2);
    }
    OUT("]", 1);
    return knd_OK;
}

int knd_attr_name_marshall(void *elem, void *unused_var(ctx),
                           struct kndStorageLeaf *leaf,
                           size_t *output_size, struct kndTask *task)
{
    struct kndAttrRef *attr_ref, *attr_refs = elem;
    struct kndAttr *attr;
    struct kndClassEntry *entry;
    struct kndOutput *out = task->out;
    int err;

    out->reset(out);

    attr = attr_refs->attr;
    OUT("{", strlen("{"));
    OUT(attr->name, attr->name_size);

    OUT("[a", strlen("[a"));

    FOREACH (attr_ref, attr_refs) {
        // TODO check commit version
        attr = attr_ref->attr;
        entry = attr->owner->entry;
        
        OUT("{", strlen("{"));
        OUT(attr->id, attr->id_size);
        
        OUT("{c ", strlen("{c "));
        OUT(entry->id, entry->id_size);
        OUT("}", strlen("}"));
        
        OUT("}", strlen("}"));
    }
    OUT("]", strlen("]"));
    OUT("}", strlen("}"));

    switch (task->mode) {
    case KND_TASK_TRACE_MODE:
        //knd_log(".. write {attr %.*s} to {filepath %.*s}", attr->name_size, attr->name,
        //        leaf->filepath_size, leaf->filepath);
        break;
    default:
        err = knd_append_file((const char*)leaf->filepath, out->buf, out->buf_size);
        KND_TASK_ERR("attr name write failure");
        leaf->curr_size += out->buf_size;
        break;
    }

    *output_size = out->buf_size;
    return knd_OK;
}

int knd_attr_export_GSP(struct kndAttr *attr, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size = 0;
    struct kndClassRefAttr *cls_ref_attr;
    struct kndClassInnerAttr *cls_inner_attr;
    struct kndClassEntry *entry;
    int err;

    assert(attr->seq != NULL);

    out->reset(out);
    knd_uid_create(attr->seq->numid, idbuf, &idbuf_size);
    OUT(idbuf, idbuf_size);

    OUT("{t ", strlen("{t "));
    OUTF("%d", attr->type);
    OUT("}", 1);

    switch (attr->mult_t) {
    case KND_ATTR_MULTIPLE:
        OUT("{m}", strlen("{m}"));
        break;
    default:
        break;
    }

    switch (attr->type) {
    case KND_ATTR_CLS_INNER:
        cls_inner_attr = attr->subtype;
        entry = cls_inner_attr->template_cls;
        OUT("{c ", strlen("{c "));
        OUT(entry->id, entry->id_size);
        OUT("}", 1);
        break;
    case KND_ATTR_CLS_REF:
        cls_ref_attr = attr->subtype;
        entry = cls_ref_attr->template_cls;
        OUT("{c ", strlen("{c "));
        OUT(entry->id, entry->id_size);
        OUT("}", 1);
        break;
    default:
        break;
    }

    if (attr->glosses) {
        err = export_glosses(attr, out);
        KND_TASK_ERR("failed to export glosses GSP");
    }

    OUT("}", 1);
    return knd_OK;
}

int knd_attr_marshall(void *elem, void *unused_var(ctx), struct kndStorageLeaf *leaf,
                      size_t *output_size, struct kndTask *task)
{
    struct kndAttrRef *attr_ref = elem;
    struct kndAttr *attr = attr_ref->attr;
    struct kndOutput *out = task->out;
    int err;

    assert (out != NULL);

    err = knd_attr_export(attr, KND_FORMAT_GSP, task);
    KND_TASK_ERR("failed to export {attr %.*s}", attr->name_size, attr->name);

    if (out->buf_size > leaf->max_size - leaf->curr_size) {
        err = knd_LIMIT;
        KND_TASK_ERR("leaf output limit reached {leaf {max-size %zu} {curr-size %zu}}",
                     leaf->max_size, leaf->curr_size);
    }

    switch (task->mode) {
    case KND_TASK_TRACE_MODE:
        break;
    default:
        err = knd_append_file((const char*)leaf->filepath, out->buf, out->buf_size);
        KND_TASK_ERR("attr GSP write failure");
        leaf->curr_size += out->buf_size;
    }

    *output_size = out->buf_size;
    return knd_OK;
}
