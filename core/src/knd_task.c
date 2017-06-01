#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_task.h"
#include "knd_user.h"
#include "knd_output.h"
#include "knd_utils.h"

#define DEBUG_TASK_LEVEL_0 0
#define DEBUG_TASK_LEVEL_1 0
#define DEBUG_TASK_LEVEL_2 0
#define DEBUG_TASK_LEVEL_3 0
#define DEBUG_TASK_LEVEL_TMP 1


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


static void
kndTask_del(struct kndTask *self)
{
    free(self);
}

static void
kndTask_str(struct kndTask *self __attribute__((unused)), size_t depth __attribute__((unused)))
{
    
}

static void
kndTask_reset(struct kndTask *self)
{
    self->sid_size = 0;
    self->uid_size = 0;
    self->tid_size = 0;
}




static int
kndTask_parse_tid(struct kndTask *self,
                  const char *rec,
                  size_t *total_size)
{
    size_t buf_size;
    const char *c;
    const char *b;

    c = rec;
    b = c;
    
    if (DEBUG_TASK_LEVEL_2)
        knd_log("   .. parsing TID rec: \"%s\"",
                c);
    
    while (*c) {
        switch (*c) {
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            b = c + 1;
            break;
        case '}':
            buf_size = c - b;
            if (!buf_size) return knd_FAIL;
            if (buf_size >= KND_NAME_SIZE) return knd_LIMIT;

            memcpy(self->tid, b, buf_size);
            self->tid_size = buf_size;
            self->tid[buf_size] = '\0';

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
kndTask_parse_domain(struct kndTask *self,
                     const char *name,
                     size_t name_size,
                     const char *rec,
                     size_t *total_size)
{
    const char *tid_tag = "tid";
    const char *user_tag = "user";
    size_t chunk_size;
    int err;

    if (DEBUG_TASK_LEVEL_2)
        knd_log(".. parsing domain %s..", name);

    switch (*name) {
    case 't':
    case 'T':
        if (!strncmp(tid_tag, name, name_size)) {
            err = kndTask_parse_tid(self, rec, &chunk_size);
            if (err) {
                knd_log("-- tid parse failed");
                return knd_FAIL;
            }
            *total_size = chunk_size;
            return knd_OK;
        }
        break;
    case 'u':
    case 'U':
        if (!strncmp(user_tag, name, name_size)) {
            self->admin->task = self;
            err = self->admin->parse_task(self->admin, rec, total_size);
            if (err) {
                knd_log("-- User area parse failed");
                return knd_FAIL;
            }
            return knd_OK;
        }
        break;
    default:
        break;
    }
    
    return knd_FAIL;
}

static int
kndTask_run(struct kndTask *self,
            const char *rec,
            size_t rec_size,
            const char *obj,
            size_t obj_size)
{
    const char *header_tag = "knd::Task";
    size_t header_tag_size = strlen(header_tag);
    
    size_t buf_size;
    
    bool in_body = false;
    bool in_header = false;
    bool in_field = false;
    
    const char *b, *c, *e;
    size_t chunk_size;
    size_t total_size;
    
    int err = knd_FAIL;

    self->spec = rec;
    self->spec_size = rec_size;
    self->obj = obj;
    self->obj_size = obj_size;
    
    c = rec;
    b = rec;
    e = rec;
    
    if (DEBUG_TASK_LEVEL_2)
        knd_log("   .. parsing TASK rec \"%s\"", rec);
    
    while (*c) {
        switch (*c) {
            /* non-whitespace char */
        default:
            if (!in_body) break;

            e = c + 1;

            break;
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            /* whitespace */
            if (!in_body) break;

            if (in_field) {
                err = check_name_limits(b, c, &buf_size);
                if (err) return err;

                err = kndTask_parse_domain(self, b, buf_size, c, &chunk_size);
                if (err) return err;

                c += chunk_size;
                in_field = false;
                b = c + 1;
                break;
            }
            
            break;
        case '{':
            if (!in_body) {
                in_body = true;
                b = c + 1;
                break;
            }

            if (!in_header) {
                err = check_name_limits(b, e, &buf_size);
                if (err) return err;
                
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

            err = check_name_limits(b, c, &buf_size);
            if (err) return err;

            err = kndTask_parse_domain(self, b, buf_size, c, &chunk_size);
            if (err) return err;

            c += chunk_size;

            if (DEBUG_TASK_LEVEL_2)
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
            
            total_size = c - rec;

            if (DEBUG_TASK_LEVEL_2)
                knd_log("++ TASK parse OK: %lu bytes     REMAINDER: %s",
                        (unsigned long)total_size, c);

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
kndTask_new(struct kndTask **task)
{
    struct kndTask *self;
    
    self = malloc(sizeof(struct kndTask));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndTask));

    self->del = kndTask_del;
    self->str = kndTask_str;
    self->reset = kndTask_reset;
    self->run = kndTask_run;

    *task = self;

    return knd_OK;
}
