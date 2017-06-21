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
#include "knd_task.h"
#include "knd_utils.h"

#define DEBUG_PARSER_LEVEL_1 0
#define DEBUG_PARSER_LEVEL_2 0
#define DEBUG_PARSER_LEVEL_3 0
#define DEBUG_PARSER_LEVEL_4 0
#define DEBUG_PARSER_LEVEL_TMP 1


/* copy a sequence of non-whitespace chars */
extern int
knd_read_name(char *output,
              size_t *output_size,
              const char *rec,
              size_t rec_size)
{
    const char *c;
    size_t curr_size = 0;
    size_t i = 0;

    c = rec;
    
    while (*c) {
        switch (*c) {
        case ' ':
            break;
        case '\n':
            break;
        case '\r':
            break;
        case '\t':
            break;
        default:
            *output = *c;
            output++;
            curr_size++;
            break;
        }

        if (i >= rec_size) break;

        i++;
        c++;
    }
   
    *output = '\0';
    *output_size = curr_size;
    
    return knd_OK;
}



static inline int
check_name_limits(const char *b, const char *e, size_t *buf_size)
{
    *buf_size = e - b;
    if (!(*buf_size)) return knd_LIMIT;
    if ((*buf_size) >= KND_NAME_SIZE) {
        knd_log("-- field tag too large: %lu bytes",
                (unsigned long)buf_size);
        return knd_LIMIT;
    }
    return knd_OK;
}


extern int
knd_read_UTF8_char(const char *rec,
                   size_t rec_size,
                   size_t *val,
                   size_t *len)
{
    size_t num_bytes = 0;
    long numval = 0;
    
    /* single byte ASCII-code */
    if ((unsigned char)*rec < 128) {
        if (DEBUG_PARSER_LEVEL_3)
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
            if (DEBUG_PARSER_LEVEL_TMP) 
                knd_log("    -- No payload byte left :(\n");
            return knd_LIMIT;
        }

        if ((rec[1] & 0xC0) != 0x80) {
            if (DEBUG_PARSER_LEVEL_4) 
                knd_log("    -- Invalid UTF-8 payload byte: %2.2x\n", 
                        rec[1]);
            return knd_FAIL;
        }
	    
        numval = ((rec[0] & 0x1F) << 6) | 
            (rec[1] & 0x3F);

        if (DEBUG_PARSER_LEVEL_3)
            knd_log("    == UTF-8 2-byte code: %lu\n",
                    (unsigned long)numval);

        *val = (size_t)numval;
        *len = 2;
        return knd_OK;
    }

    /* 3-byte indicator */
    if ((*rec & 0xF0) == 0xE0) {
        if (rec_size < 3) {
            if (DEBUG_PARSER_LEVEL_3) 
                knd_log("    -- Not enough payload bytes left :(\n");
            return knd_LIMIT;
        }
            
        if ((rec[1] & 0xC0) != 0x80) {
            if (DEBUG_PARSER_LEVEL_3) 
                knd_log("    -- Invalid UTF-8 payload byte: %2.2x\n", 
                        rec[1]);
            return knd_FAIL;
        }
            
        if ((rec[2] & 0xC0) != 0x80) {
            if (DEBUG_PARSER_LEVEL_3) 
                knd_log("   -- Invalid UTF-8 payload byte: %2.2x\n", 
                        rec[2]);
            return knd_FAIL;
        }

        numval = ((rec[0] & 0x0F) << 12)  | 
            ((rec[1] & 0x3F) << 6) | 
            (rec[2] & 0x3F);

        if (DEBUG_PARSER_LEVEL_3) 
            knd_log("    == UTF-8 3-byte code: %lu\n", 
                    (unsigned long)numval);

        *val = (size_t)numval;
        *len = 3;

        return knd_OK;
    }

    if (DEBUG_PARSER_LEVEL_3) 
        knd_log("    -- Invalid UTF-8 code: %2.2x\n",
                *rec);
    return knd_FAIL;
}


extern int
knd_get_schema_name(const char *rec,
                    char *buf,
                    size_t *buf_size,
                    size_t *total_size)
{
    const char *b, *c, *e;
    size_t chunk_size = 0;
    size_t max_size = *buf_size;
    
    if (strncmp(rec, "::", strlen("::"))) return knd_FAIL;

    c = rec + strlen("::");;
    b = c;
    e = c;

    while (*c) {
        switch (*c) {
        case ' ':
        case '\n':
        case '\r':
        case '\t':
            break;
        case '{':
            chunk_size = e - b;
            if (!chunk_size) return knd_FAIL;
            if (chunk_size >= max_size) return knd_LIMIT;
            memcpy(buf, b, chunk_size);
            *buf_size = chunk_size;
            *total_size = c - rec;
            return knd_OK;
        default:
            e = c + 1;
            break;
        }
        c++;
    }

    return knd_FAIL;
}


extern int 
knd_get_trailer(const char   *rec,
                size_t  rec_size,
                char   *trailer_name,
                size_t *trailer_name_size,
                size_t *num_items,
                char   *dir_rec,
                size_t *dir_rec_size)
{
    char buf[KND_LARGE_BUF_SIZE];
    size_t buf_size = 0;

    char num_buf[KND_SMALL_BUF_SIZE];

    char *c;
    char *b;
    const char *curr;
    char *val;
    long numval = 0;

    size_t curr_size;
    size_t numfield_size = 0;
    bool in_facet = false;
    bool in_refset = false;
    int err;

    buf_size = KND_NUMFIELD_MAX_SIZE;
    if (rec_size < KND_NUMFIELD_MAX_SIZE)
        buf_size = rec_size;

    /* get size of trailer */
    curr = rec + rec_size - buf_size;
    memcpy(buf, curr, buf_size);
    buf[buf_size] = '\0';

    /*knd_log("TRAILER BUF: \"%s\"\n", buf);*/
    
    /* find last closing delimiter */
    for (size_t i = buf_size - 1; i > 0; i--) {
        c = buf + i;
        if (!strncmp(c, GSL_CLOSE_DELIM, GSL_CLOSE_DELIM_SIZE)) {
            in_refset = true;
            goto got_close_delim;
        }
        if (!strncmp(c, GSL_CLOSE_FACET_DELIM, GSL_CLOSE_FACET_DELIM_SIZE)) {
            in_facet = true;
            goto got_close_delim;
        }
    }

    knd_log("   -- invalid IDX trailer.. no closing delimiter found.. :(\n");

    return knd_FAIL;

 got_close_delim:
    numfield_size = buf_size - (c - buf) - 1;

    /*knd_log("   ++ Closing delim found, numfield size: %lu\n",
      (unsigned long)numfield_size); */

    if (!numfield_size) {
        *dir_rec_size = 0;
        return knd_OK;
    }
    
    memcpy(num_buf, (const char*)c + 1, numfield_size);
    num_buf[numfield_size] = '\0';
            
    err = knd_parse_num((const char*)num_buf, &numval);
    if (err) return err;

    /* check limits */
    if (numval < 1 || numval >= KND_LARGE_BUF_SIZE)
        return knd_FAIL;

    /* extract trailer */
    buf_size = (size_t)numval;
    
    curr = rec + rec_size - numval - numfield_size;
    memcpy(buf, curr, buf_size);

    /* delete the closing delim */
    buf[buf_size - 1] = '\0';

    /*knd_log("   IDX TRAILER: %s [%lu]\n",
      buf, (unsigned long)buf_size); */

    if (in_facet) {
        if (strncmp(buf, GSL_OPEN_FACET_DELIM, GSL_OPEN_FACET_DELIM_SIZE)) {
            knd_log("   -- invalid IDX trailer.. Facet doesn't start with an opening delim..\n");
            return knd_FAIL;
        }
        c = buf + GSL_OPEN_FACET_DELIM_SIZE;
    }

    if (in_refset) {
        if (strncmp(buf, GSL_OPEN_DELIM, GSL_OPEN_DELIM_SIZE)) {
            knd_log("   -- invalid IDX trailer.. Refset doesn't start with an opening delim..\n");
            return knd_FAIL;
        }
        c = buf + GSL_OPEN_DELIM_SIZE;
    }
    
    curr_size = buf - c;
    if (!curr_size) return knd_FAIL;
    
    val = strstr(c, GSL_TERM_SEPAR);
    if (!val) return knd_FAIL;

    curr_size = val - c;
    *val = '\0';
    
    /* total num items? */
    b = strstr(c, GSL_TOTAL);
    if (b) {
        numfield_size = b - c;
        *b = '\0';
        b++;
        
        err = knd_parse_num((const char*)b, &numval);
        if (err) return err;

        if (DEBUG_PARSER_LEVEL_3)
            knd_log("  TOTAL items: %lu\n", (unsigned long)numval);
        
        *num_items = (size_t)numval;
    }

    /* TODO: check limits */
    
    strncpy(trailer_name, c, curr_size);
    *trailer_name_size = curr_size;

    if (!(buf_size - curr_size -  GSL_TERM_SEPAR_SIZE)) return knd_FAIL;
    c += (curr_size + GSL_TERM_SEPAR_SIZE);
    
    *dir_rec_size = buf_size - curr_size -  GSL_TERM_SEPAR_SIZE;
    strncpy(dir_rec, c, *dir_rec_size);

    return knd_OK;
}


extern int 
knd_get_elem_suffix(const char *name,
                    char *buf)
{
    char suff = 0;
    const char *c;

    c = name;
    
    while (*c++) {
        if (*c != '_') continue;
        if (!c[1]) return knd_FAIL;
        suff = c[1];
    }

    if (!suff)
        return knd_FAIL;

    buf[0] = suff;
    return knd_OK;
}


extern int 
knd_parse_matching_braces(const char *rec,
                          size_t *chunk_size)
{
    const char *b;
    const char *c;
    size_t brace_count = 0;
    
    c = rec;
    b = c;
    
    while (*c) {
        switch (*c) {
        case '{':
            brace_count++;
            break;
        case '}':
            if (!brace_count)
                return knd_FAIL;
            brace_count--;
            if (!brace_count) {
                *chunk_size = c - b;
                return knd_OK;
            }
            break;
        default:
            break;
        }
        c++;
    }
    
    return knd_FAIL;
}


/**
 */
extern int
knd_parse_num(const char *val,
	      long *result)
/*int *warning)*/
{
    long numval;
    char *invalid_num_char = NULL;
    int err = knd_OK;

    if (!val) return knd_FAIL;

    errno = 0;

    /* fix core typos, raise a warning */
    /*c = val;
    while (*c) {
        if ((*c) == 'O' || (*c) == 'o') {
            *c = '0';
            warning = KND_IS_TYPO;
        }
        c++;
        }*/
    
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


int
knd_parse_IPV4(char *ip, unsigned long *ip_val)
{
    char *field;
    const char *delim = ".";
    char *last;
    long numval;
    int i = 0, err = 0;
    int bit_shift = 8;
    int curr_shift = 24;
    unsigned long result = 0;

    for (field = strtok_r(ip, delim, &last);
         field;
         field = strtok_r(NULL, delim, &last)) {

        if (i > 3) return -1;

        err = knd_parse_num(field, &numval);
        if (err) return err;

        if (!curr_shift)
            result |= numval;
        else
            result |= numval << curr_shift; 

        i++;
        curr_shift -= bit_shift;
    }

    *ip_val = (unsigned long)result;
    
    return knd_OK;
}


extern int
knd_parse_task(const char *rec,
               size_t *total_size,
               struct kndTaskSpec *specs,
               size_t num_specs)
{
    const char *b, *c, *e;
    size_t buf_size;

    struct kndTaskSpec *spec;
    struct kndTaskSpec *curr_spec = NULL;

    struct kndTaskArg args[KND_MAX_ARGS];
    size_t num_args = 0;
    struct kndTaskArg *arg;
    
    bool in_field = false;
    bool in_terminal = false;
    bool got_task = false;
    
    size_t chunk_size;
    int err;
    
    c = rec;
    b = rec;
    e = rec;
    
     if (DEBUG_PARSER_LEVEL_2)
         knd_log("\n.. parse task: \"%s\"", rec);
  
    while (*c) {
        switch (*c) {
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            if (!in_field) break;

            if (!in_terminal) {
                err = check_name_limits(b, e, &buf_size);
                if (err) return err;

                if (DEBUG_PARSER_LEVEL_2)
                    knd_log("++ got field: \"%.*s\" [%lu]",
                            buf_size, b, (unsigned long)buf_size);

                for (size_t i = 0; i < num_specs; i++) {
                    spec = &specs[i];

                    if (spec->is_completed) {
                        if (DEBUG_PARSER_LEVEL_2)
                            knd_log("++ \"%s\" spec successfully completed!",
                                    spec->name);
                        continue;
                    }
                    
                    if (DEBUG_PARSER_LEVEL_2)
                        knd_log("== curr SPEC name: \"%s\"", spec->name);

                    if (!strncmp(b, spec->name, spec->name_size)) {
                        curr_spec = spec;
                        got_task = true;

                        if (!spec->parse) {
                            if (DEBUG_PARSER_LEVEL_2)
                                knd_log("   == ATOMIC SPEC found: %s! no further parsing is required.",
                                        spec->name);
                            in_terminal = true;
                            b = c + 1;
                            e = b;
                            break;
                        }

                        /* nested parsing required */
                        if (DEBUG_PARSER_LEVEL_3)
                            knd_log("   == further parsing required in \"%s\"",
                                    spec->name);

                        err = spec->parse(spec->obj, b, &chunk_size);
                        if (err) return err;

                        c += chunk_size;
                        
                        spec->is_completed = true;
                        in_terminal = false;
                        in_field = false;

                        if (DEBUG_PARSER_LEVEL_2)
                            knd_log("\n\n   == remainder after parsing \"%s\": \"%s\"",
                                    spec->name, c);
                        
                        b = c + 1;
                        e = b;
                        break;
                    }

                    /*if (DEBUG_PARSER_LEVEL_TMP)
                        knd_log("-- unrecognized task: \"%.*s\" :(",
                                buf_size, b);

                                return knd_FAIL; */
                    
                }
            }
            
            break;
        case '{':
            if (!in_field) {
                in_field = true;
                b = c + 1;
                e = b;
                break;
            }
            
            break;
        case '}':
            if (in_terminal) {
                err = check_name_limits(b, e, &buf_size);
                if (err) return err;

                if (DEBUG_PARSER_LEVEL_2)
                    knd_log("++ got arg: \"%.*s\" [%lu]",
                            buf_size, b, (unsigned long)buf_size);

                arg = &args[num_args];
                num_args++;

                memcpy(arg->name, curr_spec->name, curr_spec->name_size);
                arg->name_size = curr_spec->name_size;
                
                memcpy(arg->val, b, buf_size);
                arg->val_size = buf_size;

                if (spec->run) {
                    err = spec->run(spec->obj, args, num_args);
                    if (err) {
                        if (DEBUG_PARSER_LEVEL_TMP)
                            knd_log("-- \"%s\" func run failed: %d :(",
                                    spec->name, err);
                        return err;
                    }
                    got_task = true;
                }

                b = c + 1;
                e = b;
                in_terminal = false;
                in_field = false;
                break;
            }

            if (!got_task) return knd_NO_MATCH;
            
            *total_size = c - rec;
            return knd_OK;
        default:
            e = c + 1;
            break;
        }
        c++;
    }

    if (!got_task) return knd_NO_MATCH;

    
    *total_size = c - rec;
    return knd_OK;
    /*return knd_FAIL;*/
}

