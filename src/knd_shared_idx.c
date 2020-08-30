#include "knd_shared_idx.h"

#include "knd_utils.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include <alloca.h>

// Operations with mask:
#define MASK_MARK_BIT(mask, bit)    (mask |  (UINT64_C(1) << bit))
#define MASK_UNMARK_BIT(mask, bit)  (mask & ~(UINT64_C(1) << bit))
#define MASK_TEST_BIT(mask, bit)    (mask &  (UINT64_C(1) << bit))
#define MASK_LSBIT(mask)            __builtin_ctzll(mask & (~mask + 1))
#define MASK_CLEAR_LSBIT(mask)      (mask &  (mask - 1))

static inline uint8_t knd_shared_idx_key_bit(const char *key, size_t key_size __attribute__((unused))) {
    assert(key_size != 0);
    int bit = obj_id_base[(unsigned char)*key];
    assert(bit >= 0);
    return (uint8_t)bit;
}

static inline struct kndSharedIdxFolder *knd_shared_idx_folder_new(void) {
    return calloc(1, sizeof(struct kndSharedIdxFolder));
}

static int intersect(struct kndSharedIdxFolder *result, struct kndSharedIdxFolder **operands, size_t num_operands)
{
    assert(!result->elems_mask && !result->folders_mask);  // |result| isn't empty!!

    int err;
    struct kndSharedIdxFolder **suboperands = alloca(sizeof(struct kndSharedIdxFolder *) * num_operands);

    result->elems_mask = operands[0]->elems_mask;
    result->folders_mask = operands[0]->folders_mask;
    for (size_t i = 1; i < num_operands; ++i) {
        result->elems_mask &= operands[i]->elems_mask;
        result->folders_mask &= operands[i]->folders_mask;
    }

    for (uint64_t folders_mask = result->folders_mask; folders_mask; folders_mask = MASK_CLEAR_LSBIT(folders_mask)) {
        uint8_t folder_bit = MASK_LSBIT(folders_mask);

        struct kndSharedIdxFolder *subresult = result->folders[folder_bit] = knd_shared_idx_folder_new();
        if (subresult == NULL) return knd_NOMEM;

        for (size_t i = 0; i < num_operands; ++i)
            suboperands[i] = operands[i]->folders[folder_bit];

        err = intersect(subresult, suboperands, num_operands);
        if (err) return err;

        if (!subresult->elems_mask && !subresult->folders_mask) {
            result->folders_mask = MASK_UNMARK_BIT(result->folders_mask, folder_bit);
            // We can also free |subresult| to reduce memory usage.
        }
    }
    return 0;
}

int knd_shared_idx_add(struct kndSharedIdx *self, const char *key, size_t key_size)
{
    assert(key != NULL && key_size != 0);

    struct kndSharedIdxFolder *folder = &self->root;
    while (key_size != 1) {
        uint8_t folder_bit = knd_shared_idx_key_bit(key, key_size);

        struct kndSharedIdxFolder *next_folder = folder->folders[folder_bit];
        if (!next_folder) {
            next_folder = knd_shared_idx_folder_new();
            if (next_folder == NULL) return knd_NOMEM;

            folder->folders_mask = MASK_MARK_BIT(folder->folders_mask, folder_bit);
            folder->folders[folder_bit] = next_folder;
        }

        key++;
        key_size--;
        folder = next_folder;
    }

    folder->elems_mask = MASK_MARK_BIT(folder->elems_mask, knd_shared_idx_key_bit(key, key_size));
    atomic_store_explicit(&folder->elems_mask, , memory_order_relaxed);
    atomic_fetch_add_explicit(&folder->num_elems, 1, memory_order_relaxed);
    return 0;
}

bool knd_shared_idx_exist(struct kndSharedIdx *self, const char *key, size_t key_size)
{
    assert(key != NULL && key_size != 0);

    struct kndSharedIdxFolder *folder = &self->root;
    while (key_size != 1 &&
           (folder = folder->folders[knd_shared_idx_key_bit(key++, key_size--)])) {
        // empty body
    }
    return folder && MASK_TEST_BIT(folder->elems_mask, knd_shared_idx_key_bit(key, key_size));
}

int knd_shared_idx_new(struct kndSharedIdx **out)
{
    *out = calloc(1, sizeof(struct kndSharedIdx));
    return *out ? 0 : knd_NOMEM;
}

int knd_shared_idx_intersect(struct kndSharedIdx **out, struct kndSharedIdx **idxs, size_t num_idxs)
{
    int err = knd_shared_idx_new(out);
    if (err) return *out = NULL, err;

    // That's why I'm casting |idxs| to struct kndSharedIdxFolder **
    _Static_assert(offsetof(struct kndSharedIdx, root) == 0, "Illegal cast of idxs to (struct kndSharedIdxFolder **)");
    return intersect(&(*out)->root, (struct kndSharedIdxFolder **)idxs, num_idxs);
}
