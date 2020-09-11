#include "knd_output.h"
#include "knd_mempool.h"
#include "knd_utils.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG_OUTPUT_LEVEL_0 0
#define DEBUG_OUTPUT_LEVEL_1 0
#define DEBUG_OUTPUT_LEVEL_2 0
#define DEBUG_OUTPUT_LEVEL_3 0
#define DEBUG_OUTPUT_LEVEL_TMP 1

void knd_output_del(struct kndOutput *self)
{
    if (self->local_alloc)
        free(self->buf);
    free(self);
}

static void
kndOutput_reset(struct kndOutput *self)
{
    self->buf_size = 0;
    self->buf[self->buf_size] = '\0';
}

static int
kndOutput_rtrim(struct kndOutput *self,
                size_t trim_size)
{
    if (trim_size > self->buf_size) return knd_LIMIT;

    self->buf_size -= trim_size;
    self->buf[self->buf_size] = '\0';

    return knd_OK;
}

static int
kndOutput_writec(struct kndOutput *self, char ch)
{
    if (self->buf_size >= self->capacity - 1)
        return knd_NOMEM;

    self->buf[self->buf_size] = ch;
    self->buf_size++;
    self->buf[self->buf_size] = '\0';
    return knd_OK;
}

static int
kndOutput_writef(struct kndOutput *self,
                 const char *format,
                 ...)
{
    int len;

    va_list arg;
    va_start(arg, format);
    len = vsnprintf(self->buf + self->buf_size, self->capacity - self->buf_size, format, arg);
    va_end(arg);

    if (len < 0) return knd_IO_FAIL;
    if ((size_t)len >= self->capacity - self->buf_size)
        return knd_NOMEM;

    self->buf_size += len;
    return knd_OK;
}

static int kndOutput_write(struct kndOutput *self, const char *buf, size_t buf_size)
{
    if (buf_size > self->capacity - self->buf_size - 1)
        return knd_NOMEM;

    memcpy(self->buf + self->buf_size, buf, buf_size);
    self->buf_size += buf_size;
    self->buf[self->buf_size] = '\0';
    return knd_OK;
}

static int
kndOutput_write_escaped(struct kndOutput *self,
                        const char *buf,
                        size_t buf_size)
{
    size_t free_space = self->capacity - self->buf_size;
    size_t chunk_size = 0;
    char *c = self->buf + self->buf_size;
    const char *b;

    for (size_t i = 0; i < buf_size; i++) {
        b = buf + i;
        if (chunk_size >= free_space)
            return knd_NOMEM;

        switch (*b) {
        case '{':
        case '}':
        case '"':
        case '\'':
            break;
        case '\n':
            *c = '\\';
            c++;
            *c = 'n';
            c++;
            chunk_size += 2;
            break;
        default:
            *c = *b;
            c++;
            chunk_size++;
        }
    }
    
    self->buf_size += chunk_size;
    return knd_OK;
}

static int
kndOutput_write_state_path(struct kndOutput *self,
                           const char *state,
                           size_t state_size)
{
    size_t rec_size = state_size * 2 + 1;
    if (rec_size > self->capacity - self->buf_size)
        return knd_NOMEM;

    char *rec = self->buf + self->buf_size;

    for (const char *state_end = state + state_size; state < state_end; state++) {
        *rec++ =  '/';
        *rec++ = *state;
    }
    *rec = '\0';

    self->buf_size += rec_size;

    return knd_OK;
}

static int
kndOutput_write_file_content(struct kndOutput *self,
                             const char *file_name/*,
                             bool allow_resize*/)
{
    int err;
    FILE *file_stream;
    long file_size;
    size_t read_size;

    if (DEBUG_OUTPUT_LEVEL_2)
        knd_log(".. IO [%p] reading the \"%s\" file..", self, file_name);

    if (self->buf_size != 0) return knd_FAIL;

    if (DEBUG_OUTPUT_LEVEL_3)
        knd_log(" .. opening file \"%s\"..\n", file_name);

    file_stream = fopen(file_name, "r");
    if (file_stream == NULL) {
        knd_log("-- error opening FILE \"%s\"",
                file_name);
        return knd_IO_FAIL;
    }

    if (fseek(file_stream, 0L, SEEK_END) != 0) { err = knd_IO_FAIL; goto final; }

    file_size = ftell(file_stream);
    if (file_size == -1) { err = knd_IO_FAIL; goto final; }

    if (fseek(file_stream, 0L, SEEK_SET) != 0) { err = knd_IO_FAIL; goto final; }

    if ((size_t)file_size > self->capacity - self->buf_size - 1) { err = knd_NOMEM; goto final; }

    if (DEBUG_OUTPUT_LEVEL_3)
        knd_log("  .. reading FILE \"%s\" [%ld] ...\n",
                file_name, file_size);

    read_size = fread(self->buf + self->buf_size, 1, file_size, file_stream);
    self->buf_size += read_size;
    self->buf[self->buf_size] = '\0';

    if (DEBUG_OUTPUT_LEVEL_3)
        knd_log("   ++ FILE \"%s\" read OK [size: %zu]\n",
                file_name, read_size);

    err = knd_OK;

final:

    fclose(file_stream);

    return err;
}


static int
kndOutput_init(struct kndOutput *self,
               size_t capacity)
{
    self->buf_size = 0;
    self->capacity = capacity;

    self->del = knd_output_del;
    self->reset = kndOutput_reset;
    self->rtrim = kndOutput_rtrim;
    self->writec = kndOutput_writec;
    self->writef = kndOutput_writef;
    self->write = kndOutput_write;
    self->write_escaped = kndOutput_write_escaped;
    self->write_state_path = kndOutput_write_state_path;
    self->write_file_content = kndOutput_write_file_content;

    return knd_OK;
}

int knd_name_buf_new(struct kndMemPool *mempool, struct kndNameBuf **result)
{
    void *page;
    int err;
    assert(mempool->page_size >= sizeof(struct kndNameBuf));
    err = knd_mempool_page(mempool, KND_MEMPAGE_BASE, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndNameBuf));
    *result = page;
    return knd_OK;
}

int knd_output_new(struct kndOutput **output, char *buf, size_t capacity)
{
    int err;
    struct kndOutput *self;

    self = malloc(sizeof(struct kndOutput));
    if (!self) return knd_NOMEM;

    if (!buf) {
        self->buf = malloc((capacity + 1) * sizeof(char));
        if (self->buf == NULL) return knd_NOMEM;
    } else {
        self->buf = buf;
    }

    err = kndOutput_init(self, capacity);
    if (err != 0) {
        free(self);
        return err;
    }

    self->threshold = capacity * KND_OUTPUT_THRESHOLD_RATIO;

    *output = self;

    return knd_OK;
}
