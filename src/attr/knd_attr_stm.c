#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_attr.h"
#include "knd_attr_stm.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_proc_call.h"
#include "knd_task.h"
#include "knd_class.h"
#include "knd_text.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_set.h"
#include "knd_mempool.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_ATTR_STM_LEVEL_1 0
#define DEBUG_ATTR_STM_LEVEL_2 0
#define DEBUG_ATTR_STM_LEVEL_3 0
#define DEBUG_ATTR_STM_LEVEL_4 0
#define DEBUG_ATTR_STM_LEVEL_5 0
#define DEBUG_ATTR_STM_LEVEL_TMP 1

void knd_attr_stm_str(struct kndAttrStm *stm, size_t depth)
{
    struct kndAttr *attr = stm->attr;
    if (stm->is_list_item)
        attr = stm->parent->attr;

    struct kndAttrStm *item;
    const char *type_name = "";

    assert (attr != NULL);

    type_name = knd_attr_names[attr->type];
    if (stm->is_list_item) {
        switch (attr->type) {
        case KND_ATTR_CLS_INNER:
            knd_log("%*s* {inner-class %.*s}", depth * KND_OFFSET_SIZE, "",
                    attr->cls_name_size, attr->cls_name);
            break;
        case KND_ATTR_CLS_REF:
            knd_log("%*s* {cls-ref %.*s}", depth * KND_OFFSET_SIZE, "",
                    attr->cls_name_size, attr->cls_name);
            break;
        default:
            knd_log("%*s* {%s %.*s}", depth * KND_OFFSET_SIZE, "",
                    type_name, stm->name_size, stm->name);
            break;
        }
        return;
    }

    if (attr->is_a_set) {
        knd_log("%*s%.*s (%s)  [", depth * KND_OFFSET_SIZE, "",
                stm->name_size, stm->name, type_name);

        FOREACH (item, stm->list)
            knd_attr_stm_str(item, depth + 1);

        knd_log("%*s]", depth * KND_OFFSET_SIZE, "");
        return;
    }

    switch (attr->type) {
        case KND_ATTR_CLS_INNER:
            knd_log("%*s%.*s (inner \"%.*s\")", depth * KND_OFFSET_SIZE, "",
                    stm->name_size, stm->name,
                    attr->cls_name_size, attr->cls_name);
            FOREACH (item, stm->children) {
                knd_attr_stm_str(item, depth + 1);
            }
            break;
        case KND_ATTR_CLS_REF:
            knd_log("%*s%.*s (\"%.*s\" class ref)", depth * KND_OFFSET_SIZE, "",
                    stm->name_size, stm->name,
                    attr->cls_name_size, attr->cls_name);
            return;
        case KND_ATTR_TEXT:
            knd_log("%*s%.*s:", depth * KND_OFFSET_SIZE, "", stm->name_size, stm->name);
            //knd_text_str(stm->text, depth + 1);
            return;
        default:
            knd_log("%*s%.*s (%s) => %.*s", depth * KND_OFFSET_SIZE, "",
                   stm->name_size, stm->name,  type_name, stm->val_size, stm->val);
            break;
    }
}

void knd_attr_stm_present_subj(void *obj, size_t depth)
{
    struct kndAttrStm *stm = obj;
    struct kndClass *subj = stm->subj;
    struct kndAttr *attr = stm->attr;

    knd_log("%*s{cls %.*s {%.*s %.*s}}", depth * KND_OFFSET_SIZE, "",
            subj->name_size, subj->name,
            stm->name_size, stm->name, stm->val_size, stm->val);

    /*
    switch (attr->type) {
    case KND_ATTR_CLS_INNER:
        break;
    case KND_ATTR_CLS_REF:
        break;
    case KND_ATTR_UINT:
        break;
    case KND_ATTR_UREAL:
        break;
    case KND_ATTR_STR:
        break;
    default:
        break;
    }
    */
}

int knd_cls_ref_attr_stm_new(struct kndClassRefAttrStm **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndClassRefAttrStm));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndClassRefAttrStm));
    *result = page;
    return knd_OK;
}

int knd_cls_inner_attr_stm_new(struct kndClassInnerAttrStm **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndClassInnerAttrStm));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndClassInnerAttrStm));
    *result = page;
    return knd_OK;
}

int knd_attr_stm_new(struct kndAttrStm **result, struct kndClass *subj, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->small_x4_page_size >= sizeof(struct kndAttrStm));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL_X4, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndAttrStm));
    *result = page;
    (*result)->subj = subj;
    return knd_OK;
}
