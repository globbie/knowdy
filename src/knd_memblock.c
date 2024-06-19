#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>

#include "knd_config.h"
#include "knd_memblock.h"

#define DEBUG_MEMBLOCK_LEVEL_0 0
#define DEBUG_MEMBLOCK_LEVEL_TMP 1

int knd_memblock_new(struct kndMemBlock **result, size_t numid)
{
    struct kndMemBlock *block = calloc(1, sizeof(struct kndMemBlock));
    if (!block) return knd_NOMEM;
    block->numid = numid;
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

int knd_memblock_read_file(struct kndMemBlock *block, const char *filename, size_t file_size)
{
    FILE *file_stream;
    size_t read_size;
    size_t num_extra_bytes = 2; // closing brace + null term
    char *b = malloc(file_size + num_extra_bytes);
    if (!b) return knd_NOMEM;

    file_stream = fopen(filename, "r");
    if (!file_stream) return knd_IO_FAIL;
    read_size = fread(b, 1, file_size, file_stream);
    if (!read_size) return knd_IO_FAIL;

    b[file_size] = '}';
    b[file_size + 1] = '\0';

    block->buf = b;
    block->buf_size = file_size + 1;

    return knd_OK;
}
