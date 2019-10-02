#pragma once

#include "knd_config.h"

#include <stddef.h>

#define KND_OUTPUT_THRESHOLD_RATIO 0.8

struct kndOutput
{
    char *buf;
    size_t buf_size;
    size_t capacity;
    size_t threshold;
    bool local_alloc;

    /**********  interface methods  **********/
    void (*del)(struct kndOutput *self);

    void (*reset)(struct kndOutput *self);

    int (*rtrim)(struct kndOutput *self,
                 size_t trim_size);

    int (*write)(struct kndOutput *self,
                 const char *buf,
                 size_t buf_size);
    int (*writec)(struct kndOutput *self,
                  char ch);
    int (*writef)(struct kndOutput *self,
                  const char *format,
                  ...);

    int (*write_escaped)(struct kndOutput *self,
                         const char *buf,
                         size_t buf_size);

    int (*write_state_path)(struct kndOutput *self,
                            const char *state,
                            size_t state_size);

    int (*write_file_content)(struct kndOutput *self,
                              const char *filename);
};

int knd_output_new(struct kndOutput **self,
                   char *buf,
                   size_t capacity);
int knd_output_del(struct kndOutput *self);
