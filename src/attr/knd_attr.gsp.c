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


static int export_glosses(struct kndAttr *self, struct kndOutput *out)
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
        OUT("}}", 2);
    }
    OUT("]", 1);
    return knd_OK;
}

int knd_attr_names_marshall(void *elem, size_t *output_size, struct kndTask *task)
{
    struct kndSharedDictItem *item, *items = elem;
    struct kndAttrRef *attr_ref, *attr_refs;
    struct kndAttr *attr;
    struct kndClassEntry *entry;
    struct kndOutput *out = task->out;
    size_t orig_size = out->buf_size;

    OUT("[n", strlen("[n"));

    FOREACH (item, items) {
        attr_refs = item->data;

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
    }
    OUT("]", strlen("]"));

    *output_size = out->buf_size - orig_size;
    return knd_OK;
}

int knd_attr_export_GSP(struct kndAttr *attr, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    const char *type_name = knd_attr_names[attr->type];
    size_t type_name_size = strlen(knd_attr_names[attr->type]);
    struct kndClassRefAttr *cls_ref_attr;
    struct kndClassInnerAttr *cls_inner_attr;
    struct kndClassEntry *entry;
    int err;

    OUT("{", 1);
    OUT(type_name, type_name_size);

    OUT(" ", 1);
    OUT(attr->id, attr->id_size);

    if (attr->is_a_set) {
        OUT("{t set}", strlen("{t set}"));
    }

    if (attr->is_required) {
        OUT("{req}", strlen("{req}"));
    }

    if (attr->is_unique) {
        OUT("{uniq}", strlen("{uniq}"));
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

    if (attr->ref_proc_name_size) {
        OUT("{p ", strlen("{p "));
        OUT(attr->ref_proc_name, attr->ref_proc_name_size);
        OUT("}", 1);
    }

    /* choose gloss */
    if (attr->tr) {
        err = export_glosses(attr, out);
        KND_TASK_ERR("failed to export glosses GSP");
    }

    OUT("}", 1);
    return knd_OK;
}
