#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_class_inst.h"
#include "knd_class.h"
#include "knd_mempool.h"
#include "knd_attr.h"
#include "knd_elem.h"
#include "knd_repo.h"

#include "knd_text.h"
#include "knd_num.h"
#include "knd_rel.h"
#include "knd_set.h"
#include "knd_output.h"

#include "knd_user.h"
#include "knd_state.h"

#include <gsl-parser.h>

#define DEBUG_INST_LEVEL_1 0
#define DEBUG_INST_LEVEL_2 0
#define DEBUG_INST_LEVEL_3 0
#define DEBUG_INST_LEVEL_4 0
#define DEBUG_INST_LEVEL_TMP 1

void knd_class_inst_str(struct kndClassInst *self, size_t depth)
{
    struct kndElem *elem;
    struct kndState *state = self->states;

    if (self->type == KND_OBJ_ADDR) {
        knd_log("\n%*sClass Instance \"%.*s::%.*s\"  numid:%zu",
                depth * KND_OFFSET_SIZE, "",
                self->base->name_size, self->base->name,
                self->name_size, self->name,
                self->entry->numid);
        if (state) {
            knd_log("    state:%zu  phase:%d",
                    state->numid, state->phase);
        }
    }

    for (elem = self->elems; elem; elem = elem->next) {
        knd_elem_str(elem, depth + 1);
    }
}

int knd_class_inst_export(struct kndClassInst *self, knd_format format,
                          struct kndTask *task)
{
    switch (format) {
        case KND_FORMAT_JSON:
            return knd_class_inst_export_JSON(self, task);
        case KND_FORMAT_GSP:
            return knd_class_inst_export_GSP(self, task);
        default:
            return knd_RANGE;
    }
}

int knd_class_inst_entry_new(struct kndMemPool *mempool,
                             struct kndClassInstEntry **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                            sizeof(struct kndClassInstEntry), &page);  RET_ERR();
    *result = page;
    return knd_OK;
}

int knd_class_inst_new(struct kndMemPool *mempool,
                       struct kndClassInst **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL_X2,
                            sizeof(struct kndClassInst), &page);                  RET_ERR();
    *result = page;
    return knd_OK;
}

int knd_class_inst_mem(struct kndMemPool *mempool,
                       struct kndClassInst **result)
{
    void *page;
    int err;
    err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_SMALL_X2,
                                 sizeof(struct kndClassInst), &page);                  RET_ERR();
    *result = page;
    return knd_OK;
}
