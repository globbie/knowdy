#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <fcntl.h>

#include <unistd.h>

#include "knd_output.h"
#include "knd_utils.h"

#define DEBUG_OUTPUT_LEVEL_0 0
#define DEBUG_OUTPUT_LEVEL_1 0
#define DEBUG_OUTPUT_LEVEL_2 0
#define DEBUG_OUTPUT_LEVEL_3 0
#define DEBUG_OUTPUT_LEVEL_TMP 1


static int 
kndOutput_del(struct kndOutput *self)
{

    free(self);

    return knd_OK;
}


static void
kndOutput_reset(struct kndOutput *self)
{
    self->buf[0] = '\0';
    self->filename[0] = '\0';
    self->filename_size = 0;

    if (self->file) {
        free(self->file);
        self->file = NULL;
    }
    
    self->file_size = 0;
    
    self->curr_buf = self->buf;
    self->free_space = self->max_size;
    self->buf_size = 0;
}

static int
kndOutput_write(struct kndOutput *self,
                const char    *buf,
                size_t        buf_size)
{
    char **output_buf;
    size_t *output_size;
    size_t *free_space;

    output_buf = &self->curr_buf;
    output_size = &self->buf_size;
    free_space = &self->free_space;
  
    if (buf_size > *free_space) return knd_NOMEM;

    memcpy(*output_buf, buf, buf_size);

    *output_buf += buf_size;
    *output_buf[0] = '\0';
    *output_size += buf_size;
    *free_space -= buf_size;

    return knd_OK;
}


static int
kndOutput_read_file(struct kndOutput *self,
                    const char    *filename,
                    size_t filename_size)
{
    struct stat file_info;
    int fd;
    int err;

    if (DEBUG_OUTPUT_LEVEL_2)
        knd_log(".. IO [%p] reading the \"%s\" file..", self, filename);

    if (!filename_size) return knd_FAIL;

    if (self->filename_size) {
        if (!strncmp(self->filename, filename, filename_size)) {
            if (DEBUG_OUTPUT_LEVEL_3)
                knd_log("   ++ file \"%s\" already in memory: [%lu]\n",
                        filename, (unsigned long)self->file_size);
            
            return knd_OK;
        }
    }
    
    memcpy(self->filename, filename, filename_size);
    self->filename[filename_size] = '\0';
    self->filename_size = filename_size;
    
    if (DEBUG_OUTPUT_LEVEL_3)
        knd_log(" .. opening file \"%s\"..\n", filename);

    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        knd_log("-- error reading FILE \"%s\": %d",
                filename, fd);
        return knd_IO_FAIL;
    }

    fstat(fd, &file_info);

    self->file_size = file_info.st_size;  
    
    if (DEBUG_OUTPUT_LEVEL_3)
        knd_log("  .. reading FILE \"%s\" [%lu] ...\n",
                filename, (unsigned long)self->file_size);

    if (self->file) free(self->file);
    
    self->file = malloc(sizeof(char) * self->file_size);
    if (!self->file) {
        err = knd_NOMEM;
        goto final;
    }

    err = read(fd, self->file, self->file_size);
    if (err == -1) {
        err = knd_IO_FAIL;
        goto final;
    }

    /*self->file[self->file_size] = '\0';*/

    if (DEBUG_OUTPUT_LEVEL_3)
        knd_log("   ++ FILE \"%s\" read OK [size: %lu]\n",
                filename, (unsigned long)self->file_size);
    
    err = knd_OK;
    
 final:

    close(fd);

    return err;
}


static int
kndOutput_rtrim(struct kndOutput *self,
               size_t        trim_size)
{
    if (trim_size > self->buf_size) return knd_LIMIT;
    
    self->curr_buf -= trim_size;
    self->buf_size -= trim_size;
    self->free_space += trim_size;
    
    return knd_OK;
}

extern int 
kndOutput_init(struct kndOutput *self)
{
    self->del = kndOutput_del;
    self->reset = kndOutput_reset;
    self->rtrim = kndOutput_rtrim;
    self->write = kndOutput_write;
    self->read_file = kndOutput_read_file;
    
    return knd_OK;
}

extern int 
kndOutput_new(struct kndOutput **output,
              size_t capacity)
{
    struct kndOutput *self;

    self = malloc(sizeof(struct kndOutput));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndOutput));

    /* output buffer */
    self->buf = malloc(capacity * sizeof(char));
    if (!self->buf)
	return knd_NOMEM;

    self->curr_buf = self->buf;
    self->max_size = capacity * sizeof(char);
    self->free_space = self->max_size;

    kndOutput_init(self);

    *output = self;

    return knd_OK;
}
