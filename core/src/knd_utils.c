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

// #include <openssl/sha.h>

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

/* big-endian order: Y1 (62 alphanum base) => 96 (decimal) */
extern void
knd_calc_num_id(const char *id, size_t id_size, size_t *numval)
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
    if (fd < 0) {
        knd_log("-- append to file \"%s\" failed :(", filename);
        return knd_IO_FAIL;
    }

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

/*extern int knd_graphic_rounded_rect(struct kndOutput *out,
                                    size_t x, size_t y,
                                    size_t w, size_t h,
                                    size_t r,
                                    bool tl, bool tr, bool bl, bool br)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    int err;

    buf_size = sprintf(buf, "M%zu,%zu", (x + r), y);

        retval += "h" + (w - 2*r);

        if (tr) {
            retval += "a" + r + "," + r + " 0 0 1 " + r + "," + r;
        } else {
            retval += "h" + r; retval += "v" + r;
        }

        retval += "v" + (h - 2*r);

        if (br) {
            retval += "a" + r + "," + r + " 0 0 1 " + -r + "," + r;
        } else { retval += "v" + r; retval += "h" + -r; }
        retval += "h" + (2*r - w);
        if (bl) { retval += "a" + r + "," + r + " 0 0 1 " + -r + "," + -r; }
        else { retval += "h" + -r; retval += "v" + -r; }
        retval += "v" + (2*r - h);
        if (tl) { retval += "a" + r + "," + r + " 0 0 1 " + r + "," + -r; }
        else { retval += "v" + -r; retval += "h" + r; }
        retval += "z";
        return retval;
}
*/


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

int knd_parse_num(const char *val,
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




/**
 * read out the tag and the implied field value (name)
 */
int knd_parse_incipit(const char *rec,
                      size_t rec_size,
                      char *result_tag_name,
                      size_t *result_tag_name_size,
                      char *result_name,
                      size_t *result_name_size)
{
    const char *c, *b, *e;
    bool in_tag = false;
    bool in_name = false;
    bool got_name = false;
    size_t tag_size;
    size_t name_size;
    
    c = rec;
    b = rec;
    e = rec;

    for (size_t i = 0; i < rec_size; i++) {
        c = rec + i;
        switch (*c) {
        case ' ':
        case '\n':
        case '\r':
        case '\t':
	    if (in_name) break;
	    if (in_tag) {
		tag_size = c - b;
		if (!tag_size)
		    return knd_FAIL;
		if (tag_size >= *result_tag_name_size)
		    return knd_FAIL;

		memcpy(result_tag_name, b, tag_size);
		*result_tag_name_size = tag_size;
                b = c + 1;
                e = b;
                in_name = true;
                break;
            }

            break;
        case '[':
        case '{':
            if (!in_tag) {
                in_tag = true;
                b = c + 1;
                e = b;
                break;
	    }

            got_name = true;
            e = c;
            break;
        default:
            e = c;
            break;
        }
        if (got_name) break;
    }

    name_size = e - b;
    if (!name_size)
	return knd_FAIL;
    if (name_size >= *result_name_size)
	return knd_FAIL;

    memcpy(result_name, b, name_size);
    *result_name_size = name_size;

    return knd_OK;
}

int knd_parse_dir_size(const char *rec,
                       size_t rec_size,
                       const char **val,
                       size_t *val_size,
                       size_t *total_trailer_size)
{
    bool in_field = false;
    bool got_separ = false;
    bool got_tag = false;
    bool got_size = false;
    size_t chunk_size = 0;
    const char *c, *b;
    int i = 0;

    b = rec;
    for (i = rec_size - 1; i >= 0; i--) { 
        c = rec + i;
        switch (*c) {
        case '\n':
        case '\r':
            break;
        case '}':
            if (in_field) return knd_FAIL;
            in_field = true;
            break;
        case '{':
            if (!in_field) return knd_FAIL;
            if (got_tag) got_size = true;
            break;
        case ' ':
            got_separ = true;
            break;
        case 'L':
            got_tag = true;
            break;
        default:
            if (!in_field) return knd_FAIL;
            if (got_tag) return knd_FAIL;
            if (!isalnum(*c))  return knd_FAIL;
            b = c;
            chunk_size++;
            break;
        }
        if (got_size) {
            *val = b;
            *val_size = chunk_size;
            *total_trailer_size = rec_size - i;
             return knd_OK;
        }
    }

    return knd_FAIL;
}



static const char knd_base_64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void knd_base64_encode(char *encoded, const char *string, int len)
{
    char *p;
    p = encoded;
    size_t i;

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


//int validate_secret_SHA512(const char *token,
//                           size_t token_size,
//                           const char *correct_hash,
//                           size_t correct_hash_size)
//{
//    char hash[SHA512_DIGEST_LENGTH];
//    char result[SHA512_DIGEST_LENGTH];
//
//    SHA512(token, token_size, hash); //the first iteration computation hash
//
//    // 4999 iterations
//    for (int i = 1; i < 5000; i++){
//        SHA512(hash, sizeof(hash) + token_size - 1, hash);
//    }
//
//    knd_base64_encode(result, hash,  sizeof(hash)); // the hash computation in base64
//
//    knd_log("RESULT:%.*s", SHA512_DIGEST_LENGTH, result);
//
//    return knd_OK;
//}
