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
    block->buf = malloc(capacity + 1);
    if (!block->buf) {
        free(block);
        return knd_NOMEM;
    }
    block->numid = numid;
    block->capacity = capacity;
    block->free_space = capacity;
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
                       bool separ_needed, const char **result)
{
    char *curr = self->buf + self->buf_size;

    if (buf_size > self->capacity - self->buf_size - 1)
        return knd_NOMEM;

    memcpy(curr, buf, buf_size);
    self->buf_size += buf_size;

    if (separ_needed) {
        self->buf[self->buf_size] = '\0';
        self->buf_size++;
    }

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
        task->num_blocks = 1;
        *result = block;
        return knd_OK;
    }

    curr_block = task->blocks;
    if ((curr_block->capacity - curr_block->buf_size) >= space_required) {
        //knd_log("  {capacity %zu}", curr_block->capacity - curr_block->buf_size);
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

int knd_memblock_read_file(struct kndMemBlock *block, const char *filename, size_t file_size,
                           bool separ_needed, const char **result)
{
    FILE *file_stream;
    size_t read_size;
    char *curr_pos;

    if (file_size >= block->free_space) return knd_LIMIT;

    file_stream = fopen(filename, "r");
    if (!file_stream) return knd_IO_FAIL;
    curr_pos = block->buf + block->buf_size;

    read_size = fread(curr_pos, 1, file_size, file_stream);
    if (!read_size) return knd_IO_FAIL;
    if (read_size != file_size) return knd_IO_FAIL;

    block->buf_size += file_size;
    block->free_space -= file_size;

    fclose(file_stream);

    if (separ_needed) {
        block->buf[block->buf_size] = '\0';
        block->buf_size++;
    }

    *result = curr_pos;
    return knd_OK;
}
