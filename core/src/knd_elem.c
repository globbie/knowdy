#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/stat.h>

#include "knd_concept.h"
#include "knd_repo.h"
#include "knd_elem.h"
#include "knd_attr.h"
#include "knd_object.h"

#include "knd_text.h"
#include "knd_ref.h"
#include "knd_num.h"

#include "knd_refset.h"
#include "knd_sorttag.h"

#include "knd_output.h"
#include "knd_user.h"
#include "knd_parser.h"

#define DEBUG_ELEM_LEVEL_1 0
#define DEBUG_ELEM_LEVEL_2 0
#define DEBUG_ELEM_LEVEL_3 0
#define DEBUG_ELEM_LEVEL_4 0
#define DEBUG_ELEM_LEVEL_TMP 1


static void del(struct kndElem *self)
{
    struct kndElemState *state;

    if (self->aggr)
        self->aggr->del(self->aggr);
    if (self->num)
        self->num->del(self->num);

    free(self);
}


static void str(struct kndElem *self)
{
    if (self->aggr) {
        if (self->is_list) {
            knd_log("%*s[%.*s\n",
                    self->depth * KND_OFFSET_SIZE, "", self->attr->name_size, self->attr->name);
            struct kndObject *obj = self->aggr;
            while (obj) {
                obj->depth = self->depth + 1;
                obj->str(obj);
                obj = obj->next;
            }
            knd_log("%*s]\n",
                    self->depth * KND_OFFSET_SIZE, "");
            return;
        }

        knd_log("%*s%.*s:",
                self->depth * KND_OFFSET_SIZE, "", self->attr->name_size, self->attr->name);
        self->aggr->depth = self->depth + 1;
        self->aggr->str(self->aggr);
        return;
    }

    switch (self->attr->type) {
    case KND_ATTR_REF:
        self->ref->depth = self->depth + 1;
        self->ref->str(self->ref);
        return;
    case KND_ATTR_NUM:
        self->num->depth = self->depth + 1;
        self->num->str(self->num);
        return;
    case KND_ATTR_TEXT:
        self->text->depth = self->depth + 1;
        self->text->str(self->text);
        return;
    default:
        break;
    }

    knd_log("%*s%s => %s", self->depth * KND_OFFSET_SIZE, "",
            self->attr->name, self->states->val);
}


static int
kndElem_export_JSON(struct kndElem *self)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    struct kndObject *obj;
    struct kndText *text;
    struct kndRef *ref;

    struct kndOutput *out = self->out;
    size_t curr_size;
    //unsigned long numval;
    int err;

    if (self->aggr) {
        if (self->is_list) {
            buf_size = sprintf(buf, "\"%s_l\":[",
                               self->states->val);
            err = out->write(out, buf, buf_size);
            if (err) return err;

            obj = self->aggr;
            while (obj) {
                obj->out = out;
                err = obj->export(obj);

                if (obj->next) {
                    err = out->write(out, ",", 1);
                    if (err) return err;
                }

                obj = obj->next;
            }

            err = out->write(out, "]", 1);
            if (err) return err;

            return knd_OK;
        }

        /* single anonymous aggr obj */
        err = out->write(out, "\"", 1);
        if (err) goto final;
        err = out->write(out, self->attr->name, self->attr->name_size);
        if (err) goto final;
        err = out->write(out, "\":", strlen("\":"));
        if (err) goto final;
        
        self->aggr->out = out;
        err = self->aggr->export(self->aggr);
        
        return err;
    }

    /* attr name */
    err = out->write(out, "\"", 1);
    if (err) goto final;
    err = out->write(out, self->attr->name, self->attr->name_size);
    if (err) goto final;
    err = out->write(out, "\":", strlen("\":"));
    if (err) goto final;

    /* key:value repr */
    switch (self->attr->type) {
    case KND_ATTR_NUM:
        err = out->write(out, self->num->states->val, self->num->states->val_size);
        if (err) goto final;
        return knd_OK;
    case KND_ATTR_STR:
    case KND_ATTR_BIN:
        err = out->write(out, "\"", 1);
        if (err) goto final;
        err = out->write(out, self->states->val, self->states->val_size);
        if (err) goto final;
        err = out->write(out, "\"", 1);
        if (err) goto final;
        return knd_OK;
    default:
        break;
    }

    /* nested repr */
    err = out->write(out, "{", 1);
    if (err) goto final;

    curr_size = out->buf_size;

    if (self->attr) {
        switch (self->attr->type) {
        case  KND_ATTR_TEXT:
            text = self->text;
            text->out = out;
            err = text->export(text);
            if (err) goto final;
            break;
            /*case KND_ATTR_FILE:
            err = out->write(out, "\"file\":\"", strlen("\"file\":\""));
            if (err) goto final;
            err = out->write(out,
                                   self->states->val, self->states->val_size);
            if (err) goto final;
            err = out->write(out, "\"", 1);
            if (err) goto final;
            */
            /* soft link the actual file */
            /* TODO: remove reader
                               buf_size = sprintf(buf, "%s/%s",
                               self->obj->cache->repo->user->reader->webpath,
                               self->states->val); */

            /*dirname_size = sprintf(dirname,
                                   "%s/%s", self->obj->cache->repo->path,
                                   self->obj->cache->baseclass->name);
            knd_make_id_path(pathbuf,
                             dirname,
                             self->obj->id, self->states->val);
            
            if (DEBUG_ELEM_LEVEL_2)
                knd_log("SOFT LINK: %s -> %s\n", buf, pathbuf);

            err = lstat(buf, &linkstat);
            if (err) {
                err = symlink((const char*)pathbuf, (const char*)buf);
                if (err) {
                    if (DEBUG_ELEM_LEVEL_TMP)
                        knd_log("  -- soft link failed: %d :(\n", err);
                    return err;
                }
            }
            
            if (self->states) {
                buf_size = sprintf(buf, ",\"_st\":%lu",
                                   (unsigned long)self->states->state);
                err = out->write(out, buf, buf_size);
                if (err) goto final;
            }
            
            break; */
        case KND_ATTR_REF:
            ref = self->ref;
            ref->out = out;
            err = ref->export(ref);
            if (err) goto final;
            break;
        case KND_ATTR_CALC:
            if (DEBUG_ELEM_LEVEL_TMP)
                knd_log("  .. CALC elem!\n");

            buf_size = sprintf(buf, "\"val\":\"%s\"",
                               self->states->val);
            err = out->write(out, buf, buf_size);
            if (err) goto final;
            break;
        default:
            break;
        }
    }
    else {
        if (self->states) {
            buf_size = sprintf(buf, "\"val\":\"%s\"",
                               self->states->val);
            err = out->write(out, buf, buf_size);
            if (err) goto final;
        }
    }

final:

    return err;
}

static int
kndElem_export_GSP(struct kndElem *self)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    struct kndObject *obj;
    struct kndText *text;
    struct kndRef *ref;
    struct kndOutput *out = self->out;
    size_t curr_size;
    int err;

    if (DEBUG_ELEM_LEVEL_2)
        knd_log("  .. GSP export of %.*s elem.. ",
                self->attr->name_size, self->attr->name);

    if (self->aggr) {
        /* single anonymous aggr obj */
        err = out->write(out, "{", 1);
        if (err) return err;
        err = out->write(out, self->attr->name, self->attr->name_size);
        if (err) return err;
        
        self->aggr->out = out;
        self->aggr->format = KND_FORMAT_GSP;
        err = self->aggr->export(self->aggr);

        err = out->write(out, "}", 1);
        if (err) return err;
        return err;
    }

    err = out->write(out, "{", 1);
    if (err) return err;
    err = out->write(out, self->attr->name, self->attr->name_size);
    if (err) return err;
    err = out->write(out, " ", 1);
    if (err) return err;

    /* key:value repr */
    switch (self->attr->type) {
    case KND_ATTR_NUM:
        //self->obj->str(self->obj);
        err = out->write(out, self->num->states->val, self->num->states->val_size);
        if (err) return err;
        break;
    case KND_ATTR_STR:
    case KND_ATTR_BIN:
        err = out->write(out, self->states->val, self->states->val_size);
        if (err) return err;
        break;
    case  KND_ATTR_TEXT:
        text = self->text;
        text->out = out;
        text->format = KND_FORMAT_GSP;
        err = text->export(text);
        if (err) return err;
        break;
    case KND_ATTR_REF:
        ref = self->ref;
        ref->out = out;
        ref->format = KND_FORMAT_GSP;
        err = ref->export(ref);
        if (err) return err;
        break;
    default:
        break;
    }

    err = out->write(out, "}", 1);
    if (err) return err;

    return knd_OK;
}

static int 
kndElem_export(struct kndElem *self)
{
    int err;
    bool is_concise = 0;

    switch(self->format) {
    case KND_FORMAT_JSON:
        err = kndElem_export_JSON(self);
        if (err) return err;
        break;
        /*case KND_FORMAT_HTML:
        err = kndElem_export_HTML(self, is_concise);
        if (err) return err;
        break;*/
        /*case KND_FORMAT_GSL:
        err = kndElem_export_GSL(self);
        if (err) return err;
        break;
        */
    case KND_FORMAT_GSP:
        err = kndElem_export_GSP(self);
        if (err) return err;
        break;
    default:
        break;
    }
    
    return knd_OK;
}

static int run_empty_val_warning(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndElem *self = (struct kndElem*)obj;
    knd_log("-- empty val of \"%.*s\" not accepted :(",
            self->attr->name_size, self->attr->name);
    return knd_FAIL;
}

static int run_set_val(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndElem *self = (struct kndElem*)obj;
    struct kndTaskArg *arg;
    struct kndElemState *state;
    const char *val = NULL;
    size_t val_size = 0;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "_impl", strlen("_impl"))) {
            val = arg->val_ref;
            val_size = arg->val_size;
        }
    }

    if (DEBUG_ELEM_LEVEL_2)
        knd_log(".. %.*s to set val \"%.*s\"",
                self->attr->name_size, self->attr->name,
                val_size, val);

    if (!val_size) return knd_FAIL;
    if (val_size >= KND_VAL_SIZE) return knd_LIMIT;

    state = malloc(sizeof(struct kndElemState));
    if (!state) return knd_NOMEM;
    memset(state, 0, sizeof(struct kndElemState));
    self->states = state;
    self->num_states = 1;

    state->val = val;
    state->val_size = val_size;

    /* TODO: validate if needed */
    /*switch (self->attr->type) {
    case KND_ATTR_NUM:
        knd_log(".. validate ELEM NUM val of class %.*s: \"%.*s\"",
                self->attr->name_size, self->attr->name, val_size, val);
        break;
    case KND_ATTR_BIN:
        knd_log("++ ELEM BIN val of class %.*s: \"%.*s\"",
                self->attr->name_size, self->attr->name, val_size, val);
        break;
    default:
        break;
        } */

    if (DEBUG_ELEM_LEVEL_2)
        knd_log("++ ELEM VAL: \"%.*s\"", state->val_size, state->val);

    return knd_OK;
}

static int parse_GSL(struct kndElem *self,
                     const char *rec,
                     size_t *total_size)
{
    int err;

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_val,
          .obj = self
        },
        { .type = KND_CHANGE_STATE,
          .name = "default",
          .name_size = strlen("default"),
          .is_default = true,
          .run = run_empty_val_warning,
          .obj = self
        }
    };

    if (DEBUG_ELEM_LEVEL_1)
        knd_log(".. ELEM \"%.*s\" parse REC: \"%.*s\"",
                self->attr->name_size, self->attr->name,
                16, rec);

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;
    
    return knd_OK;
}

static int
kndElem_resolve(struct kndElem *self)
{
    struct kndObject *obj;
    int err;
    
    if (self->aggr) {
        for (obj = self->aggr; obj; obj = obj->next) {
            obj->log = self->log;
            err = obj->resolve(obj);
            if (err) return err;
        }
    }

    switch (self->attr->type) {
    case KND_ATTR_REF:
        self->ref->log = self->log;
        err = self->ref->resolve(self->ref);
        if (err) return err;
    default:
        break;
    }
    
    return knd_OK;
}


extern void
kndElem_init(struct kndElem *self)
{
    self->del = del;
    self->str = str;
    self->parse = parse_GSL;

    self->resolve = kndElem_resolve;
    //self->index = kndElem_index;
    self->export = kndElem_export;
}

extern int
kndElem_new(struct kndElem **obj)
{
    struct kndElem *self;

    self = malloc(sizeof(struct kndElem));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndElem));

    kndElem_init(self);

    *obj = self;

    return knd_OK;
}
