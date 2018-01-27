#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "knd_config.h"

extern int
knd_read_UTF8_char(const char *rec,
                   size_t rec_size,
                   size_t *val,
                   size_t *len);

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
knd_get_schema_name(const char *rec,
                    char *buf,
                    size_t *buf_size,
                    size_t *total_size);

