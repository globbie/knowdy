#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/stat.h>

#include <gsl-parser.h>

#include "knd_class_inst.h"
#include "knd_class.h"
#include "knd_repo.h"
#include "knd_attr_inst.h"
#include "knd_attr.h"
#include "knd_state.h"
#include "knd_mempool.h"
#include "knd_output.h"

#include "knd_text.h"
#include "knd_ref.h"
#include "knd_num.h"

#include "knd_user.h"

#define DEBUG_ATTR_INST_LEVEL_1 0
#define DEBUG_ATTR_INST_LEVEL_2 0
#define DEBUG_ATTR_INST_LEVEL_3 0
#define DEBUG_ATTR_INST_LEVEL_4 0
#define DEBUG_ATTR_INST_LEVEL_TMP 1

struct LocalContext {
    struct kndAttrInst *attr_inst;
    struct kndClass *class;
    struct kndTask *task;
};

void knd_attr_inst_str(struct kndAttrInst *self, size_t depth)
{
    struct kndState *state = self->states;

    if (self->inner) {
        if (self->is_list) {
            knd_log("%*s[%.*s\n",
                    depth * KND_OFFSET_SIZE, "",
                    self->attr->name_size, self->attr->name);
            struct kndClassInst *obj = self->inner;
            while (obj) {
                knd_class_inst_str(obj, 0);
                obj = obj->next;
            }
            knd_log("%*s]\n",
                    depth * KND_OFFSET_SIZE, "");
            return;
        }

        knd_log("%*s%.*s:",
                depth * KND_OFFSET_SIZE, "", self->attr->name_size, self->attr->name);
        knd_class_inst_str(self->inner, 0);
        return;
    }

    switch (self->attr->type) {
    case KND_ATTR_REF:
        if (self->ref_inst)
            knd_class_inst_str(self->ref_inst, 0);
        return;
        /*case KND_ATTR_NUM:
        self->num->depth = self->depth;
        self->num->str(self->num);
        return;
    case KND_ATTR_TEXT:
        self->text->depth = self->depth;
        self->text->str(self->text);
        return;*/
    default:
        break;
    }

    knd_log("%*s%.*s => %.*s", depth * KND_OFFSET_SIZE, "",
            self->attr->name_size, self->attr->name,
            self->val_size, self->val);
    if (state) {
        knd_log("%*s    [state:%zu  phase:%d]",
                depth * KND_OFFSET_SIZE, "",
                state->numid, state->phase);
    }

}

static int export_JSON(struct kndAttrInst *self,
                       struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;

    if (DEBUG_ATTR_INST_LEVEL_2)
        knd_log(".. export %.*s attr_inst val => \"%.*s\"",
                self->attr->name_size, self->attr->name,
                self->val_size, self->val);

    if (self->inner) {
        /*if (self->is_list) {
            buf_size = sprintf(buf, "\"%s_l\":[",
                               self->states->val);
            err = out->write(out, buf, buf_size);
            if (err) return err;

            obj = self->inner;
            while (obj) {
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
        */
        
        /* single anonymous inner obj */
        err = out->write(out, "\"", 1);
        if (err) goto final;
        err = out->write(out, self->attr->name, self->attr->name_size);
        if (err) goto final;
        err = out->write(out, "\":", strlen("\":"));
        if (err) goto final;

        err = self->inner->export(self->inner, KND_FORMAT_JSON, task);
        
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
    case KND_ATTR_FLOAT:
        err = out->write(out, self->val, self->val_size);
        if (err) goto final;
        return knd_OK;
    case KND_ATTR_REF:
        //err = self->ref_inst->export(self->ref_inst, KND_FORMAT_JSON, out);
        //if (err) return err;
        //return knd_OK;
    case KND_ATTR_DATE:
    case KND_ATTR_STR:
    case KND_ATTR_BIN:
        err = out->writec(out, '"');
        if (err) goto final;
        err = out->write(out, self->val, self->val_size);
        if (err) goto final;
        err = out->writec(out, '"');
        if (err) goto final;
        return knd_OK;
    default:
        break;
    }

    /* nested repr */
    err = out->write(out, "{", 1);
    if (err) goto final;

    //curr_size = out->buf_size;

    /*if (self->attr) {
        switch (self->attr->type) {
        case  KND_ATTR_TEXT:
            text = self->text;
            err = text->export(text, KND_FORMAT_JSON, out);
            if (err) goto final;
            break;
        case KND_ATTR_REF:
            ref = self->ref;
            ref->out = out;
            ref->format = self->format;
            err = ref->export(ref);
            if (err) goto final;
            break;
        default:
            break;
        }
        }*/

    if (self->states) {
        err = out->writef(out, "\"val\":\"%.*s\"",
                          self->val_size,
                          (const char*)self->val);  RET_ERR();
    }

final:
    return err;
}

static int export_GSL(struct kndAttrInst *self,
                      struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;

    if (DEBUG_ATTR_INST_LEVEL_2)
        knd_log("\n  .. GSL export of \"%.*s\" attr_inst.. ",
                self->attr->name_size, self->attr->name);

    /* single anonymous inner obj */
    if (self->inner) {
        err = out->write(out, "{", 1);
        if (err) return err;
        err = out->write(out, self->attr->name, self->attr->name_size);
        if (err) return err;
        
        err = self->inner->export(self->inner, KND_FORMAT_GSL, task);

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
        /*    err = out->write(out, self->num->val, self->num->val_size);
        if (err) return err;
        break;*/
    case KND_ATTR_NUM:
    case KND_ATTR_STR:
    case KND_ATTR_BIN:
        err = out->write(out, self->val, self->val_size);
        if (err) return err;
        break;
        /*case  KND_ATTR_TEXT:
        text = self->text;
        err = text->export(text, KND_FORMAT_GSP, out);
        if (err) return err;
        break;
    case KND_ATTR_REF:
        ref = self->ref;
        ref->out = out;
        ref->format = KND_FORMAT_GSP;
        err = ref->export(ref);
        if (err) return err;
        break;*/
    default:
        break;
    }

    err = out->write(out, "}", 1);
    if (err) return err;

    return knd_OK;
}

extern int knd_attr_inst_export(struct kndAttrInst *self,
                           knd_format format,
                           struct kndTask *task)
{
    int err;

    switch (format) {
    case KND_FORMAT_JSON:
        err = export_JSON(self, task);
        if (err) return err;
        break;
    case KND_FORMAT_GSL:
        err = export_GSL(self, task);
        if (err) return err;
        break;
    case KND_FORMAT_GSP:
        err = export_GSL(self, task);
        if (err) return err;
        break;
    default:
        break;
    }
    
    return knd_OK;
}

static gsl_err_t run_empty_val_warning(void *obj,
                                       const char *unused_var(val),
                                       size_t unused_var(val_size))
{
    struct kndAttrInst *self = (struct kndAttrInst*)obj;
    knd_log("-- empty val of \"%.*s\" not accepted :(",
            self->attr->name_size, self->attr->name);
    return make_gsl_err(gsl_FAIL);
}

static void register_state(struct kndAttrInst *self,
                           struct kndState *state,
                           struct kndStateRef *state_ref)
{
    if (!self->states)
        state->phase = KND_CREATED;
    else
        state->phase = KND_UPDATED;

    self->states = state;
    self->num_states++;
    state->numid = self->num_states;
    state_ref->next =  self->parent->attr_inst_state_refs;
    self->parent->attr_inst_state_refs = state_ref;
}

static gsl_err_t run_set_val(void *obj, const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrInst *self = ctx->attr_inst;
    if (DEBUG_ATTR_INST_LEVEL_2)
        knd_log(".. attr \"%.*s\" [%s] to set val \"%.*s\"",
                self->attr->name_size, self->attr->name,
                knd_attr_names[self->attr->type], val_size, val);
    struct kndState *state;
    struct kndStateVal *state_val;
    struct kndStateRef *state_ref;
    struct kndMemPool *mempool = ctx->task->mempool;
    int err;

    if (!val_size) return make_gsl_err(gsl_FORMAT);
    if (val_size >= KND_VAL_SIZE) return make_gsl_err(gsl_LIMIT);

    err = knd_state_new(mempool, &state);
    if (err) {
        knd_log("-- state alloc failed");
        return make_gsl_err_external(err);
    }
    err = knd_state_val_new(mempool, &state_val);
    if (err) {
        knd_log("-- state val alloc failed");
        return make_gsl_err_external(err);
    }
    err = knd_state_ref_new(mempool, &state_ref);
    if (err) {
        knd_log("-- state ref alloc failed");
        return make_gsl_err_external(err);
    }
    state_ref->state = state;

    state_val->obj = (void*)self;
    state_val->val      = val;
    state_val->val_size = val_size;
    state->val          = state_val;

    self->val = val;
    self->val_size = val_size;

    register_state(self, state, state_ref);

    if (DEBUG_ATTR_INST_LEVEL_2)
        knd_log("++ attr_inst val set: \"%.*s\" [state:%zu]",
                self->val_size, self->val, state->numid);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t check_class_inst_name(void *obj,
                                       const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndAttrInst *self = ctx->attr_inst;
    struct kndClass *c = ctx->class;
    struct kndClassInst *inst;
    int err;

    if (DEBUG_ATTR_INST_LEVEL_2)
        knd_log(".. class \"%.*s\" to check inst name: \"%.*s\"",
                c->name_size, c->name,
                name_size, name);

    err = knd_get_class_inst(c, name, name_size, task, &inst);
    if (err) return make_gsl_err_external(err);

    self->ref_inst = inst;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_inst_ref(void *obj,
                                      const char *rec,
                                      size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrInst *self = ctx->attr_inst;

    if (DEBUG_ATTR_INST_LEVEL_2)
        knd_log(".. parse class inst: \"%.*s\"", 16, rec);

    if (!ctx->class) {
        knd_log("-- no class specified");
        return make_gsl_err(gsl_FAIL);
    }

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = check_class_inst_name,
          .obj = ctx
        },
        { .is_default = true,
          .run = run_empty_val_warning,
          .obj = self
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t check_class_name(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrInst *self = ctx->attr_inst;
    struct kndClass *ref_class = self->attr->ref_class;
    struct kndRepo *repo = self->root->base->entry->repo;
    struct kndClass *c;
    int err;

    if (DEBUG_ATTR_INST_LEVEL_TMP)
        knd_log(".. attr \"%.*s\" [%s] to check class name: \"%.*s\"",
                self->attr->name_size, self->attr->name,
                knd_attr_names[self->attr->type], name_size, name);

    if (!ref_class)  {
        knd_log("-- no ref template class specified");
        return make_gsl_err(gsl_FAIL);
    }

    err = knd_get_class(repo, name, name_size, &c, ctx->task);
    if (err) {
        knd_log("-- no such class");
        return make_gsl_err_external(err);
    }

    err = knd_is_base(ref_class, c);
    if (err) return make_gsl_err_external(err);

    ctx->class = c;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_ref(void *obj,
                                const char *rec,
                                size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrInst *self = ctx->attr_inst;

    if (DEBUG_ATTR_INST_LEVEL_2)
        knd_log(".. parse class ref: \"%.*s\"", 16, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = check_class_name,
          .obj = ctx
        },
        { .name = "inst",
          .name_size = strlen("inst"),
          .parse = parse_class_inst_ref,
          .obj = ctx
        },
        { .is_default = true,
          .run = run_empty_val_warning,
          .obj = self
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

gsl_err_t knd_import_attr_inst(struct kndAttrInst *self,
                          const char *rec, size_t *total_size,
                          struct kndTask *task)
{
    if (DEBUG_ATTR_INST_LEVEL_2)
        knd_log(".. ATTR_INST \"%.*s\" parse REC: \"%.*s\"",
                self->attr->name_size, self->attr->name,
                16, rec);

    struct LocalContext ctx = {
        .attr_inst = self,
        .task = task
    };
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_val,
          .obj = &ctx
        },
        { .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_ref,
          .obj = &ctx
        },
        { .is_default = true,
          .run = run_empty_val_warning,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

extern gsl_err_t knd_attr_inst_parse_select(struct kndAttrInst *self,
                                       const char *rec,
                                       size_t *total_size)
{
    if (DEBUG_ATTR_INST_LEVEL_2)
        knd_log(".. ATTR_INST \"%.*s\" parse REC: \"%.*s\"",
                self->attr->name_size, self->attr->name,
                16, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_val,
          .obj = self
        },
        { .is_default = true,
          .run = run_empty_val_warning,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

int knd_attr_inst_resolve(struct kndAttrInst *self)
{
    struct kndClassInst *obj;
    int err;
    
    if (self->inner) {
        for (obj = self->inner; obj; obj = obj->next) {
            err = obj->resolve(obj);
            if (err) return err;
        }
    }
    return knd_OK;
}

extern int knd_attr_inst_new(struct kndMemPool *mempool,
                             struct kndAttrInst **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL_X2,
                            sizeof(struct kndAttrInst), &page);                       RET_ERR();
    *result = page;
    return knd_OK;
}

extern int knd_attr_inst_mem(struct kndMemPool *mempool,
                             struct kndAttrInst **result)
{
    void *page;
    int err;
    err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_SMALL_X2,
                                 sizeof(struct kndAttrInst), &page);                       RET_ERR();
    *result = page;
    return knd_OK;
}
