#ifndef KND_OUTPUT_H
#define KND_OUTPUT_H

#include "knd_config.h"

struct kndOutput
{
    char name[KND_NAME_SIZE];

    char filename[KND_TEMP_BUF_SIZE];
    size_t filename_size;
    
    char *buf;
    char *curr_buf;
    size_t buf_size;
    size_t max_size;
    size_t free_space;
    size_t threshold_size;

    char *file;
    size_t file_size;

    /**********  interface methods  **********/
    void (*del)(struct kndOutput *self);

    void (*reset)(struct kndOutput *self);

    int (*rtrim)(struct kndOutput *self,
                 size_t        trim_size);

    int (*write)(struct kndOutput *self,
                 const char    *buf,
                 size_t        buf_size);

    int (*write_state_path)(struct kndOutput *self,
                            const char    *state);

    int (*read_file)(struct kndOutput *self,
                     const char *filename,
                     size_t filename_size);
    
};

extern int kndOutput_new(struct kndOutput **self,
                         size_t capacity);
#endif
