#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "knd_config.h"
#include "knd_task.h"

extern int 
knd_get_trailer(const char  *rec,
                size_t rec_size,
                char  *name,
                size_t *name_size,
                size_t *num_items,
                char   *dir_rec,
                size_t *dir_rec_size);


extern int 
knd_read_UTF8_char(const char *rec,
                   size_t rec_size,
                   size_t *val,
                   size_t *len);

extern int 
knd_parse_matching_braces(const char *rec,
                          size_t *chunk_size);

extern int 
knd_remove_nonprintables(char *data);

extern int
knd_parse_num(const char *val,
	      long *result);
extern int
knd_read_name(char *output,
              size_t *output_size,
              const char *rec,
              size_t rec_size);
extern int
knd_parse_IPV4(char *ip, unsigned long *ip_val);

extern int
knd_parse_task(const char *rec,
               size_t *total_size,
               struct kndTaskSpec *specs,
               size_t num_specs);

extern int
knd_get_schema_name(const char *rec,
                    char *buf,
                    size_t *buf_size,
                    size_t *total_size);

