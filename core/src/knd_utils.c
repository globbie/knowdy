#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <strings.h>
#include <memory.h>

#include <stdarg.h>
#include <syslog.h>

/* numeric conversion by strtol */
#include <errno.h>
#include <limits.h>
#include <unistd.h>

#include "knd_config.h"
#include "knd_utils.h"

#define DEBUG_UTILS_LEVEL_1 0
#define DEBUG_UTILS_LEVEL_2 0
#define DEBUG_UTILS_LEVEL_3 0
#define DEBUG_UTILS_LEVEL_4 0
#define DEBUG_UTILS_LEVEL_TMP 1

/* base name to integer value mapping */
int obj_id_base[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  0, 1, 2, 3, 4, 5, 6, 7, 8, 9,-1,-1,-1,-1,-1,-1,
    -1,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24, 25,26,27,28,29,30,31,32,33,34,35,-1,-1,-1,-1,-1,
    -1,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50, 51,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};

const char *obj_id_seq = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

extern void 
knd_log(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    vprintf(fmt, args);
    printf("\n");
    
    va_end(args);
}

extern int 
knd_compare(const char *a, const char *b)
{
    for (size_t i = 0; i < KND_ID_SIZE; i++) {
        if (a[i] > b[i]) return knd_MORE;
        else if (a[i] < b[i]) return knd_LESS;
    }
    
    return knd_EQUALS;
}

extern int knd_next_state(char *s)
{
    char *c;

    for (int i = KND_STATE_SIZE - 1; i > -1; i--) {
        c = &s[i];
        switch (*c) {
        case '9':
            *c = 'A';
            return knd_OK;
        case 'Z':
            *c = 'a';
            return knd_OK;
        case 'z':
            /* last position overflow */
            if (i == 0) return knd_LIMIT; 
            *c = '0';
            continue;
        default:
            (*c)++;
            return knd_OK;
        }
    }

    return knd_OK;
}

extern int 
knd_state_is_valid(const char *id, size_t id_size)
{
    if (id_size > KND_STATE_SIZE) return knd_FAIL;
    
    for (size_t i = 0; i < KND_STATE_SIZE; i++) {
        if (id[i] >= '0' && id[i] <= '9') continue;
        if (id[i] >= 'A' && id[i] <= 'Z') continue;
        if (id[i] >= 'a' && id[i] <= 'z') continue;
        return knd_FAIL;
    }

    return knd_OK;
}

extern int 
knd_state_compare(const char *a, const char *b)
{
    for (size_t i = 0; i < KND_STATE_SIZE; i++) {
        if (a[i] > b[i]) return knd_MORE;
        else if (a[i] < b[i]) return knd_LESS;
    }
    return knd_EQUALS;
}


extern int 
knd_is_valid_id(const char *id, size_t id_size)
{
    if (id_size > KND_ID_SIZE) return knd_FAIL;
    
    for (size_t i = 0; i < KND_ID_SIZE; i++) {
        if (id[i] >= '0' && id[i] <= '9') continue;
        if (id[i] >= 'A' && id[i] <= 'Z') continue;
        if (id[i] >= 'a' && id[i] <= 'z') continue;
        return knd_FAIL;
    }

    return knd_OK;
}

extern const char *
knd_max_id(const char *a, const char *b)
{
    return (knd_compare(a, b) == knd_MORE) ? a : b;
}

extern const char *
knd_min_id(const char *a, const char *b)
{
    return knd_compare(a, b) == knd_LESS ? a : b;
}



extern unsigned char *
knd_pack_int(unsigned char *buf,
             unsigned int val)
{
    buf[0] = val >> 24;
    buf[1] = val >> 16;
    buf[2] = val >> 8;
    buf[3] = val;

    return buf + 4;
}


extern unsigned long
knd_unpack_int(const unsigned char *buf)
{
    unsigned long result = 0;

    result =  buf[3];
    result |= buf[2] << 8;
    result |= buf[1] << 16;
    result |= buf[0] << 24;

    return result;
}



static int 
knd_mkdir(const char *path, mode_t mode)
{
    struct stat st;
    int err = knd_OK;

    if (stat(path, &st) != 0) {

        /* directory does not exist */
        if (mkdir(path, mode) != 0)
            err = knd_FAIL;
    }
    else if (!S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        err = knd_FAIL;
    }

    return err;
}


/**
 * knd_mkpath - ensure all directories in path exist
 */
extern int 
knd_mkpath(const char *path, mode_t mode, bool has_filename)
{
    char *p;
    char *sep = NULL;
    int  err;
    
    char *path_buf = strdup(path);
    if (!path_buf)
        return knd_NOMEM;
    
    err = knd_OK;
    p = path_buf;

    while (1) {
        sep = strchr(p, '/');
        if (!sep) break;

        /*knd_log("CURR PATH: %s\n", path_buf);
          knd_log("REST: %s\n", p); */
        
        *sep = '\0';
        if (!(*p)) goto next_sep;
        
        err = knd_mkdir(path_buf, mode);
        if (err)
            goto final;
        
    next_sep:
        *sep = '/';
        p = sep + 1;
    }

    /*knd_log("LAST DIR: \"%s\"\n", p); */
    
    /* in case no final dir separator is present at the end */
    if (*p && !has_filename) {
        err = knd_mkdir(path, mode);
        if (err)
            goto final;
    }

 final:
    free(path_buf);

    return err;
}

extern int 
knd_write_file(const char *path,
               const char *filename, 
               void *buf, size_t buf_size)
{
    char name_buf[KND_TEMP_BUF_SIZE];
    int fd;

    sprintf(name_buf, "%s/%s", path, filename);

    /* write textual content */
    fd = open((const char*)name_buf,  
	      O_WRONLY | O_TRUNC | O_CREAT, 0644);
    if (fd < 0) return knd_IO_FAIL;

    write(fd, buf, buf_size);
    close(fd);

    return knd_OK;
}

extern int 
knd_append_file(const char *filename, 
                const void *buf, size_t buf_size)
{
    int fd;

    /* write textual content */
    fd = open(filename,  
	      O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return knd_IO_FAIL;

    write(fd, buf, buf_size);
    close(fd);

    return knd_OK;
}


extern int 
knd_make_id_path(char *buf,
                 const char *path,
                 const char *id, 
                 const char *filename)
{
    char *curr_buf = buf;
    size_t path_size = 0;
    size_t i;

    if (path) {
        path_size = sprintf(curr_buf, "%s", path);
        curr_buf += path_size;
    }
    
    /* treat each digit in the id as a folder */
    for (i = 0; i < KND_ID_SIZE; i++) {
	*curr_buf =  '/';
	curr_buf++;
	*curr_buf = id[i];
	curr_buf++;
    }
    
    *curr_buf = '\0';
        
    if (filename)
	sprintf(curr_buf, "/%s", filename);

    return knd_OK;
}


extern int 
knd_remove_nonprintables(char *data)
{
    unsigned char *c;
    c = (unsigned char*)data;

    while (*c) {
	if (*c < 32) {
	    *c = ' ';
	}
	if (*c == '\"') *c = ' ';
	if (*c == '\'') *c = ' ';
	if (*c == '&') *c = ' ';
	if (*c == '\\') *c = ' '; 
	c++;
    }

    return knd_OK;
}

