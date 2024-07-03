#pragma once

#include "knd_config.h"

struct kndMemBlock {
    size_t numid;

    char *buf;
    size_t buf_size;

    size_t capacity;
    struct kndMemBlock *next;
};

int knd_memblock_new(struct kndMemBlock **result, size_t numid, size_t capacity);
int knd_memblock_copy(struct kndMemBlock *self, const char *input, size_t input_size);
int knd_memblock_write(struct kndMemBlock *self, const char *buf, size_t buf_size);
int knd_memblock_read_file(struct kndMemBlock *self, const char *filename, size_t filename_size);
void knd_memblock_free(struct kndMemBlock *self);
