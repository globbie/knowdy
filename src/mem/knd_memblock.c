#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>

#include "knd_config.h"
#include "knd_memblock.h"
#include "knd_output.h"
#include "knd_task.h"
#include "knd_utils.h"

#define DEBUG_MEMBLOCK_LEVEL_0 0
#define DEBUG_MEMBLOCK_LEVEL_TMP 1

int knd_memblock_new(struct kndMemBlock **result, size_t numid, size_t capacity)
{
    struct kndMemBlock *block = malloc(sizeof(struct kndMemBlock));
    if (!block) return knd_NOMEM;
    block->init = malloc(capacity + 1);
    if (!block->init) {
        free(block);
        return knd_NOMEM;
    }
    block->capacity = capacity;
    block->numid = numid;

    block->buf = block->init;
    block->buf_size = 0;

    *result = block;
    return knd_OK;
}

int knd_memblock_copy(struct kndMemBlock *block, const char *input, size_t input_size)
{
    char *b = malloc(input_size + 1);
    if (!b) return knd_NOMEM;

    memcpy(b, input, input_size);
    b[input_size] = '\0';

    block->buf = b;
    block->buf_size = input_size;

    return knd_OK;
}

int knd_memblock_write(struct kndMemBlock *self, const char *buf, size_t buf_size,
                       const char **result)
{
    char *curr = self->buf + self->buf_size;

    if (buf_size > self->capacity - self->buf_size - 1)
        return knd_NOMEM;

    memcpy(curr, buf, buf_size);
    self->buf_size += buf_size;
    self->buf[self->buf_size] = '\0';

    *result = curr;
    return knd_OK;
}

int knd_memblock_fetch( struct kndMemBlock **result, size_t space_required, struct kndTask *task)
{
    struct kndMemBlock *block, *curr_block;
    int err;

    if (space_required >= KND_MEMBLOCK_BUF_SIZE) return knd_LIMIT;

    if (!task->blocks) {
        err = knd_memblock_new(&block, 0, KND_MEMBLOCK_BUF_SIZE);
        KND_TASK_ERR("failed to alloc a memblock");
        task->blocks = block;
        *result = block;
        return knd_OK;
    }

    curr_block = task->blocks;
    if ((curr_block->capacity - curr_block->buf_size) >= space_required) {
        *result = curr_block;
        return knd_OK;
    }

    err = knd_memblock_new(&block, 0, KND_MEMBLOCK_BUF_SIZE);
    KND_TASK_ERR("failed to alloc a memblock");
    block->next = curr_block;
    task->blocks = block;
    task->num_blocks++;
    *result = block;
    return knd_OK;
}

int knd_memblock_read_file(struct kndMemBlock *block, const char *filename, size_t file_size)
{
    FILE *file_stream;
    size_t read_size;

    if (file_size >= block->capacity) return knd_LIMIT;

    file_stream = fopen(filename, "r");
    if (!file_stream) return knd_IO_FAIL;
    read_size = fread(block->buf, 1, file_size, file_stream);
    if (!read_size) return knd_IO_FAIL;
    if (read_size != file_size) return knd_IO_FAIL;

    return knd_OK;
}
