#undef NDEBUG  // For developing
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
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
#define DEBUG_PARSER_LEVEL_2 1
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
    if (!*buf_size) {
        knd_log("-- empty name?");
        return knd_LIMIT;
    }
    if (*buf_size > KND_NAME_SIZE) {
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


int knd_parse_matching_braces(const char *rec,
                              size_t brace_count,
                              size_t *chunk_size)
{
    const char *b;
    const char *c;

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

    assert(val != NULL);
       
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

static int
knd_run_set_size_t(void *obj,
                   struct kndTaskArg *args, size_t num_args)
{
    size_t *self = (size_t *)obj;
    struct kndTaskArg *arg;
    char *num_end;
    unsigned long long num;

    assert(args && num_args == 1);
    arg = &args[0];

    assert(arg->name_size == strlen("_impl") && !memcmp(arg->name, "_impl", arg->name_size));
    assert(arg->val && arg->val_size != 0);

    if (!isdigit(arg->val[0])) {
        knd_log("-- num size_t doesn't start from a digit: \"%.*s\"",
                arg->val_size, arg->val);
        return knd_FORMAT;
    }

    errno = 0;
    num = strtoull(arg->val, &num_end, KND_NUM_ENCODE_BASE);  // FIXME(ki.stfu): Null-terminated string is expected
    if (errno == ERANGE && num == ULLONG_MAX) {
        knd_log("-- num limit reached: %.*s max: %llu",
                arg->val_size, arg->val, ULLONG_MAX);
        return knd_LIMIT;
    }
    else if (errno != 0 && num == 0) {
        knd_log("-- cannot convert \"%.*s\" to num: %d",
                arg->val_size, arg->val, errno);
        return knd_FORMAT;
    }

    if (arg->val + arg->val_size != num_end) {
        knd_log("-- not all characters in \"%.*s\" were parsed: \"%.*s\"",
                arg->val_size, arg->val, num_end - arg->val, arg->val);
        return knd_FORMAT;
    }

    if (ULLONG_MAX > SIZE_MAX && num > SIZE_MAX) {
        knd_log("-- num size_t limit reached: %llu max: %llu",
                num, (unsigned long long)SIZE_MAX);
        return knd_LIMIT;
    }

    *self = (size_t)num;
    if (DEBUG_PARSER_LEVEL_2)
        knd_log("++ got num size_t: %zu",
                *self);

    return knd_OK;
}

int
knd_parse_size_t(void *obj,
                 const char *rec,
                 size_t *total_size)
{
    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = knd_run_set_size_t,
          .obj = obj
        }
    };
    int err;

    if (DEBUG_PARSER_LEVEL_2)
        knd_log(".. parse num size_t: \"%.*s\"", 16, rec);

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    return knd_OK;
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
knd_spec_is_correct(struct kndTaskSpec *spec)
{
    if (DEBUG_PARSER_LEVEL_2) 
        knd_log(".. check spec: \"%.*s\"..", spec->name_size, spec->name);

    // Check the fields are not mutually exclusive (by groups):

    assert(spec->type == KND_GET_STATE || spec->type == KND_CHANGE_STATE);

    assert((spec->name != NULL) == (spec->name_size != 0));

    assert(spec->specs == NULL && spec->num_specs == 0);  // TODO(ki.stfu): Remove these fields

    assert(!spec->is_completed);

    if (spec->is_default)
        assert(!spec->is_selector && !spec->is_implied && !spec->is_validator && !spec->is_list && !spec->is_atomic);
    if (spec->is_selector)
        assert(!spec->is_default && !spec->is_validator && !spec->is_list && !spec->is_atomic);
    if (spec->is_implied)
        assert(!spec->is_default && !spec->is_validator && !spec->is_list && !spec->is_atomic);
    if (spec->is_validator)
        assert(!spec->is_default && !spec->is_selector && !spec->is_implied && !spec->is_list && !spec->is_atomic);
    assert(!spec->is_terminal);  // TODO(ki.stfu): ?? Remove this field
    // FIXME(ki.stfu): assert(!spec->is_list);  // TODO(ki.stfu): ?? Remove this field
    assert(!spec->is_atomic);  // TODO(ki.stfu): ?? Remove this field

    assert((spec->buf != NULL) == (spec->buf_size != NULL));
    assert((spec->buf != NULL) == (spec->max_buf_size != 0));

    // TODO(ki.stfu): ?? assert(spec->accu == NULL);  // TODO(ki.stfu): ?? remove this field

    if (spec->parse)
        assert(spec->validate == NULL && spec->run == NULL);
    if (spec->validate)
        assert(spec->parse == NULL && spec->run == NULL);
    if (spec->run)
        assert(spec->parse == NULL && spec->validate == NULL);
    // TODO(ki.stfu): ?? assert(spec->append == NULL);  // TODO(ki.stfu): ?? remove this field
    // TODO(ki.stfu): ?? assert(spec->alloc == NULL);  // TODO(ki.stfu): ?? remove this field

    // Check that they are not mutually exclusive (in general):

    if (spec->name) {
        bool name_is_default = spec->name_size == strlen("default") &&
                               0 == memcmp(spec->name, "default", spec->name_size);
        assert(name_is_default == spec->is_default);
    }

    if (spec->is_default) {
        // |spec->name| can be set to "default"
        assert(spec->buf == NULL);
        assert(spec->obj != NULL);
        assert(spec->run != NULL);
    }

    if (spec->is_implied) {
        // |spec->name| can be NULL
        assert(spec->buf == NULL);
        assert(spec->obj != NULL);
        assert(spec->run != NULL);
    }

    assert(spec->is_validator == (spec->validate != NULL));
    if (spec->is_validator) {
        // FIXME(ki.stfu): ?? assert(spec->name != NULL);
        assert(spec->buf != NULL);
        assert(spec->obj != NULL);
        assert(spec->validate != NULL);
    }

    if (spec->is_list) {
        assert(spec->accu != NULL);
        assert(spec->alloc != NULL);
        assert(spec->append != NULL);
        assert(spec->parse != NULL);
        assert(spec->obj == NULL);
    }

    if (spec->parse) {
        assert(spec->name != NULL);
        assert(spec->buf == NULL);
        if (!spec->is_list)
            assert(spec->obj != NULL);
    }

    // if (spec->validate)  -- already handled in spec->is_validator

    if (spec->run) {
        assert(spec->is_default || spec->is_implied || spec->name != NULL);
        assert(spec->buf == NULL);
        assert(spec->obj != NULL);
    }

    if (spec->buf) {
        // |spec->obj| can be NULL (depends on |spec->validate|)
        assert(spec->parse == NULL);
        // |spec->validate| can be NULL
        assert(spec->run == NULL);
    }

    assert(spec->buf != NULL || spec->parse != NULL || spec->validate != NULL || spec->run != NULL);

    return 1;
}

static int
knd_spec_buf_copy(struct kndTaskSpec *spec,
                  const char *val,
                  size_t val_size)
{
    if (DEBUG_PARSER_LEVEL_2)
        knd_log(".. writing val \"%.*s\" to buf [max size: %zu] [len: %zu]..",
                val_size, val, spec->max_buf_size, val_size);

    if (val_size > spec->max_buf_size) {
        knd_log("-- %.*s: buf limit reached: %zu max: %zu",
                spec->name_size, spec->name, val_size, spec->max_buf_size);
        return knd_LIMIT;
    }

    memcpy(spec->buf, val, val_size);
    *spec->buf_size = val_size;

    return knd_OK;
}

static int
knd_find_spec(const char *name,
              size_t name_size,
              knd_task_spec_type spec_type,
              struct kndTaskSpec *specs,
              size_t num_specs,
              struct kndTaskSpec **out_spec)
{
    struct kndTaskSpec *spec;
    struct kndTaskSpec *validator_spec = NULL;
    int err;

    for (size_t i = 0; i < num_specs; i++) {
        spec = &specs[i];

        if (spec->type != spec_type) continue;

        if (spec->is_validator) {
            assert(validator_spec == NULL && "validator_spec was already specified");
            validator_spec = spec;
            continue;
        }

        if (spec->name_size == name_size && !memcmp(spec->name, name, spec->name_size)) {
            *out_spec = spec;
            return knd_OK;
        }
    }

    if (DEBUG_PARSER_LEVEL_2)
        knd_log("-- no named spec found for \"%.*s\"  validator: %p",
                name_size, name, validator_spec);

    if (validator_spec) {
        err = knd_spec_buf_copy(validator_spec, name, name_size);
        if (err) return err;

        *out_spec = validator_spec;
        return knd_OK;
    }

    return knd_NO_MATCH;
}

static int
knd_args_push_back(const char *name,
                   size_t name_size,
                   const char *val,
                   size_t val_size,
                   struct kndTaskArg *args,
                   size_t *num_args)
{
    struct kndTaskArg *arg;

    if (DEBUG_PARSER_LEVEL_2)
        knd_log(".. adding (\"%.*s\", \"%.*s\") to args [size: %zu]..",
                name_size, name, val_size, val, *num_args);

    if (*num_args == KND_MAX_ARGS) {
        knd_log("-- no slot for \"%.*s\" arg [num_args: %zu] :(",
                name_size, name, *num_args);
        return knd_LIMIT;
    }
    assert(name_size <= KND_NAME_SIZE && "arg name is longer than KND_NAME_SIZE");
    assert(val_size <= KND_NAME_SIZE && "arg val is longer than KND_NAME_SIZE");

    arg = &args[*num_args];
    memcpy(arg->name, name, name_size);
    arg->name[name_size] = '\0';
    arg->name_size = name_size;

    memcpy(arg->val, val, val_size);
    arg->val[val_size] = '\0';
    arg->val_size = val_size;

    (*num_args)++;

    return knd_OK;
}

static int
knd_check_implied_field(const char *name,
                        size_t name_size,
                        struct kndTaskSpec *specs,
                        size_t num_specs,
                        struct kndTaskArg *args,
                        size_t *num_args)
{
    struct kndTaskSpec *spec;
    const char *impl_arg_name = "_impl";
    size_t impl_arg_name_size = strlen("_impl");
    int err;
    
    assert(impl_arg_name_size <= KND_NAME_SIZE && "\"_impl\" is longer than KND_NAME_SIZE");
    assert(name_size && "implied val is empty");

    if (DEBUG_PARSER_LEVEL_2)
        knd_log("++ got implied val: \"%.*s\" [%zu]",
                name_size, name, name_size);
    if (name_size > KND_NAME_SIZE) return knd_LIMIT;

    err = knd_args_push_back(impl_arg_name, impl_arg_name_size, name, name_size, args, num_args);
    if (err) return err;

    /* any action needed? */
    for (size_t i = 0; i < num_specs; i++) {
        spec = &specs[i];

        if (spec->is_implied) {
            if (DEBUG_PARSER_LEVEL_2)
                knd_log("++ got implied spec: \"%.*s\" run: %p!",
                        spec->name_size, spec->name, spec->run);

            err = spec->run(spec->obj, args, *num_args);
            if (err) {
                knd_log("-- implied func for \"%.*s\" failed: %d :(",
                        name_size, name, err);
                return err;
            }

            spec->is_completed = true;
            return knd_OK;
        }
    }

    knd_log("-- no implied spec found to handle the \"%.*s\" val",
            name_size, name);

    return knd_NO_MATCH;
}

static int
knd_check_field_tag(const char *name,
                    size_t name_size,
                    knd_task_spec_type type,
                    struct kndTaskSpec *specs,
                    size_t num_specs,
                    struct kndTaskSpec **out_spec)
{
    int err;

    if (!name_size) {
        knd_log("-- empty field tag?");
        return knd_FORMAT;
    }
    if (name_size > KND_NAME_SIZE) {
        knd_log("-- field tag too large: %zu bytes: \"%.*s\"",
                name_size, name_size, name);
        return knd_LIMIT;
    }

    if (DEBUG_PARSER_LEVEL_2)
        knd_log("++ BASIC LOOP got tag after brace: \"%.*s\" [%zu]",
                name_size, name, name_size);

    err = knd_find_spec(name, name_size, type, specs, num_specs, out_spec);
    if (err) {
        knd_log("-- no spec found to handle the \"%.*s\" tag: %d",
                name_size, name, err);
        return err;
    }

    if (DEBUG_PARSER_LEVEL_2)
        knd_log("++ got SPEC: \"%.*s\" (default: %d) (is validator: %d)",
                (*out_spec)->name_size, (*out_spec)->name, (*out_spec)->is_default, (*out_spec)->is_validator);

    return knd_OK;
}

static int
knd_parse_field_value(struct kndTaskSpec *spec,
                      const char *rec,
                      size_t *total_size,
                      bool *in_terminal)
{
    int err;

    assert(!*in_terminal && "knd_parse_field_value is called for terminal value");

    if (spec->validate) {
        err = spec->validate(spec->obj,
                             (const char*)spec->buf, *spec->buf_size,
                             rec, total_size);
        if (err) {
            knd_log("-- ERR: %d validation of spec \"%.*s\" failed :(",
                    err, spec->name_size, spec->name);
            return err;
        }

        spec->is_completed = true;
        return knd_OK;
    }

    if (spec->parse) {
        if (DEBUG_PARSER_LEVEL_2)
            knd_log("\n    >>> further parsing required in \"%.*s\" FROM: \"%s\" FUNC: %p",
                    spec->name_size, spec->name, rec, spec->parse);

        err = spec->parse(spec->obj, rec, total_size);
        if (err) {
            knd_log("-- ERR: %d parsing of spec \"%.*s\" failed :(",
                    err, spec->name_size, spec->name);
            return err;
        }

        spec->is_completed = true;
        return knd_OK;
    }

    if (DEBUG_PARSER_LEVEL_2)
        knd_log("== ATOMIC SPEC found: %.*s! no further parsing is required.",
                spec->name_size, spec->name);

    *in_terminal = true;
    // *total_size = 0;  // This actully isn't used.
    return knd_OK;
}

static int
knd_check_field_terminal_value(const char *val,
                               size_t val_size,
                               struct kndTaskSpec *spec,
                               struct kndTaskArg *args,
                               size_t *num_args)
{
    int err;

    if (!val_size) {
        knd_log("-- empty value :(");
        return knd_FORMAT;
    }
    if (val_size > KND_NAME_SIZE) {
        knd_log("-- value too large: %zu bytes: \"%.*s\"",
                val_size, val_size, val);
        return knd_LIMIT;
    }

    if (DEBUG_PARSER_LEVEL_2)
        knd_log("++ got terminal val: \"%.*s\" [%zu]",
                val_size, val, val_size);

    assert(spec->parse == NULL && spec->validate == NULL && "spec for terminal val has .parse or .validate");

    if (spec->buf) {
        err = knd_spec_buf_copy(spec, val, val_size);
        if (err) return err;

        spec->is_completed = true;
        return knd_OK;
    }

    // FIXME(ki.stfu): ?? valid case
    // FIXME(ki.stfu): ?? push to args only if spec->run != NULL
    err = knd_args_push_back(spec->name, spec->name_size, val, val_size, args, num_args);
    if (err) return err;

    err = spec->run(spec->obj, args, *num_args);
    if (err) {
        knd_log("-- \"%.*s\" func run failed: %d :(",
                spec->name_size, spec->name, err);
        return err;
    }

    spec->is_completed = true;
    return knd_OK;
}

static int
knd_check_default(const char *rec,
                  knd_task_spec_type type,
                  struct kndTaskSpec *specs,
                  size_t num_specs) {
    struct kndTaskSpec *spec;
    struct kndTaskSpec *default_spec = NULL;
    int err;

    for (size_t i = 0; i < num_specs; ++i) {
        spec = &specs[i];

        if (spec->is_default && spec->type == type) {
            assert(default_spec == NULL && "default_spec was already specified");
            default_spec = spec;
            continue;
        }

        if (!spec->is_selector && spec->is_completed)
            return knd_OK;
    }

    if (!default_spec) {
        knd_log("-- no default spec found to handle an empty field (ignoring selectors): %.*s",
                16, rec);
        return knd_FORMAT;
    }

    err = default_spec->run(default_spec->obj, NULL, 0);
    if (err) {
        knd_log("-- default func run failed: %d :(",
                err);
        return err;
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
    
    struct kndTaskSpec *spec;

    struct kndTaskArg args[KND_MAX_ARGS];
    size_t num_args = 0;

    bool in_field = false;
    bool in_implied_field = false;
    bool in_tag = false;
    bool in_terminal = false;

    size_t chunk_size;
    int err;

    c = rec;
    b = rec;
    e = rec;

    if (DEBUG_PARSER_LEVEL_2)
        knd_log("\n\n*** start basic PARSING: \"%.*s\" num specs: %zu [%p]",
                16, rec, num_specs, specs);

    // Check kndTaskSpec is properly filled
    for (size_t i = 0; i < num_specs; i++)
        assert(knd_spec_is_correct(&specs[i]));

    while (*c) {
        switch (*c) {
        case '-':
            if (!in_field) {
                e = c + 1;
                break;
            }
            if (in_tag) {
                e = c + 1;
                break;
            }
            /* comment out this region */
            err = knd_parse_matching_braces(c, 1, &chunk_size);
            if (err) return err;
            c += chunk_size;
            in_field = false;
            b = c;
            e = b;
            break;
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            if (!in_field)
                break;
            if (in_terminal)
                break;

            if (DEBUG_PARSER_LEVEL_2)
                knd_log("+ whitespace in basic PARSING!");

            // Parse a tag after a first space.  Means in_tag can be set to true.

            err = knd_check_field_tag(b, e - b, KND_GET_STATE, specs, num_specs, &spec);
            if (err) return err;

            err = knd_parse_field_value(spec, c, &chunk_size, &in_terminal);
            if (err) return err;

            if (in_terminal) {
                // Parse an atomic value.  Remember that we are in in_tag state.
                // in_field == true
                in_tag = true;
                // in_terminal == true
                b = c + 1;
                e = b;
                break;
            }

            in_field = false;
            // in_tag == false
            // in_terminal == false
            c += chunk_size;
            b = c;
            e = b;
            break;
        case '{':
            /* starting brace '{' */
            if (!in_field) {
                if (in_implied_field) {
                    err = knd_check_implied_field(b, e - b, specs, num_specs, args, &num_args);
                    if (err) return err;

                    in_implied_field = false;
                }

                in_field = true;
                // in_tag == false
                // in_terminal == false
                b = c + 1;
                e = b;
                break;
            }

            assert(in_tag == in_terminal);

            if (in_terminal) {  // or in_tag
                knd_log("-- terminal val for ATOMIC SPEC \"%.*s\" has an opening brace '{': %.*s",
                        spec->name_size, spec->name, c - b + 16, b);
                return knd_FORMAT;
            }

            // Parse a tag after an inner field brace '{'.  Means in_tag can be set to true.

            err = knd_check_field_tag(b, e - b, KND_GET_STATE, specs, num_specs, &spec);
            if (err) return err;

            err = knd_parse_field_value(spec, c, &chunk_size, &in_terminal);
            if (err) return err;

            if (in_terminal) {
                knd_log("-- terminal val for ATOMIC SPEC \"%.*s\" starts with an opening brace '{': %.*s",
                        spec->name_size, spec->name, c - b + 16, b);
                return knd_FORMAT;
            }

            in_field = false;
            // in_tag == false
            // in_terminal == false
            c += chunk_size;
            b = c;
            e = b;
            break;
        case '}':
            /* empty field? */
            if (!in_field) {
                if (in_implied_field) {
                    err = knd_check_implied_field(b, e - b, specs, num_specs, args, &num_args);
                    if (err) return err;

                    in_implied_field = false;
                }

                err = knd_check_default(rec, KND_GET_STATE, specs, num_specs);
                if (err) return err;

                *total_size = c - rec;
                return knd_OK;
            }

            assert(in_tag == in_terminal);

            if (in_terminal) {
                err = knd_check_field_terminal_value(b, e - b, spec, args, &num_args);
                if (err) return err;

                in_field = false;
                in_tag = false;
                in_terminal = false;
                b = c + 1;
                e = b;
                break;
            }

            // Parse a tag after a closing brace '}' in an inner field.  Means in_tag can be set to true.

            err = knd_check_field_tag(b, e - b, KND_GET_STATE, specs, num_specs, &spec);
            if (err) return err;

            err = knd_parse_field_value(spec, c, &chunk_size, &in_terminal);  // TODO(ki.stfu): allow in_terminal parsing
            if (err) return err;

            if (in_terminal) {
                knd_log("-- empty terminal val for ATOMIC SPEC \"%.*s\": %.*s",
                        spec->name_size, spec->name, c - b + 16, b);
                return knd_FORMAT;
            }

            in_field = false;
            // in_tag == false
            // in_terminal == false
            break;
        case '(':
            if (DEBUG_PARSER_LEVEL_2)
                knd_log(".. basic LOOP %p detected the state change area: \"%s\"\n", specs, c);

            /* starting brace '(' */
            if (!in_field) {
                if (in_implied_field) {
                    err = knd_check_implied_field(b, e - b, specs, num_specs, args, &num_args);
                    if (err) return err;

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

                // in_field = false
                // in_tag == false
                // in_terminal == false
                b = c + 1;
                e = b;
                break;
            }

            assert(in_tag == in_terminal);

            if (in_terminal) {  // or in_tag
                knd_log("-- terminal val for ATOMIC SPEC \"%.*s\" has an opening brace '(': %.*s",
                        spec->name_size, spec->name, c - b + 16, b);
                return knd_FORMAT;
            }

            // Parse a tag after an inner field brace '('.  Means in_tag can be set to true.

            err = knd_check_field_tag(b, e - b, KND_CHANGE_STATE, specs, num_specs, &spec);
            if (err) return err;

            err = knd_parse_field_value(spec, c, &chunk_size, &in_terminal);
            if (err) return err;

            if (in_terminal) {
                knd_log("-- terminal val for ATOMIC SPEC \"%.*s\" starts with an opening brace '(': %.*s",
                        spec->name_size, spec->name, c - b + 16, b);
                return knd_FORMAT;
            }

            in_field = false;
            // in_tag == false
            // in_terminal == false
            c += chunk_size;
            b = c;
            e = b;
            break;
        case ')':
            if (DEBUG_PARSER_LEVEL_2)
                knd_log("\n\n-- END OF BASIC LOOP [%p] STARTED at: \"%.*s\" FINISHED at: \"%.*s\"\n\n",
                        specs, 16, rec, 16, c);

            if (!in_field) {
                if (in_implied_field) {
                    err = knd_check_implied_field(b, e - b, specs, num_specs, args, &num_args);
                    if (err) return err;
                    in_implied_field = false;
                    *total_size = c - rec;
                    return knd_OK;
                }

                /* any default action to take? */
                if (!spec) {
                    err = knd_check_default(rec, KND_CHANGE_STATE, specs, num_specs);
                    if (err) return err;
                }
            }
            
            *total_size = c - rec;
            return knd_OK;
        case '[':
            /* starting brace */
            if (!in_field) {
                if (in_implied_field) {
                    name_size = e - b;
                    if (name_size) {
                        err = knd_check_implied_field(b, name_size,
                                                      specs, num_specs,
                                                      args, &num_args);
                        if (err) return err;
                    }
                    in_implied_field = false;
                }
            }
            else {
                err = check_name_limits(b, e, &name_size);
                if (err) return err;

                if (DEBUG_PARSER_LEVEL_2)
                    knd_log("++ BASIC LOOP got tag before square bracket: \"%.*s\" [%zu]",
                            name_size, b, name_size);
                  
                err = knd_find_spec(b, name_size, KND_GET_STATE, specs, num_specs, &spec);
                if (err) {
                    knd_log("-- no spec found to handle the \"%.*s\" tag: %d",
                            name_size, b, err);
                    return err;
                }

                if (spec->validate) {
                    err = spec->validate(spec->obj,
                                         (const char*)spec->buf, *spec->buf_size,
                                         c, &chunk_size);
                    if (err) {
                        knd_log("-- ERR: %d validation of spec \"%s\" failed :(",
                                err, spec->name);
                        return err;
                    }
                }
                c += chunk_size;
                spec->is_completed = true;
                in_field = false;
                b = c;
                e = b;
                break;
            }
            
            err = knd_parse_list(c, &chunk_size, specs, num_specs);
            if (err) {
                knd_log("-- basic LOOP failed to parse the list area \"%.*s\" :(", 32, c);
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
        knd_log("\n\n--- end of basic PARSING: \"%s\" num specs: %zu [%p]",
                rec, num_specs, specs);
    
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
    bool in_terminal = false;

    size_t chunk_size;
    int err;

    c = rec;
    b = rec;
    e = rec;

    if (DEBUG_PARSER_LEVEL_2)
        knd_log("\n\n == parsing the state change area: \"%.*s\" num specs: %lu [%p]",
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
                knd_log("++ state change loop got tag: \"%.*s\" [%lu]",
                        name_size, b, (unsigned long)name_size);

            err = knd_find_spec(b, name_size, KND_CHANGE_STATE, specs, num_specs, &spec);
            if (err) {
                knd_log("-- no spec found to handle the \"%.*s\" change state tag in \"%.*s\" :(",
                        name_size, b, 32, c);
                return err;
            }

            if (DEBUG_PARSER_LEVEL_2)
                knd_log("++ got SPEC: \"%.*s\" default: %d  terminal: %d",
                        spec->name_size, spec->name, spec->is_default, spec->is_terminal);

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

                spec->is_completed = true;

                if (DEBUG_PARSER_LEVEL_2)
                    knd_log("== END func parsing spec \"%s\" [%p] at: \"%s\"\n\n",
                            spec->name, specs, c);
                break;
            }

            in_terminal = true; // OK?
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
            if (DEBUG_PARSER_LEVEL_2) {
                if (spec) {
                    knd_log("== END parse state change: \"%.*s\" [%.*s]",
                            16, c, spec->name_size, spec->name);
                }
            }

            if (!spec) {
                /* activate spec search */
                err = check_name_limits(b, e, &name_size);
                if (err) return err;

                if (DEBUG_PARSER_LEVEL_2)
                    knd_log("++ FUNC LOOP got tag in close bracket: \"%.*s\" [%lu]",
                            name_size, b, (unsigned long)name_size);

                err = knd_find_spec(b, name_size, KND_CHANGE_STATE, specs, num_specs, &spec);
                if (err) {
                    knd_log("-- no spec found to handle the \"%.*s\" change state tag, rec:  \"%.*s\" :(",
                            name_size, b, 16, c);
                    return err;
                }

                if (DEBUG_PARSER_LEVEL_TMP)
                    knd_log("++ got SPEC: \"%s\"  default: %d  terminal: %d",
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
            if (in_terminal) {
                err = check_name_limits(b, e, &name_size);
                if (err) {
                    knd_log("-- name limit reached :(");
                    return err;
                }
                if (DEBUG_PARSER_LEVEL_2)
                    knd_log("++ got state change terminal: \"%.*s\" [%lu]",
                            name_size, b, (unsigned long)name_size);

                err = knd_spec_buf_copy(spec, b, name_size);
                if (err) return err;
                spec->is_completed = true;

                *total_size = c - rec;
                return knd_OK;
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
            if (!in_item) break;

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
                    knd_log("  == list got new item: \"%.*s\"",
                            name_size, b);
                err = alloc_item(accu, b, name_size, item_count, &item);
                if (err) {
                    knd_log("-- item alloc failed: %d :(", err);
                    return err;
                }
                item_count++;

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

            if (DEBUG_PARSER_LEVEL_2)
                knd_log("++ list got tag: \"%.*s\" [%lu]",
                        name_size, b, (unsigned long)name_size);

            err = knd_find_spec(b, name_size, KND_GET_STATE, specs, num_specs, &spec);
            if (err) {
                knd_log("-- no spec found to handle the \"%.*s\" list tag :(",
                        name_size, b);
                return err;
            }

            if (DEBUG_PARSER_LEVEL_2)
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

                err = knd_find_spec(b, name_size, KND_GET_STATE, specs, num_specs, &spec);
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

            /* list requires a tag and some items */
            if (!got_tag) return knd_FAIL;

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
