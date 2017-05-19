#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_spec.h"
#include "knd_output.h"
#include "knd_utils.h"

#define DEBUG_SPEC_LEVEL_0 0
#define DEBUG_SPEC_LEVEL_1 0
#define DEBUG_SPEC_LEVEL_2 0
#define DEBUG_SPEC_LEVEL_3 0
#define DEBUG_SPEC_LEVEL_TMP 1

static void
kndSpec_del(struct kndSpec *self)
{
    free(self);
}

static void
kndSpec_str(struct kndSpec *self __attribute__((unused)), size_t depth __attribute__((unused)))
{
    
}

static void
kndSpec_reset(struct kndSpec *self)
{
    self->sid_size = 0;
    self->uid_size = 0;
    self->tid_size = 0;
    self->num_instructions = 0;
}



static int
kndSpec_parse_repo(struct kndSpec *self,
                   char *rec,
                   size_t *total_size)
{
    struct kndSpecInstruction *instruct;
    struct kndSpecArg *arg;
    
    char *b, *c;
    size_t buf_size;
    
    bool in_proc = false;
    bool in_arg = false;
    bool in_val = false;

    c = rec;
    b = c;

    if (DEBUG_SPEC_LEVEL_2)
        knd_log("   .. parsing REPO rec: \"%s\"", c);

    instruct = &self->instructions[self->num_instructions];
    memset(instruct, 0, sizeof(struct kndSpecInstruction));

    instruct->type = KND_AGENT_REPO;
    instruct->spec = self;
    
    while (*c) {
        switch (*c) {
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            if (!in_proc) {
                b = c + 1;
                break;
            }
            
            buf_size = c - b;
            if (!buf_size) {
                knd_log("-- empty tag");
                return knd_FAIL;
            }

            if (in_val) break;

            if (buf_size >= KND_NAME_SIZE) return knd_LIMIT;
            
            arg = &instruct->args[instruct->num_args];
            memset(arg, 0, sizeof(struct kndSpecArg));

            memcpy(arg->name, b, buf_size);
            arg->name[buf_size] = '\0';
            
            
            b = c + 1;
            in_val = true;
            
            break;
        case '{':
            if (!in_proc) {
                in_proc = true;
                b = c + 1;
                break;
            }

            if (!in_arg) {
                buf_size = c - b;
                if (buf_size >= KND_NAME_SIZE) return knd_LIMIT;
                
                if (DEBUG_SPEC_LEVEL_2) {
                    *c = '\0';
                    knd_log("PROC: \"%s\"", b);
                }
                
                memcpy(instruct->proc_name, b, buf_size);
                instruct->proc_name[buf_size] = '\0';
                
                in_arg = true;
                b = c + 1;
                break;
            }

            break;
        case '}':
            /**c = '\0'; */
            if (in_arg) {
                buf_size = c - b;
                if (!buf_size) return knd_FAIL;
                if (buf_size >= KND_NAME_SIZE) return knd_LIMIT;

                memcpy(arg->val, b, buf_size);
                arg->val[buf_size] = '\0';
                arg->val_size = buf_size;
                
                /* valid arg added */
                instruct->num_args++;
                
                in_arg = false;
                break;
            }

            if (in_proc) {
                in_proc = false;
                break;
            }

            self->num_instructions++;
            
            *total_size = c - rec;
            return knd_OK;
        default:
            break;
        }

        c++;
    }

    
    return knd_FAIL;
}


static int
kndSpec_parse_user(struct kndSpec *self,
                   char *rec,
                   size_t *total_size)
{
    struct kndSpecInstruction *instruct;
    struct kndSpecArg *arg;
    
    char *b, *c;
    size_t buf_size;
    
    bool in_proc = false;
    bool in_arg = false;
    bool in_val = false;

    c = rec;
    b = c;

    if (DEBUG_SPEC_LEVEL_2)
        knd_log("   .. parsing USER rec: \"%s\"", c);

    instruct = &self->instructions[self->num_instructions];
    memset(instruct, 0, sizeof(struct kndSpecInstruction));

    instruct->type = KND_AGENT_USER;
    
    while (*c) {
        switch (*c) {
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            if (!in_proc) {
                b = c + 1;
                break;
            }
            
            buf_size = c - b;
            if (!buf_size) {
                knd_log("-- empty tag");
                return knd_FAIL;
            }

            if (in_val) break;

            if (buf_size >= KND_NAME_SIZE) return knd_LIMIT;
            
            arg = &instruct->args[instruct->num_args];
            memset(arg, 0, sizeof(struct kndSpecArg));

            memcpy(arg->name, b, buf_size);
            arg->name[buf_size] = '\0';

            b = c + 1;
            in_val = true;
            
            break;
        case '{':
            if (!in_proc) {
                in_proc = true;
                b = c + 1;
                break;
            }

            if (!in_arg) {
                buf_size = c - b;
                if (buf_size >= KND_NAME_SIZE) return knd_LIMIT;

                memcpy(instruct->proc_name, b, buf_size);
                instruct->proc_name[buf_size] = '\0';
                
                in_arg = true;
                b = c + 1;
                break;
            }

            break;
        case '}':
            if (in_arg) {
                buf_size = c - b;
                if (!buf_size) return knd_FAIL;
                if (buf_size >= KND_NAME_SIZE) return knd_LIMIT;

                memcpy(arg->val, b, buf_size);
                arg->val[buf_size] = '\0';
                arg->val_size = buf_size;
                
                /* valid arg added */
                instruct->num_args++;
                
                in_arg = false;
                break;
            }

            if (in_proc) {
                in_proc = false;
                break;
            }

            self->num_instructions++;
            
            *total_size = c - rec;
            return knd_OK;
        default:
            break;
        }

        c++;
    }
    
    return knd_FAIL;
}



static int
kndSpec_parse_auth(struct kndSpec *self,
                   char *rec,
                   size_t *total_size)
{
    const char *sid_tag = "sid";
    size_t sid_tag_size = strlen(sid_tag);

    const char *uid_tag = "uid";
    size_t uid_tag_size = strlen(uid_tag);
    
    const char *tid_tag = "tid";
    size_t tid_tag_size = strlen(tid_tag);
    
    size_t buf_size;
    
    bool in_field = false;
    bool in_sid = false;
    bool in_uid = false;
    bool in_tid = false;
    
    char *c;
    const char *b;

    c = rec;
    b = c;
    
    if (DEBUG_SPEC_LEVEL_2)
        knd_log("   .. parsing AUTH rec: \"%s\"",
                c);
    
    while (*c) {
        switch (*c) {
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            buf_size = c - b;
            if (!buf_size) {
                knd_log("-- empty tag");
                return knd_FAIL;
            }
            
            if (!strncmp(b, uid_tag, uid_tag_size)){
                in_uid = true;
                b = c + 1;
                break;
            }

            if (!strncmp(b, sid_tag, sid_tag_size)){
                in_sid = true;
                b = c + 1;
                break;
            }

            if (!strncmp(b, tid_tag, tid_tag_size)){
                in_tid = true;
                b = c + 1;
                break;
            }
            
            /*if (in_sid || in_tid || in_uid ) {
                b = c + 1;
                break;
                }*/

            break;
        case '{':
            if (!in_field) {
                in_field = true;
                b = c + 1;
                break;
            }

            break;
        case '}':
            if (in_sid) {
                buf_size = c - b;
                if (buf_size >= KND_NAME_SIZE) return knd_LIMIT;
                memcpy(self->sid, b, buf_size);
                self->sid_size = buf_size;
                self->sid[buf_size] = '\0';
                
                in_sid = false;
                in_field = false;
                break;
            }
            
            if (in_uid) {
                buf_size = c - b;
                if (buf_size >= KND_NAME_SIZE) return knd_LIMIT;
                memcpy(self->uid, b, buf_size);
                self->uid_size = buf_size;
                self->uid[buf_size] = '\0';

                in_uid = false;
                in_field = false;
                b = c + 1;
                break;
            }

            if (in_tid) {
                buf_size = c - b;
                if (buf_size >= KND_NAME_SIZE) return knd_LIMIT;
                memcpy(self->tid, b, buf_size);
                self->tid_size = buf_size;
                self->tid[buf_size] = '\0';

                in_tid = false;
                in_field = false;
                break;
            }
            
            *total_size = c - rec;
            return knd_OK;
        default:
            /* non-whitespace */
            
            break;
        }

        c++;
    }

    return knd_FAIL;
}



static int
kndSpec_parse_domain(struct kndSpec *self,
                     const char *name,
                     size_t name_size,
                     char *rec,
                     size_t *total_size)
{
    char *c, *b;
    size_t chunk_size;

    const char *auth_tag = "AUTH";
    const char *repo_tag = "Repo";
    const char *user_tag = "User";

    int err;
    
    b = rec;
    c = rec;
    
    switch (*name) {
    case 'a':
    case 'A':
        if (!strncmp(auth_tag, name, name_size)){
            err = kndSpec_parse_auth(self, c, &chunk_size);
            if (err) {
                knd_log("-- AUTH parse failed");
                return knd_FAIL;
            }
            *total_size = chunk_size;
            return knd_OK;
        }
        break;
    case 'r':
    case 'R':
        if (!strncmp(repo_tag, name, name_size)){
            err = kndSpec_parse_repo(self, c, &chunk_size);
            if (err) {
                knd_log("-- Repo parse failed");
                return knd_FAIL;
            }
            *total_size = chunk_size;
            return knd_OK;
        }
        break;
    case 'u':
    case 'U':
        if (!strncmp(user_tag, name, name_size)){
            err = kndSpec_parse_user(self, c, &chunk_size);
            if (err) {
                knd_log("-- User parse failed");
                return knd_FAIL;
            }
            *total_size = chunk_size;
            return knd_OK;
        }
        break;
    default:
        break;
    }
    
    return knd_FAIL;
}

static int
kndSpec_parse(struct kndSpec *self,
              char *rec,
              size_t *total_size)
{
    const char *header_tag = "SPEC";
    size_t header_tag_size = strlen(header_tag);
    
    size_t buf_size;
    
    bool in_body = false;
    bool in_header = false;
    bool in_field = false;
    
    char *c;
    char *b;
    size_t chunk_size;
    int err = knd_FAIL;
    
    c = rec;
    b = c;
    
    if (DEBUG_SPEC_LEVEL_2)
        knd_log("   .. parsing SPEC rec \"%s\"",
                c);
    
    while (*c) {
        switch (*c) {
            /* non-whitespace char */
        default:
            if (!in_body) break;
            break;
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            /* whitespace */
            if (!in_body) break;

            break;
        case '{':
            if (!in_body) {
                in_body = true;
                b = c + 1;
                break;
            }

            if (!in_header) {
                buf_size = c - b;
                if (buf_size > strlen(header_tag)) {
                    knd_log("-- header tag too large: %lu bytes",
                    (unsigned long)buf_size);
                    return knd_LIMIT;
                }

                if (strncmp(b, header_tag, header_tag_size)){
                    knd_log("-- header tag mismatch");
                    return knd_FAIL;
                }
                in_header = true;
                in_field = true;
                b = c + 1;
                break;
            }

            if (!in_field) {
                in_field = true;
                b = c + 1;
                break;
            }
            
            buf_size = c - b;
            if (!buf_size) {
                knd_log("-- empty tag");
                return knd_FAIL;
            }

            if (buf_size >= KND_NAME_SIZE)
                return knd_LIMIT;

            err = kndSpec_parse_domain(self, b, buf_size, c, &chunk_size);
            if (err) return err;

            c += chunk_size;

            if (DEBUG_SPEC_LEVEL_2)
                knd_log("++ Domain \"%s\" parse OK: %lu bytes     REMAINDER: %s",
                        b, (unsigned long)chunk_size, c);

            in_field = false;
            b = c + 1;
            break;
        case '}':
            if (!in_body) {
                knd_log("-- right brace mismatch :(");
                return knd_FAIL;
            }

            
            *total_size = c - rec;

            if (DEBUG_SPEC_LEVEL_2)
                knd_log("++ SPEC parse OK: %lu bytes     REMAINDER: %s",
                        (unsigned long)*total_size, c);

            return knd_OK;
        case '[':
            c++;
            break;
        }
        
        c++;
    }

    return err;
}


extern int 
kndSpec_new(struct kndSpec **spec)
{
    struct kndSpec *self;
    
    self = malloc(sizeof(struct kndSpec));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndSpec));

    self->del = kndSpec_del;
    self->str = kndSpec_str;
    self->reset = kndSpec_reset;
    self->parse = kndSpec_parse;

    *spec = self;

    return knd_OK;
}
