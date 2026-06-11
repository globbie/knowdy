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
#include "knd_dict.h"
#include "knd_user.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#include <gsl-parser.h>

#define DEBUG_ATTR_RESOLVE_LEVEL_1 0
#define DEBUG_ATTR_RESOLVE_LEVEL_2 0
#define DEBUG_ATTR_RESOLVE_LEVEL_3 0
#define DEBUG_ATTR_RESOLVE_LEVEL_4 0
#define DEBUG_ATTR_RESOLVE_LEVEL_5 0
#define DEBUG_ATTR_RESOLVE_LEVEL_TMP 1

static int check_attr_name_conflict(struct kndClass *self, struct kndAttr *attr_candidate,
                                    struct kndTask *task)
{
    struct kndAttrRef *attr_ref, *attr_refs;
    struct kndAttr *attr;
    void *obj;
    struct kndSet *attr_idx = self->attr_idx;
    struct kndDict *attr_name_idx = task->idxs.attr_name_idx;    
    int err;

    assert (attr_name_idx != NULL);

    if (DEBUG_ATTR_RESOLVE_LEVEL_2) {
        knd_log(".. checking attr name conflict: %.*s",
                attr_candidate->name_size, attr_candidate->name);
    }

    /* global attr name search */
    err = knd_dict_get(attr_name_idx, attr_candidate->name, attr_candidate->name_size,
                            (void**)&attr_refs, task);
    switch (err) {
    case knd_OK:
        break;
    case knd_NO_MATCH:
        return knd_OK;
    default:
        return err;
    }

    FOREACH (attr_ref, attr_refs) {
        attr = attr_ref->attr;
        err = knd_set_get(attr_idx, attr->id, attr->id_size, &obj, task);
        if (!err) {
            err = knd_CONFLICT;
            KND_TASK_ERR("{attr %.*s} already present in {cls %.*s}",
                         attr_candidate->name_size, attr_candidate->name,
                         self->name_size, self->name);
        }
    }
    return knd_OK;
}

int knd_attr_resolve(struct kndAttr *attr, struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    struct kndClassInnerAttr *cls_inner_attr;
    struct kndClassRefAttr *cls_ref_attr;
    const char *name;
    size_t name_size;
    int err;

    switch (attr->type) {
    case KND_ATTR_DATE:
        // TODO
        break;
    case KND_ATTR_STR:
        if (attr->format_cls_name_size) {
            name = attr->format_cls_name;
            name_size = attr->format_cls_name_size;

            err = knd_get_cls_entry_by_name(snapshot, name, name_size, &attr->format_cls_entry, task);
            KND_TASK_ERR("no such {cls %.*s}", name_size, name);
        }
        break;
    case KND_ATTR_CLS_INNER:
        cls_inner_attr = attr->subtype;
        if (!cls_inner_attr->cls_name_size) {
            err = knd_FAIL;
            KND_TASK_ERR("no template cls specified for {inner-attr %.*s}",
                         attr->name_size, attr->name);
        }
        name = cls_inner_attr->cls_name;
        name_size = cls_inner_attr->cls_name_size;

        err = knd_get_cls_entry_by_name(snapshot, name, name_size, &cls_inner_attr->template_cls, task);
        KND_TASK_ERR("no such {cls %.*s}", name_size, name);
        break;
    case KND_ATTR_CLS_REF:
        cls_ref_attr = attr->subtype;
        if (!cls_ref_attr->cls_name_size) {
            err = knd_FAIL;
            KND_TASK_ERR("no template cls specified for {attr %.*s}",
                         attr->name_size, attr->name);
        }
        name = cls_ref_attr->cls_name;
        name_size = cls_ref_attr->cls_name_size;

        err = knd_get_cls_entry_by_name(snapshot, name, name_size, &cls_ref_attr->template_cls, task);
        KND_TASK_ERR("no such {cls %.*s}", name_size, name);
        break;
    default:
        // TODO
        break;
    }
    return knd_OK;
}

int knd_resolve_primary_attrs(struct kndClass *cls, struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    struct kndAttr *attr;
    int err;

    if (DEBUG_ATTR_RESOLVE_LEVEL_2) {
        knd_log(".. resolving primary attrs of {cls %.*s {total-attrs %zu}}",
                cls->name_size, cls->name, cls->num_attrs);
    }

    FOREACH (attr, cls->attrs) {
        err = check_attr_name_conflict(cls, attr, task);
        KND_TASK_ERR("name conflict detected");

        err = knd_attr_resolve(attr, snapshot, task);
        KND_TASK_ERR("failed to resolve attr");

        err = knd_attr_register(attr, cls, task);
        KND_TASK_ERR("failed to register new attr");
    }
    return knd_OK;
}
