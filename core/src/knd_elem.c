#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/stat.h>

#include <gsl-parser.h>
#include <glb-lib/output.h>

#include "knd_class_inst.h"
#include "knd_class.h"
#include "knd_repo.h"
#include "knd_elem.h"
#include "knd_attr.h"
#include "knd_state.h"
#include "knd_mempool.h"

#include "knd_text.h"
#include "knd_ref.h"
#include "knd_num.h"

#include "knd_user.h"

#define DEBUG_ELEM_LEVEL_1 0
#define DEBUG_ELEM_LEVEL_2 0
#define DEBUG_ELEM_LEVEL_3 0
#define DEBUG_ELEM_LEVEL_4 0
#define DEBUG_ELEM_LEVEL_TMP 1

/*static void del(struct kndElem *self)
{
    // TODO
}
*/

static void str(struct kndElem *self)
{
    if (self->aggr) {
        if (self->is_list) {
            knd_log("%*s[%.*s\n",
                    self->depth * KND_OFFSET_SIZE, "", self->attr->name_size, self->attr->name);
            struct kndClassInst *obj = self->aggr;
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
        knd_log("ref:");
        self->ref_inst->str(self->ref_inst);
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

    knd_log("%*s%.*s => %.*s", self->depth * KND_OFFSET_SIZE, "",
            self->attr->name_size, self->attr->name,
            self->states->val_size, self->states->val);
}

static int export_JSON(struct kndElem *self)
{
    struct glbOutput *out = self->out;
    int err;

    if (self->aggr) {
        /*if (self->is_list) {
            buf_size = sprintf(buf, "\"%s_l\":[",
                               self->states->val);
            err = out->write(out, buf, buf_size);
            if (err) return err;

            obj = self->aggr;
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
        
        /* single anonymous aggr obj */
        err = out->write(out, "\"", 1);
        if (err) goto final;
        err = out->write(out, self->attr->name, self->attr->name_size);
        if (err) goto final;
        err = out->write(out, "\":", strlen("\":"));
        if (err) goto final;

        knd_log(".. aggr inst export..");
        err = self->aggr->export(self->aggr, KND_FORMAT_JSON, out);
        
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
    case KND_ATTR_REF:
        err = self->ref_inst->export(self->ref_inst, KND_FORMAT_JSON, out);
        if (err) return err;
        return knd_OK;
    case KND_ATTR_NUM:
        err = out->write(out, self->states->val, self->states->val_size);
        if (err) goto final;
        return knd_OK;
    case KND_ATTR_STR:
    case KND_ATTR_BIN:
        err = out->writec(out, '"');
        if (err) goto final;
        err = out->write(out, self->states->val, self->states->val_size);
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
                          self->states->val_size,
                          (const char*)self->states->val);  RET_ERR();
    }

final:

    return err;
}

static int export_GSP(struct kndElem *self,
                      struct glbOutput *out)
{
    int err;

    if (DEBUG_ELEM_LEVEL_2)
        knd_log("\n  .. GSP export of \"%.*s\" elem.. ",
                self->attr->name_size, self->attr->name);
    
    /* single anonymous aggr obj */
    if (self->aggr) {
        err = out->write(out, "{", 1);
        if (err) return err;
        err = out->write(out, self->attr->name, self->attr->name_size);
        if (err) return err;
        
        err = self->aggr->export(self->aggr, KND_FORMAT_GSP, out);

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
        /*    err = out->write(out, self->num->states->val, self->num->states->val_size);
        if (err) return err;
        break;*/
    case KND_ATTR_NUM:
    case KND_ATTR_STR:
    case KND_ATTR_BIN:
        err = out->write(out, self->states->val, self->states->val_size);
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

static int kndElem_export(struct kndElem *self,
                          knd_format format,
                          struct glbOutput *out)
{
    int err;

    switch (format) {
    case KND_FORMAT_JSON:
        err = export_JSON(self);
        if (err) return err;
        break;
    case KND_FORMAT_GSP:
        err = export_GSP(self, out);
        if (err) return err;
        break;
    default:
        break;
    }
    
    return knd_OK;
}

static gsl_err_t run_empty_val_warning(void *obj,
                                       const char *val __attribute__((unused)),
                                       size_t val_size __attribute__((unused)))
{
    struct kndElem *self = (struct kndElem*)obj;
    knd_log("-- empty val of \"%.*s\" not accepted :(",
            self->attr->name_size, self->attr->name);
    return make_gsl_err(gsl_FAIL);
}

static gsl_err_t run_set_val(void *obj, const char *val, size_t val_size)
{
    struct kndElem *self = obj;

    if (DEBUG_ELEM_LEVEL_2)
        knd_log(".. attr \"%.*s\" [%s] to set val \"%.*s\"",
                self->attr->name_size, self->attr->name,
                knd_attr_names[self->attr->type], val_size, val);
    struct kndState *state;
    struct kndMemPool *mempool = self->obj->base->entry->repo->mempool;
    int err;

    if (!val_size) return make_gsl_err(gsl_FORMAT);
    if (val_size >= KND_VAL_SIZE) return make_gsl_err(gsl_LIMIT);

    err = knd_state_new(mempool, &state);
    if (err) {
        knd_log("-- state alloc failed :(");
        return make_gsl_err_external(err);
    }

    state->val = (void*)val;
    state->val_size = val_size;

    self->states = state;
    self->num_states++;
    state->numid = self->num_states;

    /* TODO: validate if needed */

    if (DEBUG_ELEM_LEVEL_2)
        knd_log("++ ELEM VAL: \"%.*s\"", state->val_size, state->val);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t check_class_inst_name(void *obj, const char *name, size_t name_size)
{
    struct kndElem *self = obj;
    struct kndClass *c = self->curr_class;
    struct kndClassInst *inst;
    int err;

    if (DEBUG_ELEM_LEVEL_2)
        knd_log(".. class \"%.*s\" to check inst name: \"%.*s\"",
                c->name_size, c->name,
                name_size, name);

    err = knd_get_class_inst(self->curr_class, name, name_size, &inst);
    if (err) return make_gsl_err_external(err);

    self->ref_inst = inst;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_inst_ref(void *obj,
                                      const char *rec,
                                      size_t *total_size)
{
    struct kndElem *self = obj;

    if (DEBUG_ELEM_LEVEL_2)
        knd_log(".. parse class inst: \"%.*s\"", 16, rec);

    if (!self->curr_class) {
        knd_log("-- no class specified");
        return make_gsl_err(gsl_FAIL);
    }
    
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = check_class_inst_name,
          .obj = self
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
    struct kndElem *self = obj;
    struct kndClass *ref_class = self->attr->ref_class;
    struct kndRepo *repo = self->root->base->entry->repo;
    struct kndClass *c;
    int err;

    if (DEBUG_ELEM_LEVEL_2)
        knd_log(".. attr \"%.*s\" [%s] to check class name: \"%.*s\"",
                self->attr->name_size, self->attr->name,
                knd_attr_names[self->attr->type], name_size, name);

    if (!ref_class)  {
        knd_log("-- no ref template class specified");
        return make_gsl_err(gsl_FAIL);
    }

    err = knd_get_class(repo, name, name_size, &c);
    if (err) {
        knd_log("-- no such class");
        return make_gsl_err_external(err);
    }

    err = knd_is_base(ref_class, c);
    if (err) return make_gsl_err_external(err);

    self->curr_class = c;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_ref(void *obj,
                                const char *rec,
                                size_t *total_size)
{
    struct kndElem *self = obj;

    if (DEBUG_ELEM_LEVEL_2)
        knd_log(".. parse class ref: \"%.*s\"", 16, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = check_class_name,
          .obj = self
        },
        { .name = "inst",
          .name_size = strlen("inst"),
          .parse = parse_class_inst_ref,
          .obj = self
        },
        { .is_default = true,
          .run = run_empty_val_warning,
          .obj = self
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_GSL(struct kndElem *self,
                           const char *rec,
                           size_t *total_size)
{
    if (DEBUG_ELEM_LEVEL_2)
        knd_log(".. ELEM \"%.*s\" parse REC: \"%.*s\"",
                self->attr->name_size, self->attr->name,
                16, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_val,
          .obj = self
        },
        { .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_ref,
          .obj = self
        },
        { .is_default = true,
          .run = run_empty_val_warning,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static int kndElem_resolve(struct kndElem *self)
{
    struct kndClassInst *obj;
    int err;
    
    if (self->aggr) {
        for (obj = self->aggr; obj; obj = obj->next) {
            err = obj->resolve(obj);
            if (err) return err;
        }
    }

    /*switch (self->attr->type) {
    case KND_ATTR_REF:
        err = self->ref->resolve(self->ref);
        if (err) return err;
    default:
        break;
    }
    */
    return knd_OK;
}

extern void kndElem_init(struct kndElem *self)
{
    self->str = str;
    self->parse = parse_GSL;
    self->resolve = kndElem_resolve;
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

extern int knd_class_inst_elem_new(struct kndMemPool *mempool,
                                   struct kndElem **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                            sizeof(struct kndElem), &page);                       RET_ERR();
    *result = page;
    kndElem_init(*result);
    return knd_OK;
}
