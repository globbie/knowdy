#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <strings.h>
#include <memory.h>

#include <stdarg.h>
#include <syslog.h>
#include <ctype.h>

/* numeric conversion by strtol */
#include <errno.h>
#include <limits.h>
#include <unistd.h>

#include "knd_config.h"
#include "knd_task.h"
#include "knd_utils.h"
#include "knd_output.h"

#define DEBUG_UTILS_LEVEL_1 0
#define DEBUG_UTILS_LEVEL_2 0
#define DEBUG_UTILS_LEVEL_3 0
#define DEBUG_UTILS_LEVEL_4 0
#define DEBUG_UTILS_LEVEL_TMP 1

/* base name to integer value mapping */
const int obj_id_base[256] = {
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

extern void knd_log(const char *fmt, ...)
{
  va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

size_t knd_generate_random_id(char *buf, size_t chunk_size, size_t num_chunks, char separ)
{
    size_t max_size = strlen(obj_id_seq) - 1;
    size_t buf_size = 0;

    for (size_t chunk_num = 0; chunk_num < num_chunks; chunk_num++) {
        if (chunk_num != 0) {
            buf[buf_size] = separ;
            buf_size++;
        }
        for (size_t i = 0; i < chunk_size; i++) {
            int key = rand() % (int)max_size;
            buf[buf_size] = obj_id_seq[key];
            buf_size++;
        }
    }
    return buf_size;
}

extern gsl_err_t knd_set_curr_state(void *obj,
                                    const char *val, size_t val_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndTask *task = obj;
    long numval;
    int err;

    if (!val_size) return make_gsl_err_external(knd_FAIL);
    if (val_size >= KND_NAME_SIZE) return make_gsl_err_external(knd_LIMIT);

    memcpy(buf, val, val_size);
    buf_size = val_size;
    buf[buf_size] = '\0';

    err = knd_parse_num(buf, &numval);
    if (err) return make_gsl_err_external(err);

    // TODO: check integer
    task->state_eq = (size_t)numval;

    return make_gsl_err(gsl_OK);
}

extern int knd_print_offset(struct kndOutput *out,
                            size_t num_spaces)
{
    char buf[KND_PATH_SIZE];
    memset(buf, ' ', num_spaces); 
    return out->write(out, buf, num_spaces);
}

/* big-endian order: Y1 (62 alphanum base) => 96 (decimal) */
extern void knd_calc_num_id(const char *id, size_t id_size, size_t *numval)
{
    const char *c = id;
    int num = 0;
    size_t aggr = 0;
    size_t base = 1;

    for (size_t i = 0; i < id_size; i++) {
        num = obj_id_base[(unsigned int)*c];
        if (num == -1) return;
        aggr = aggr + (num * base);
        base = base * KND_RADIX_BASE;
        c++;
    }
    *numval = aggr;
    //knd_log("%.*s => %zu", id_size, id, aggr);
}

extern void knd_build_conc_abbr(const char *name, size_t name_size, char *buf, size_t *buf_size)
{
    const char *c = name;
    size_t i = name_size;
    size_t abbr_size = 0;

    while (i) {
        if (isupper(*c)) {
            *(buf + abbr_size) = *c;
            abbr_size++;
            if (abbr_size >= KND_ID_SIZE) return;
        }
        c++;
        i--;
    }
    *buf_size = abbr_size;
    // knd_log("class: \"%.*s\"  abbr: \"%.*s\"", name_size, name, abbr_size, buf);
}

extern void
knd_num_to_str(size_t numval, char *buf, size_t *buf_size, size_t base)
{
    size_t curr_val = numval;
    size_t curr_size = 0;
    ldiv_t result;

    if (curr_val == 0) {
        *buf = '0';
        *buf_size = 1;
        return;
    }

    while (curr_val) {
        result = ldiv(curr_val, base);

        //knd_log("Q:%lu R:%lu CURR:%zu buf_size:%zu",
        //      result.quot, result.rem, curr_base, *buf_size);

        curr_val = result.quot;
        *buf++ = obj_id_seq[result.rem];
        curr_size++;
    }

    *buf_size = curr_size;
}

size_t knd_min_bytes(size_t val)
{
    if (val < 256) return 1;
    if (val < 256 * 256) return 2;
    if (val < 256 * 256 * 256) return 3;
    return 4;
}

unsigned char * knd_pack_u32(unsigned char *buf, size_t val)
{
    buf[0] = val >> 24;
    buf[1] = val >> 16;
    buf[2] = val >> 8;
    buf[3] = val;
    return buf + 4;
}

unsigned char * knd_pack_u24(unsigned char *buf, size_t val)
{
    buf[0] = val >> 16;
    buf[1] = val >> 8;
    buf[2] = val;
    return buf + 3;
}

unsigned char * knd_pack_u16(unsigned char *buf, size_t val)
{
    buf[0] = val >> 8;
    buf[1] = val;
    return buf + 2;
}

unsigned long knd_unpack_u32(const unsigned char *buf)
{
    unsigned long result = 0;
    result =  buf[3];
    result |= buf[2] << 8;
    result |= buf[1] << 16;
    result |= buf[0] << 24;
    return result;
}

unsigned long knd_unpack_u24(const unsigned char *buf)
{
    unsigned long result = 0;
    result =  buf[2];
    result |= buf[1] << 8;
    result |= buf[0] << 16;
    return result;
}

unsigned long knd_unpack_u16(const unsigned char *buf)
{
    unsigned long result = 0;
    result =  buf[1];
    result |= buf[0] << 8;
    return result;
}

void knd_pack_int(unsigned char *buf, size_t numval, size_t byte_size)
{
    switch (byte_size) {
    case 4:
        knd_pack_u32(buf, numval);
        break;
    case 3:
        knd_pack_u24(buf, numval);
        break;
    case 2:
        knd_pack_u16(buf, numval);
        break;
    default:
        *buf = numval;
        break;
    }
}

size_t knd_unpack_int(const unsigned char *buf, size_t byte_size)
{
    switch (byte_size) {
    case 4:
        return knd_unpack_u32(buf);
    case 3:
        return knd_unpack_u24(buf);
    case 2:
        return knd_unpack_u16(buf);
    default:
        return *buf;
    }
}

static int knd_mkdir(const char *path, mode_t mode)
{
    struct stat st;
    int err = knd_OK;

    if (stat(path, &st) != 0) {
        /* directory does not exist so far */
        if (mkdir(path, mode) != 0) {
            /* check errno EEXISTS - 
               some other thread might already created this dir? */
            if (errno == EEXIST) return knd_OK;
            err = knd_IO_FAIL;
        }
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
knd_mkpath(const char *path, size_t path_size, mode_t mode, bool has_filename)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size = 0;
    size_t tail_size = path_size;
    const char *p;
    char *b;
    int  err;

    if (path_size >= KND_TEMP_BUF_SIZE)
        return knd_LIMIT;
    if (!path_size) return knd_LIMIT;

    p = path;
    b = buf;

    while (tail_size) {
        switch (*p) {
        case '/':

            if (buf_size) {
                *b = '\0';
                if (DEBUG_UTILS_LEVEL_2)
                    knd_log("NB: .. mkdir: %s [%zu]", buf, buf_size);

                err = knd_mkdir(buf, mode);                                       RET_ERR();
            }

            *b = '/';
            buf_size++;
            break;
        default:
            *b = *p;
            buf_size++;
            break;
        }
        tail_size--;
        p++;
        b++;
    }

    /* in case no final dir separator is present at the end */
    if (buf_size && !has_filename) {
        buf[buf_size] = '\0';
        if (DEBUG_UTILS_LEVEL_2)
            knd_log("LAST DIR: \"%s\" [%zu]", buf, buf_size);
        err = knd_mkdir(buf, mode);                                               RET_ERR();
    }

    return knd_OK;
}

extern int knd_write_file(const char *filename, 
                          void *buf, size_t buf_size)
{
    int fd;

    fd = open(filename,  
              O_WRONLY | O_TRUNC | O_CREAT, 0644);
    if (fd < 0) return knd_IO_FAIL;

    ssize_t written = write(fd, buf, buf_size);
    close(fd);

    return written == -1 || (size_t)written != buf_size ? knd_IO_FAIL : knd_OK;
}

extern int 
knd_append_file(const char *filename, 
                const void *buf, size_t buf_size)
{
    int fd;

    /* write textual content */
    fd = open(filename,  
              O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        knd_log("-- append to file \"%s\" failed :(", filename);
        return knd_IO_FAIL;
    }

    ssize_t written = write(fd, buf, buf_size);
    close(fd);

    return written == -1 || (size_t)written != buf_size ? knd_IO_FAIL : knd_OK;
}

/*int knd_read_file_footer(const char *filename,
                         void *footer,
                         size_t footer_size,
                         struct kndTask *task)
{
    int fd, num_bytes;
    int err;

    fd = open(filename, O_RDONLY);
    if (fd == -1) {
        err = knd_IO_FAIL;
        KND_TASK_ERR("failed to open file");
    }

    lseek(fd, -(int)footer_size, SEEK_END);
    num_bytes = read(fd, footer, footer_size);

    close(fd);
    return knd_OK;
    }*/


int knd_make_id_path(char *buf,
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


extern void knd_remove_nonprintables(char *data)
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
}

int knd_read_UTF8_char(const char *rec,
                   size_t rec_size,
                   size_t *val,
                   size_t *len)
{
    size_t num_bytes = 0;
    long numval = 0;

    /* single byte ASCII-code */
    if ((unsigned char)*rec < 128) {
        if (DEBUG_UTILS_LEVEL_3)
            knd_log("    == ASCII code: %u\n",
                    (unsigned char)*rec);
        num_bytes++;

        *val = (size_t)*rec;
        *len = num_bytes;
        return knd_OK;
    }

    /* 2-byte indicator */
    if ((*rec & 0xE0) == 0xC0) {
        if (rec_size < 2) {
            if (DEBUG_UTILS_LEVEL_TMP)
                knd_log("    -- No payload byte left :(\n");
            return knd_LIMIT;
        }

        if ((rec[1] & 0xC0) != 0x80) {
            if (DEBUG_UTILS_LEVEL_4)
                knd_log("    -- Invalid UTF-8 payload byte: %2.2x\n",
                        rec[1]);
            return knd_FAIL;
        }

        numval = ((rec[0] & 0x1F) << 6) |
            (rec[1] & 0x3F);

        if (DEBUG_UTILS_LEVEL_3)
            knd_log("    == UTF-8 2-byte code: %lu\n",
                    (unsigned long)numval);

        *val = (size_t)numval;
        *len = 2;
        return knd_OK;
    }

    /* 3-byte indicator */
    if ((*rec & 0xF0) == 0xE0) {
        if (rec_size < 3) {
            if (DEBUG_UTILS_LEVEL_3)
                knd_log("    -- Not enough payload bytes left :(\n");
            return knd_LIMIT;
        }

        if ((rec[1] & 0xC0) != 0x80) {
            if (DEBUG_UTILS_LEVEL_3)
                knd_log("    -- Invalid UTF-8 payload byte: %2.2x\n",
                        rec[1]);
            return knd_FAIL;
        }

        if ((rec[2] & 0xC0) != 0x80) {
            if (DEBUG_UTILS_LEVEL_3)
                knd_log("   -- Invalid UTF-8 payload byte: %2.2x\n",
                        rec[2]);
            return knd_FAIL;
        }

        numval = ((rec[0] & 0x0F) << 12)  |
            ((rec[1] & 0x3F) << 6) |
            (rec[2] & 0x3F);

        if (DEBUG_UTILS_LEVEL_3)
            knd_log("    == UTF-8 3-byte code: %lu\n",
                    (unsigned long)numval);

        *val = (size_t)numval;
        *len = 3;

        return knd_OK;
    }

    if (DEBUG_UTILS_LEVEL_3)
        knd_log("    -- Invalid UTF-8 code: %2.2x\n",
                *rec);
    return knd_FAIL;
}

extern int knd_parse_num(const char *val,
                         long *result)
/*int *warning)*/
{
    long numval;
    char *invalid_num_char = NULL;
    int err = knd_OK;

    assert(val != NULL);
    errno = 0;

    numval = strtol(val, &invalid_num_char, KND_NUM_ENCODE_BASE);

    /* check for various numeric decoding errors */
    if ((errno == ERANGE && (numval == LONG_MAX || numval == LONG_MIN)) ||
            (errno != 0 && numval == 0))
    {
        knd_log("input str: \"%s\"", val);
        perror("strtol");
        err = knd_FAIL;
        goto final;
    }

    if (invalid_num_char == val) {
        fprintf(stderr, "  -- No digits were found in \"%s\"\n", val);
        err = knd_FAIL;
        goto final;
    }

    *result = numval;

final:

    return err;
}

static const char knd_base_64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void knd_base64_encode(char *encoded, const char *string, int len)
{
    char *p;
    p = encoded;
    int i;

    for (i = 0; i < len - 2; i += 3) {
        *p++ = knd_base_64[(string[i] >> 2) & 0x3F];
        *p++ = knd_base_64[((string[i] & 0x3) << 4) |
                           ((int) (string[i + 1] & 0xF0) >> 4)];
        *p++ = knd_base_64[((string[i + 1] & 0xF) << 2) |
                           ((int) (string[i + 2] & 0xC0) >> 6)];
        *p++ = knd_base_64[string[i + 2] & 0x3F];
    }

    if (i < len) {
        *p++ = knd_base_64[(string[i] >> 2) & 0x3F];
        if (i == (len - 1)) {
            *p++ = knd_base_64[((string[i] & 0x3) << 4)];
            *p++ = '=';
        }
        else {
            *p++ = knd_base_64[((string[i] & 0x3) << 4) |
                               ((int) (string[i + 1] & 0xF0) >> 4)];
            *p++ = knd_base_64[((string[i + 1] & 0xF) << 2)];
        }
        *p++ = '=';
    }
    *p++ = '\0';
}
