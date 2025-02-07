#include "knd_commit.h"
#include "knd_class.h"
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_text.h"
#include "knd_repo.h"
#include "knd_user.h"
#include "knd_set.h"
#include "knd_shared_set.h"
#include "knd_output.h"

#include <gsl-parser.h>

#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdatomic.h>

#define DEBUG_CLASS_QUERY_LEVEL_1 0
#define DEBUG_CLASS_QUERY_LEVEL_2 0
#define DEBUG_CLASS_QUERY_LEVEL_3 0
#define DEBUG_CLASS_QUERY_LEVEL_4 0
#define DEBUG_CLASS_QUERY_LEVEL_5 0
#define DEBUG_CLASS_QUERY_LEVEL_TMP 1


int knd_class_match_query(struct kndClass *self, struct kndAttrStm *query)
{
    struct kndSet *attr_idx = self->attr_idx;
    knd_logic_t logic = query->logic;
    struct kndAttrRef *attr_ref;
    struct kndAttrStm *attr_stm;
    struct kndAttr *attr;
    void *result;
    int err;

    FOREACH (attr_stm, query->children) {
        attr = attr_stm->attr;

        err = attr_idx->get(attr_idx, attr->id, attr->id_size, &result);
        if (err) {
            knd_log("-- attr \"%.*s\" not present in %.*s?",
                    self->name_size, self->name);
            return err;
        }
        attr_ref = result;

        if (!attr_ref->attr_stm) {
            return knd_OK;
        }

        /* _null value expected */
        if (!attr_stm->val || !attr_stm->numval) {
            return knd_NO_MATCH;
        }
        err = knd_attr_stm_match(attr_ref->attr_stm, attr_stm);
        if (err == knd_NO_MATCH) {
            switch (logic) {
                case KND_LOGIC_AND:
                    return knd_NO_MATCH;
                default:
                    break;
            }
            continue;
        }
        /* got a match */
        switch (logic) {
            case KND_LOGIC_OR:
                return knd_OK;
            default:
                break;
        }
    }

    switch (logic) {
        case KND_LOGIC_OR:
            return knd_NO_MATCH;
        default:
            break;
    }

    return knd_OK;
}

