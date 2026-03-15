#pragma once

#include "knd_config.h"

struct kndMemBlock;
struct kndTask;

struct kndMemBlock {
    size_t numid;

    char *buf;
    size_t buf_size;

    size_t capacity;
    size_t free_space;

    struct kndCharSeq *seqs;
    struct kndCharSeq *seq_tail;
    size_t num_seqs;

    struct kndMemBlock *next;
    struct kndMemBlock *prev;
};

int knd_memblock_new(struct kndMemBlock **result, size_t numid, size_t capacity);
int knd_memblock_copy(struct kndMemBlock *self, const char *input, size_t input_size);
int knd_memblock_write(struct kndMemBlock *self, const char *buf, size_t buf_size,
                       bool separ_needed, const char **result);
int knd_memblock_read_file(struct kndMemBlock *self, const char *filename, size_t file_size,
                           bool separ_needed, const char **result);
void knd_memblock_free(struct kndMemBlock *self);

int knd_memblock_fetch(struct kndMemBlock **result, size_t space_required, struct kndTask *task);
