#include "knd_set_idx.h"

#include "knd_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <alloca.h>

static inline uint8_t knd_set_idx_key_bit(const char *key, size_t key_size) {
    assert(key_size != 0);
    int bit = obj_id_base[(unsigned char)*key];
    assert(bit >= 0);
    return (uint8_t)bit;
}

inline struct kndSetIdxFolder *knd_set_idx_folder_new(void) {
    return calloc(1, sizeof(struct kndSetIdxFolder));
}

inline void knd_set_idx_folder_init(struct kndSetIdxFolder *self)
{
    memset(self, 0, sizeof *self);
}

inline void knd_set_idx_folder_mark_elem(struct kndSetIdxFolder *self, uint8_t elem_bit)
{
    self->elems_idx |= (1 << elem_bit);
}

bool knd_set_idx_folder_test_elem(struct kndSetIdxFolder *self, uint8_t elem_bit) {
    return self->elems_idx & (1 << elem_bit);
}

void knd_set_idx_folder_mark_folder(struct kndSetIdxFolder *self, uint8_t folder_bit, struct kndSetIdxFolder *folder) {
    self->folders_idx |= (1 << folder_bit);
    self->folders[folder_bit] = folder;
}

void knd_set_idx_folder_unmark_folder(struct kndSetIdxFolder *self, uint8_t folder_bit)
{
    self->folders_idx &= ~(1 << folder_bit);
}

void knd_set_idx_init(struct kndSetIdx *self) { memset(self, 0, sizeof *self); }

int knd_set_idx_add(struct kndSetIdx *self, const char *key, size_t key_size)
{
    assert(key != NULL && key_size != 0);

    struct kndSetIdxFolder *folder = &self->root;
    while (key_size != 1) {
        uint8_t key_bit = knd_set_idx_key_bit(key, key_size);

        struct kndSetIdxFolder *child = folder->folders[key_bit];
        if (!child) {
            child = knd_set_idx_folder_new();
            if (child == NULL) return knd_NOMEM;

            knd_set_idx_folder_mark_folder(folder, key_bit, child);
        }

        key++;
        key_size--;
        folder = child;
    }

    knd_set_idx_folder_mark_elem(folder, knd_set_idx_key_bit(key, key_size));
    return 0;
}

bool knd_set_idx_exist(struct kndSetIdx *self, const char *key, size_t key_size)
{
    assert(key != NULL && key_size != 0);

    struct kndSetIdxFolder *folder = &self->root;
    while (key_size != 1 &&
           (folder = folder->folders[knd_set_idx_key_bit(key++, key_size--)])) {
        // empty body
    }

    return folder && knd_set_idx_folder_test_elem(folder, knd_set_idx_key_bit(key, key_size));
}

#define LSBIT(X)                    (__builtin_ctz((X) & (~(X) + 1)))
#define CLEARLSBIT(X)               ((X) & ((X) - 1))

int knd_set_idx_folder_intersect(struct kndSetIdxFolder *self, struct kndSetIdxFolder *other,
                                 struct kndSetIdxFolder *result)
{
    int err;

    result->elems_idx = self->elems_idx & other->elems_idx;
    result->folders_idx = self->folders_idx & other->folders_idx;

    for (uint64_t folders_idx = result->folders_idx ; folders_idx; folders_idx = CLEARLSBIT(folders_idx)) {
        uint8_t folder_bit = LSBIT(folders_idx);

        struct kndSetIdxFolder **folder_ptr = &result->folders[folder_bit];
        *folder_ptr = knd_set_idx_folder_new();
        if (*folder_ptr == NULL) return knd_NOMEM;

        err = knd_set_idx_folder_intersect(self->folders[folder_bit], other->folders[folder_bit], *folder_ptr);
        if (err) return err;

        if (!(*folder_ptr)->elems_idx && !(*folder_ptr)->folders_idx) {
            knd_set_idx_folder_unmark_folder(result, folder_bit);
            // We can also free folder_ptr to reduce memory usage.
        }
    }

    return 0;
}

int knd_set_idx_intersect(struct kndSetIdx *self, struct kndSetIdx *other, struct kndSetIdx *out_result)
{
    return knd_set_idx_folder_intersect(&self->root, &other->root, &out_result->root);
}

int knd_set_idx_folder_intersect_n(struct kndSetIdxFolder **folders, size_t num_folders,
                                   struct kndSetIdxFolder *result)
{
    int err;
    struct kndSetIdxFolder **subfolders = alloca(sizeof(struct kndSetIdxFolder *) * num_folders);

    result->elems_idx = folders[0]->elems_idx;
    result->folders_idx = folders[0]->folders_idx;
    for (size_t i = 1; i < num_folders; ++i) {
        result->elems_idx &= folders[i]->elems_idx;
        result->folders_idx &= folders[i]->folders_idx;
    }

    for (uint64_t folders_idx = result->folders_idx ; folders_idx; folders_idx = CLEARLSBIT(folders_idx)) {
        uint8_t folder_bit = LSBIT(folders_idx);

        struct kndSetIdxFolder **folder_ptr = &result->folders[folder_bit];
        *folder_ptr = knd_set_idx_folder_new();
        if (*folder_ptr == NULL) return knd_NOMEM;

        for (size_t i = 0; i < num_folders; ++i)
            subfolders[i] = folders[i]->folders[folder_bit];

        err = knd_set_idx_folder_intersect(subfolders, num_folders, *folder_ptr);
        if (err) return err;

        if (!(*folder_ptr)->elems_idx && !(*folder_ptr)->folders_idx) {
            knd_set_idx_folder_unmark_folder(result, folder_bit);
            // We can also free folder_ptr to reduce memory usage.
        }
    }

    return 0;
}
