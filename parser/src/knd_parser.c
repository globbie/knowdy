#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* numeric conversion by strtol */
#include <errno.h>
#include <limits.h>

#include <sys/stat.h>
#include <fcntl.h>

#include <unistd.h>

#include "knd_config.h"
#include "knd_parser.h"
#include "knd_concept.h"
#include "knd_task.h"
#include "knd_utils.h"

#define DEBUG_PARSER_LEVEL_1 0
#define DEBUG_PARSER_LEVEL_2 0
#define DEBUG_PARSER_LEVEL_3 0
#define DEBUG_PARSER_LEVEL_4 0
#define DEBUG_PARSER_LEVEL_TMP 1

static int knd_parse_state_change(const char *rec,
                                  size_t *total_size,
                                  struct kndTaskSpec *specs,
                                  size_t num_specs);
static int knd_parse_list(const char *rec,
                          size_t *total_size,
                          struct kndTaskSpec *specs,
                          size_t num_specs);

/* copy a sequence of non-whitespace chars */
int
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



static int
check_name_limits(const char *b, const char *e, size_t *buf_size)
{
    *buf_size = e - b;
    if (!(*buf_size)) {
        knd_log("-- empty name?");
        return knd_LIMIT;
    }
    if ((*buf_size) >= KND_NAME_SIZE) {
        knd_log("-- field tag too large: %zu", buf_size);
        return knd_LIMIT;
    }
    return knd_OK;
}

int
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


int
knd_get_schema_name(const char *rec,
                    char *buf,
                    size_t *buf_size,
                    size_t *total_size)
{
    const char *b, *c, *e;
    size_t chunk_size = 0;
    size_t max_size = *buf_size;

    if (strncmp(rec, "::", strlen("::"))) return knd_FAIL;

    c = rec + strlen("::");
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
            if (chunk_size > max_size) return knd_LIMIT;
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


int
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
int knd_parse_num(const char *val,
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

    *ip_val = result;

    return knd_OK;
}

static int
knd_find_spec(struct kndTaskSpec *specs,
              size_t num_specs,
              const char *name,
              size_t name_size,
              knd_task_spec_type spec_type,
              struct kndTaskSpec **result)
{
    struct kndTaskSpec *spec;
    struct kndTaskSpec *default_spec = NULL;
    struct kndTaskSpec *validator_spec = NULL;
    bool is_completed = false;

    for (size_t i = 0; i < num_specs; i++) {
        spec = &specs[i];

        if (spec->type != spec_type) continue;

        /* some useful action took place */
        if (!spec->is_selector && spec->is_completed) {
            is_completed = true;
        }
        
        if (spec->is_default) {
            default_spec = spec;
            continue;
        }
        if (spec->is_validator) {
            validator_spec = spec;
            continue;
        }

        if (name_size != spec->name_size) continue;
        
        if (strncmp(name, spec->name, spec->name_size) != 0) {
            continue;
        }
        

        *result = spec;
        return knd_OK;
    }

    if (DEBUG_PARSER_LEVEL_2)
        knd_log("-- no named spec found for \"%.*s\"  validator: %p",
                name_size, name, validator_spec);

    if (validator_spec) {
        if (name_size >= validator_spec->max_buf_size)
            return knd_LIMIT;
        memcpy(validator_spec->buf, name, name_size);
        *validator_spec->buf_size = name_size;
        validator_spec->buf[name_size] = '\0';
        *result = validator_spec;
        return knd_OK;
    }

    /* run default action only if nothing else was activated before */
    if (default_spec && !is_completed) {
        *result = default_spec;
        return knd_OK;
    }
    
    return knd_NO_MATCH;
}

static int
knd_spec_buf_copy(struct kndTaskSpec *spec,
                  const char *val,
                  size_t val_size)
{
    if (DEBUG_PARSER_LEVEL_2)
        knd_log(".. writing val \"%.*s\" to buf [max size: %lu] [len: %lu]..",
                val_size, val,
                (unsigned long)spec->max_buf_size,
                (unsigned long)val_size);
 
    if (val_size >= spec->max_buf_size) {
        knd_log("-- %s: buf limit reached: %lu max: %lu",
                spec->name,
                (unsigned long)val_size,
                (unsigned long)spec->max_buf_size);
        return knd_LIMIT;
    }

    memcpy(spec->buf, val, val_size);
    spec->buf[val_size] = '\0';
    *spec->buf_size = val_size;

    return knd_OK;
}


static int knd_check_implied_field(const char *name,
                                   size_t name_size,
                                   struct kndTaskSpec *specs,
                                   size_t num_specs,
                                   struct kndTaskArg *args,
                                   size_t *num_args)
{
    struct kndTaskSpec *spec = NULL;
    struct kndTaskArg *arg;
    const char *impl_arg_name = "_impl";
    size_t impl_arg_name_size = strlen("_impl");
    int err;
    
    if (DEBUG_PARSER_LEVEL_2)
        knd_log("++ got implied val: \"%.*s\" [%lu]",
                name_size, name, (unsigned long)name_size);
    if (name_size >= KND_NAME_SIZE) return knd_LIMIT;
                        
    arg = &args[*num_args];
    memcpy(arg->name, impl_arg_name, impl_arg_name_size);
    arg->name[impl_arg_name_size] = '\0';
    arg->name_size = impl_arg_name_size;

    memcpy(arg->val, name, name_size);
    arg->val[name_size] = '\0';
    arg->val_size = name_size;

    (*num_args)++;

    /* any action needed? */
    for (size_t i = 0; i < num_specs; i++) {
        spec = &specs[i];
        if (spec->is_implied) {
            if (DEBUG_PARSER_LEVEL_2)
                knd_log("++ got implied spec: \"%s\" run: %p!",
                        spec->name, spec->run);
            if (spec->run) {
                err = spec->run(spec->obj, args, *num_args);
                if (err) {
                        knd_log("-- implied func for \"%.*s\" failed: %d :(",
                                name_size, name, err);
                    return err;
                }
                //spec->is_completed = true;
            }
            break;
        }
    }
    
    return knd_OK;
}

int knd_parse_task(const char *rec,
                   size_t *total_size,
                   struct kndTaskSpec *specs,
                   size_t num_specs)
{
    const char *b, *c, *e;
    size_t name_size;
    
    struct kndTaskSpec *spec = NULL;

    struct kndTaskArg args[KND_MAX_ARGS];
    size_t num_args = 0;
    struct kndTaskArg *arg;

    bool in_field = false;
    bool in_implied_field = false;
    bool in_tag = false;
    bool in_terminal = false;

    size_t chunk_size;
    int err;

    c = rec;
    b = rec;
    e = rec;

    if (DEBUG_PARSER_LEVEL_1)
        knd_log("\n\n*** start basic PARSING: \"%s\" num specs: %lu [%p]",
                rec, (unsigned long)num_specs, specs);

    while (*c) {
        switch (*c) {
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            if (!in_field) {
                break;
            }
            if (in_terminal) break;

            if (DEBUG_PARSER_LEVEL_2)
                knd_log("+ whitespace in basic PARSING!");

            err = check_name_limits(b, e, &name_size);
            if (err) return err;

            if (DEBUG_PARSER_LEVEL_2)
                knd_log("++ BASIC LOOP got tag after whitespace: \"%.*s\" [%lu]",
                        name_size, b, (unsigned long)name_size);

            err = knd_find_spec(specs, num_specs, b, name_size, KND_GET_STATE, &spec);
            if (err) {
                knd_log("-- no spec found to handle the \"%.*s\" tag: %d",
                        name_size, b, err);
                return err;
            }

            if (DEBUG_PARSER_LEVEL_2)
                knd_log("++ got SPEC: \"%s\" (default: %d) (is validator: %d)",
                        spec->name, spec->is_default, spec->is_validator);
            in_tag = true;

            if (spec->validate) {
                err = spec->validate(spec->obj,
                                     (const char*)spec->buf, *spec->buf_size,
                                     c, &chunk_size);
                if (err) {
                    knd_log("-- ERR: %d validation of spec \"%s\" failed :(",
                            err, spec->name);
                    return err;
                }
                c += chunk_size;
                spec->is_completed = true;
                in_field = false;
                in_tag = false;
                b = c;
                e = b;
                break;
            }
            
            if (!spec->parse) {
                if (DEBUG_PARSER_LEVEL_2)
                    knd_log("== ATOMIC SPEC found: %s! no further parsing is required.",
                            spec->name);
                in_terminal = true;
                b = c + 1;
                e = b;
                break;
            }

            /* nested parsing required */
            if (DEBUG_PARSER_LEVEL_2)
                knd_log("\n    >>> further parsing required in \"%s\" FROM: \"%s\" FUNC: %p",
                        spec->name, c, spec->parse);
            err = spec->parse(spec->obj, c, &chunk_size);
            if (err) {
                knd_log("-- ERR: %d parsing of spec \"%s\" failed :(",
                        err, spec->name);
                return err;
            }
            c += chunk_size;
            spec->is_completed = true;
            in_field = false;
            in_tag = false;
            
            b = c;
            e = b;
            break;
        case '{':
            /* starting brace */
            if (!in_field) {
                if (in_implied_field) {
                    name_size = e - b;
                    if (name_size) {
                        err = knd_check_implied_field(b, name_size, specs, num_specs, args, &num_args);
                        if (err) return err;
                    }
                }
                
                in_field = true;
                in_terminal = false;
                b = c + 1;
                e = b;
                break;
            }

            /* inner field brace */
            err = check_name_limits(b, e, &name_size);
            if (err) return err;

            if (DEBUG_PARSER_LEVEL_2)
                knd_log("++ BASIC LOOP got tag after brace: \"%.*s\" [%lu]",
                        name_size, b, (unsigned long)name_size);

            err = knd_find_spec(specs, num_specs, b, name_size, KND_GET_STATE, &spec);
            if (err) {
                knd_log("-- no spec found to handle the \"%.*s\" tag: %d",
                        name_size, b, err);
                return err;
            }

            if (DEBUG_PARSER_LEVEL_2)
                knd_log("++ got SPEC: \"%s\" (default: %d) (is validator: %d)",
                        spec->name, spec->is_default, spec->is_validator);
            in_tag = true;

            if (spec->validate) {
                err = spec->validate(spec->obj,
                                     (const char*)spec->buf, *spec->buf_size,
                                     c, &chunk_size);
                if (err) {
                    knd_log("-- ERR: %d validation of spec \"%s\" failed :(",
                            err, spec->name);
                    return err;
                }
                c += chunk_size;
                spec->is_completed = true;
                in_field = false;
                in_tag = false;
                b = c;
                e = b;
                break;
            }
            
            if (!spec->parse) {
                if (DEBUG_PARSER_LEVEL_2)
                    knd_log("== ATOMIC SPEC found: %s! no further parsing is required.",
                            spec->name);
                in_terminal = true;
                b = c + 1;
                e = b;
                break;
            }

            /* nested parsing required */
            if (DEBUG_PARSER_LEVEL_2)
                knd_log("== further parsing required in \"%s\" FROM: \"%s\"",
                        spec->name, c);

            err = spec->parse(spec->obj, c, &chunk_size);
            if (err) {
                knd_log("-- ERR: %d parsing of spec \"%s\" failed :(",
                        err, spec->name);
                return err;
            }
            
            c += chunk_size;

            spec->is_completed = true;

            in_field = false;
            in_tag = false;
            
            b = c;
            e = b;
            break;
        case '}':

            /* empty body? */
            if (!in_field) {
                if (in_implied_field) {
                    name_size = e - b;
                    if (name_size) {
                        err = knd_check_implied_field(b, name_size, specs, num_specs, args, &num_args);
                        if (err) return err;
                    }
                }
                
                /* should we run a default action? */
                for (size_t i = 0; i < num_specs; i++) {
                    spec = &specs[i];
                    /* some action spec is completed, don't call the default one */
                    if (spec->is_completed) {
                        *total_size = c - rec;
                        return knd_OK;
                    }
                }
                
                /* fetch default spec if any */
                err = knd_find_spec(specs, num_specs,
                                    "default", strlen("default"), KND_GET_STATE, &spec);
                if (!err) {
                    if (spec->run) {
                        err = spec->run(spec->obj, args, num_args);
                        if (err) {
                            knd_log("-- \"%s\" func run failed: %d :(",
                                    spec->name, err);
                            return err;
                        }
                    }
                }
                
                *total_size = c - rec;
                return knd_OK;
            }
            
            if (in_terminal) {
                
                err = check_name_limits(b, e, &name_size);
                if (err) {
                    knd_log("-- name limit reached :(");
                    return err;
                }
                if (DEBUG_PARSER_LEVEL_2)
                    knd_log("++ got terminal val: \"%.*s\" [%lu]",
                            name_size, b, (unsigned long)name_size);
                /* copy to buf */
                if (spec->buf && spec->buf_size) {
                    err = knd_spec_buf_copy(spec, b, name_size);
                    if (err) return err;
                        
                    spec->is_completed = true;
                    b = c + 1;
                    e = b;
                    in_terminal = false;
                    in_tag = false;
                    in_field = false;
                    break;
                }

     
                arg = &args[num_args];
                num_args++;
                memcpy(arg->name, spec->name, spec->name_size);
                arg->name[spec->name_size] = '\0';
                arg->name_size = spec->name_size;

                memcpy(arg->val, b, name_size);
                arg->val[name_size] = '\0';
                arg->val_size = name_size;

                if (spec->run) {
                    err = spec->run(spec->obj, args, num_args);
                    if (err) {
                        knd_log("-- \"%s\" func run failed: %d :(",
                                spec->name, err);
                        return err;
                    }
                    spec->is_completed = true;
                }
                
                b = c + 1;
                e = b;

                in_terminal = false;
                in_tag = false;
                in_field = false;
                break;
            }

            if (in_field) {
                if (!in_tag) {
                    err = check_name_limits(b, e, &name_size);
                    if (err) {
                        knd_log("-- value name limit reached :(");
                        return err;
                    }
                    
                    if (DEBUG_PARSER_LEVEL_2)
                        knd_log("++ got default spec val: \"%.*s\" [%lu]?",
                                name_size, b, (unsigned long)name_size);
                    
                    err = knd_find_spec(specs, num_specs, b, name_size, KND_GET_STATE, &spec);
                    if (err) {
                        knd_log("-- no spec found to handle the \"%.*s\" tag :(", name_size, b);
                        return err;
                    }

                    if (DEBUG_PARSER_LEVEL_2)
                        knd_log("++ got single SPEC: \"%s\" (default: %d)",
                                spec->name, spec->is_default);

                    if (spec->parse) {
                        err = spec->parse(spec->obj, c, &chunk_size);
                        if (err) {
                            knd_log("-- ERR: %d parsing of spec \"%s\" failed starting from: %s",
                                    err, spec->name, b);
                            return err;
                        }
                    }
                    
                    /*if (spec->buf && spec->buf_size) {
                        err = knd_spec_buf_copy(spec, b, name_size);
                        if (err) return err;
                        spec->is_completed = true;
                        b = c + 1;
                        e = b;
                        in_terminal = false;
                        in_tag = false;
                        in_field = false;
                        }*/
                    
                }
                
                in_field = false;
                break;
            }
            *total_size = c - rec;
            return knd_OK;
        case '(':
            if (DEBUG_PARSER_LEVEL_2)
                knd_log(".. basic LOOP %p detected func area: \"%s\"\n", specs, c);
 
            if (in_implied_field) {
                if (DEBUG_PARSER_LEVEL_2)
                    knd_log(".. basic LOOP is in implied field!");

                name_size = e - b;
                if (name_size) {
                    err = knd_check_implied_field(b, name_size, specs, num_specs, args, &num_args);
                    if (err) return err;
                }
                in_implied_field = false;
            }

            err = knd_parse_state_change(c, &chunk_size, specs, num_specs);
            if (err) {
                knd_log("-- basic LOOP failed to parse state change area :(");
                return err;
            }
            c += chunk_size;

            if (DEBUG_PARSER_LEVEL_2)
                knd_log(".. basic LOOP %p finished func parsing at: \"%s\"\n", specs, c);

            in_field = false;
            in_terminal = false;
            in_implied_field = false;

            b = c + 1;
            e = b;
            break;
        case ')':
            if (DEBUG_PARSER_LEVEL_2)
                knd_log("\n\n-- END OF BASIC LOOP [%p]  STARTED at: \"%s\" FINISHED at: \"%s\"\n\n",
                        specs, rec, c);

            if (!in_field) {
                if (in_implied_field) {
                    name_size = e - b;
                    if (name_size) {
                        err = knd_check_implied_field(b, name_size, specs, num_specs, args, &num_args);
                        if (err) return err;
                    }
                }
            }
            *total_size = c - rec;
            return knd_OK;
        case '[':
            err = knd_parse_list(c, &chunk_size, specs, num_specs);
            if (err) {
                knd_log("-- basic LOOP failed to parse the list area :(");
                return err;
            }
            c += chunk_size;
            in_field = false;
            in_terminal = false;
            in_implied_field = false;
            b = c + 1;
            e = b;
            break;
        default:
            e = c + 1;
            if (!in_field) {
                if (!in_implied_field) {
                    b = c;
                    in_implied_field = true;
                }
            }
            break;
        }
        c++;
    }

    if (DEBUG_PARSER_LEVEL_1)
        knd_log("\n\n--- end of basic PARSING: \"%s\" num specs: %lu [%p]",
                rec, (unsigned long)num_specs, specs);
    
    *total_size = c - rec;
    return knd_OK;
}


static int knd_parse_state_change(const char *rec,
                                  size_t *total_size,
                                  struct kndTaskSpec *specs,
                                  size_t num_specs)
{
    const char *b, *c, *e;
    size_t name_size;

    struct kndTaskSpec *spec = NULL;
    struct kndTaskArg args[KND_MAX_ARGS];
    size_t num_args = 0;

    bool in_change = false;
    bool in_tag = false;
    bool in_field = false;
    bool in_implied_field = false;

    size_t chunk_size;
    int err;

    c = rec;
    b = rec;
    e = rec;

    if (DEBUG_PARSER_LEVEL_2)
        knd_log("\n\n == start FUNC parse: \"%.*s\" num specs: %lu [%p]",
                16, rec, (unsigned long)num_specs, specs);

    while (*c) {
        switch (*c) {
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            if (!in_change) break;
            if (in_tag) break;

            if (DEBUG_PARSER_LEVEL_2)
                knd_log("+ whitespace in FUNC loop: \"%s\"\n\n\n", c);

            err = check_name_limits(b, e, &name_size);
            if (err) return err;

            if (DEBUG_PARSER_LEVEL_2)
                knd_log("++ FUNC LOOP got tag: \"%.*s\" [%lu]",
                        name_size, b, (unsigned long)name_size);

            err = knd_find_spec(specs, num_specs, b, name_size, KND_CHANGE_STATE, &spec);
            if (err) {
                knd_log("-- no spec found to handle the \"%.*s\" change state tag :(",
                        name_size, b);
                return err;
            }

            if (DEBUG_PARSER_LEVEL_2)
                knd_log("++ got func SPEC: \"%s\"  default: %d  terminal: %d",
                        spec->name, spec->is_default, spec->is_terminal);

            if (spec->validate) {
                err = spec->validate(spec->obj,
                                     (const char*)spec->buf, *spec->buf_size,
                                     c, &chunk_size);
                if (err) {
                    knd_log("-- ERR: %d validation of spec \"%s\" failed :(",
                            err, spec->name);
                    return err;
                }
                // sharing bracket
                c += chunk_size - 1;
                in_change = false;
                in_tag = false;
                b = c;
                e = b;
                break;
            }

            if (spec->parse) {
                if (DEBUG_PARSER_LEVEL_2)
                    knd_log("== func parsing required in \"%s\" FROM: \"%s\"",
                            spec->name, c);

                err = spec->parse(spec->obj, c, &chunk_size);
                if (err) {
                    knd_log("-- ERR: %d parsing of spec \"%s\" failed :(",
                            err, spec->name);
                    return err;
                }

                // sharing the closing square bracket
                c += chunk_size - 1;
                
                in_change = false;
                in_tag = false;
                b = c + 1;
                e = b;

                if (DEBUG_PARSER_LEVEL_2)
                    knd_log("== END func parsing spec \"%s\" [%p] at: \"%s\"\n\n",
                            spec->name, specs, c);
                break;
            }
            
            in_tag = true;
            b = c + 1;
            e = b;
            break;
        case '(':
            if (in_implied_field) {
                name_size = e - b;
                if (name_size) {
                    err = knd_check_implied_field(b, name_size, specs, num_specs, args, &num_args);
                    if (err) return err;
                }
                
                in_implied_field = false;
                b = c + 1;
                e = b;
                break;
            }

            if (!in_change) {
                in_change = true;
                
                b = c + 1;
                e = b;
                break;
            }
            break;
        case ')':
            /*if (in_change) {
                in_implied_field = false;
                in_change = false;
                in_tag = false;
                break;
            }
            */

            if (!spec) {
                /* activate spec search */
                err = check_name_limits(b, e, &name_size);
                if (err) return err;

                if (DEBUG_PARSER_LEVEL_2)
                    knd_log("++ FUNC LOOP got tag in close bracket: \"%.*s\" [%lu]",
                            name_size, b, (unsigned long)name_size);

                err = knd_find_spec(specs, num_specs, b, name_size, KND_CHANGE_STATE, &spec);
                if (err) {
                    knd_log("-- no spec found to handle the \"%.*s\" change state tag :(",
                            name_size, b);
                    return err;
                }

                if (DEBUG_PARSER_LEVEL_2)
                    knd_log("++ got func SPEC: \"%s\"  default: %d  terminal: %d",
                            spec->name, spec->is_default, spec->is_terminal);
            }

            if (DEBUG_PARSER_LEVEL_2) {
                chunk_size = c - rec;
                if (chunk_size > KND_MAX_DEBUG_CONTEXT_SIZE)
                    chunk_size = KND_MAX_DEBUG_CONTEXT_SIZE;
                knd_log("-- close bracket at FUNC loop: \"%.*s\"\nIN CHANGE: %d\nSPECS: %s\n",
                        chunk_size, c, in_change, specs[0].name);
            }
            
            /* copy to buf */
            if (spec->is_terminal) {
                err = check_name_limits(b, e, &name_size);
                if (err) {
                    knd_log("-- name limit reached :(");
                    return err;
                }
            
                if (DEBUG_PARSER_LEVEL_2)
                    knd_log("++ got func terminal: \"%.*s\" [%lu]",
                            name_size, b, (unsigned long)name_size);
                
                err = knd_spec_buf_copy(spec, b, name_size);
                if (err) return err;
                spec->is_completed = true;
            }
            
            if (spec->run) {
                err = spec->run(spec->obj, args, num_args);
                if (err) {
                    knd_log("-- \"%s\" func run failed: %d :(",
                            spec->name, err);
                    return err;
                }
            }

            if (DEBUG_PARSER_LEVEL_2)
                knd_log("== END parse state change: \"%.*s\" [%p]",
                        16, c, specs);
            
            *total_size = c - rec;
            return knd_OK;
        default:
            e = c + 1;

            if (!in_field) {
                if (!in_implied_field) {
                    b = c;
                    in_implied_field = true;
                }
            }
            
            break;
        }
        c++;
    }

    //*total_size = c - rec;
    return knd_FAIL;
}

static int knd_parse_list(const char *rec,
                          size_t *total_size,
                          struct kndTaskSpec *specs,
                          size_t num_specs)
{
    const char *b, *c, *e;
    size_t name_size;

    void *accu = NULL;
    void *item = NULL;

    struct kndTaskSpec *spec = NULL;
    int (*append_item)(void *accu, void *item) = NULL;
    int (*alloc_item)(void *accu, const char *name, size_t name_size, size_t count, void **item) = NULL;

    bool in_list = false;
    bool got_tag = false;
    bool in_item = false;
    size_t chunk_size = 0;
    size_t item_count = 0;
    int err;

    c = rec;
    b = rec;
    e = rec;

    if (DEBUG_PARSER_LEVEL_2)
        knd_log(".. start list parsing: \"%.*s\" num specs: %lu [%p]",
                16, rec, (unsigned long)num_specs, specs);

    while (*c) {
        switch (*c) {
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            if (!in_list) break;
            if (got_tag) {

                if (spec->is_atomic) {
                    /* get atomic item */
                    err = check_name_limits(b, e, &name_size);
                    if (err) return err;

                    if (DEBUG_PARSER_LEVEL_2)
                        knd_log("  == got new item: \"%.*s\"",
                                name_size, b);
                    err = alloc_item(accu, b, name_size, item_count, &item);
                    if (err) return err;

                    item_count++;
                    b = c + 1;
                    e = b;
                    break;
                }
                
                /* get list item's name */
                err = check_name_limits(b, e, &name_size);
                if (err) return err;

                if (DEBUG_PARSER_LEVEL_2)
                    knd_log("  == list got new item: \"%.*s\" %p",
                            name_size, b, alloc_item);

                err = alloc_item(accu, b, name_size, item_count, &item);
                if (err) return err;

                /* parse item */
                err = spec->parse(item, c, &chunk_size);
                if (err) {
                    knd_log("-- list item parsing failed :(");
                    return err;
                }

                c += chunk_size;

                err = append_item(accu, item);
                if (err) return err;

                in_item = false;
                b = c + 1;
                e = b;
                break;
            }
            
            err = check_name_limits(b, e, &name_size);
            if (err) return err;

            if (DEBUG_PARSER_LEVEL_TMP)
                knd_log("++ list got tag: \"%.*s\" [%lu]",
                        name_size, b, (unsigned long)name_size);

            err = knd_find_spec(specs, num_specs, b, name_size, KND_GET_STATE, &spec);
            if (err) {
                knd_log("-- no spec found to handle the \"%.*s\" list tag :(",
                        name_size, b);
                return err;
            }

            if (DEBUG_PARSER_LEVEL_TMP)
                knd_log("++ got list SPEC: \"%s\"",
                        spec->name);

            if (spec->is_atomic) {
                if (!spec->accu) return knd_FAIL;
                if (!spec->alloc) return knd_FAIL;
                got_tag = true;
                accu = spec->accu;
                alloc_item = spec->alloc;
                b = c + 1;
                e = b;
                break;
            }
            
            if (!spec->append) return knd_FAIL;
            if (!spec->accu) return knd_FAIL;
            if (!spec->alloc) return knd_FAIL;
            if (!spec->parse) return knd_FAIL;

            append_item = spec->append;
            accu = spec->accu;
            alloc_item = spec->alloc;

            got_tag = true;
            b = c + 1;
            e = b;
            break;
        case '{':
            if (!in_list) break;
            if (!got_tag) {
                err = check_name_limits(b, e, &name_size);
                if (err) return err;

                if (DEBUG_PARSER_LEVEL_2)
                    knd_log("++ list got tag: \"%.*s\" [%lu]",
                            name_size, b, (unsigned long)name_size);

                err = knd_find_spec(specs, num_specs, b, name_size, KND_GET_STATE, &spec);
                if (err) {
                    knd_log("-- no spec found to handle the \"%.*s\" list tag :(",
                            name_size, b);
                    return err;
                }

                if (DEBUG_PARSER_LEVEL_2)
                    knd_log("++ got list SPEC: \"%s\"", spec->name);

                if (!spec->append) return knd_FAIL;
                if (!spec->accu) return knd_FAIL;
                if (!spec->alloc) return knd_FAIL;
                if (!spec->parse) return knd_FAIL;
                
                append_item = spec->append;
                accu = spec->accu;
                alloc_item = spec->alloc;

                got_tag = true;
            }

            /* new list item */
            if (!in_item) {
                in_item = true;
                b = c + 1;
                e = b;
                break;
            }

            b = c + 1;
            e = b;
            break;
        case '}':
            return knd_FAIL;
        case '[':
            if (in_list) return knd_FAIL;
            in_list = true;
            b = c + 1;
            e = b;
            break;
        case ']':
            if (!in_list) return knd_FAIL;

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
