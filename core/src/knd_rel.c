#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_rel.h"
#include "knd_task.h"
#include "knd_repo.h"
#include "knd_output.h"
#include "knd_object.h"
#include "knd_utils.h"
#include "knd_concept.h"
#include "knd_mempool.h"
#include "knd_parser.h"
#include "knd_text.h"

#define DEBUG_REL_LEVEL_0 0
#define DEBUG_REL_LEVEL_1 0
#define DEBUG_REL_LEVEL_2 0
#define DEBUG_REL_LEVEL_3 0
#define DEBUG_REL_LEVEL_TMP 1

static void
del(struct kndRel *self __attribute__((unused)))
{
}

static void str(struct kndRel *self __attribute__((unused)))
{
}

static int run_set_translation_text(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndTranslation *tr = (struct kndTranslation*)obj;
    struct kndTaskArg *arg;
    const char *val = NULL;
    size_t val_size = 0;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. run set translation text..");

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!memcmp(arg->name, "_impl", strlen("_impl"))) {
            val = arg->val;
            val_size = arg->val_size;
        }
    }
    if (!val_size) return knd_FAIL;
    if (val_size >= KND_NAME_SIZE) return knd_LIMIT;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. run set translation text: %.*s [%lu]\n", val_size, val,
                (unsigned long)val_size);

    memcpy(tr->val, val, val_size);
    tr->val_size = val_size;

    return knd_OK;
}


static int read_gloss(void *obj,
                      const char *rec,
                      size_t *total_size)
{
    struct kndTranslation *tr = (struct kndTranslation*)obj;
    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_translation_text,
          .obj = tr
        }
    };
    int err;

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. reading gloss translation: \"%.*s\"",
                tr->locale_size, tr->locale);

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;
    
    return knd_OK;
}

static int gloss_append(void *accu,
                        void *item)
{
    struct kndRel *self = accu;
    struct kndTranslation *tr = item;

    tr->next = self->tr;
    self->tr = tr;
   
    return knd_OK;
}

static int gloss_alloc(void *obj,
                       const char *name,
                       size_t name_size,
                       size_t count,
                       void **item)
{
    struct kndRel *self = obj;
    struct kndTranslation *tr;

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. %.*s to create gloss: %.*s count: %zu",
                self->name_size, self->name, name_size, name, count);

    if (name_size > KND_LOCALE_SIZE) return knd_LIMIT;

    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return knd_NOMEM;

    memset(tr, 0, sizeof(struct kndTranslation));
    memcpy(tr->curr_locale, name, name_size);
    tr->curr_locale_size = name_size;

    tr->locale = tr->curr_locale;
    tr->locale_size = tr->curr_locale_size;
    *item = tr;

    return knd_OK;
}


static int
kndRel_set_reverse_rel(struct kndRel *self,
                   struct kndObject *obj)
{
    struct kndRelClass *relc;
    struct kndRelType *reltype;
    struct kndConcept *conc;

    /*conc = self->elem->attr->parent_conc;
    if (DEBUG_REL_LEVEL_2)
        knd_log(".. %.*s OBJ to get REL from %s => %s",
                obj->name_size, obj->name, 
                conc->name,
                obj->conc->name);

    for (relc = obj->reverse_rel_classes; relc; relc = relc->next) {
        if (relc->conc == conc) break;
    }

    if (!relc) {
        relc = malloc(sizeof(struct kndRelClass));
        if (!relc) return knd_NOMEM;
        memset(relc, 0, sizeof(struct kndRelClass));
        relc->conc = conc;
        relc->next = obj->reverse_rel_classes;
        obj->reverse_rel_classes = relc;
    }
    for (reltype = relc->rel_types; reltype; reltype = reltype->next) {
        if (reltype->attr == self->elem->attr) break;
    }

    if (!reltype) {
        reltype = malloc(sizeof(struct kndRelType));
        if (!reltype) return knd_NOMEM;
        memset(reltype, 0, sizeof(struct kndRelType));
        reltype->attr = self->elem->attr;
        reltype->next = relc->rel_types;
        relc->rel_types = reltype;
    }
    
    if (!reltype->tail) {
        reltype->tail = self;
        reltype->rels = self;
    }
    else {
        reltype->tail->next = self;
        reltype->tail = self;
    }

    reltype->num_rels++;
    */
    return knd_OK;
}


static int kndRel_resolve(struct kndRel *self)
{
    struct kndConcept *conc;
    struct kndObjEntry *entry;
    struct kndObject *obj;
    const char *obj_name;
    int err, e;
    
    /*    conc = self->elem->attr->conc;

    if (DEBUG_REL_LEVEL_2) {
        knd_log(".. resolve REL: %.*s  state:%p",
                conc->name_size, conc->name,
                self->states);

        knd_log(".. resolve REL: %.*s::%.*s => \"%.*s\" dir: %p",
                conc->name_size, conc->name,
                self->elem->attr->name_size, self->elem->attr->name,
                self->states->val_size, self->states->val,
                conc->dir);
    }

    if (!conc->dir || !conc->dir->obj_idx) {
        knd_log("-- \"%.*s\" class has no obj idx, unable to resolve rel: \"%.*s\" :(",
                conc->name_size, conc->name,
                self->elem->attr->name_size, self->elem->attr->name);
        return knd_FAIL;
    }

    obj_name = self->states->val;
    entry = conc->dir->obj_idx->get(conc->dir->obj_idx, obj_name);
    if (!entry) {
        knd_log("-- no such obj: \"%s\" :(", obj_name);
        e = self->log->write(self->log, "no such obj: ", strlen("no such obj: "));
        if (e) return e;
        e = self->log->write(self->log, obj_name, self->states->val_size);
        if (e) return e;
        return knd_FAIL;
    }

    obj = entry->obj;
    self->states->obj = obj;
    */

    /* set reverse_rel */
    /*err = kndRel_set_reverse_rel(self, obj);
    if (err) return err;
    */
    return knd_OK;
}

static int run_set_name(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndRel *self = obj;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!memcmp(arg->name, "_impl", strlen("_impl"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }
    if (!name_size) return knd_FAIL;
    if (name_size >= KND_NAME_SIZE) return knd_LIMIT;

    memcpy(self->name, name, name_size);
    self->name_size = name_size;

    return knd_OK;
}

static int run_set_val(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndRel *self = (struct kndRel*)obj;
    struct kndTaskArg *arg;
    struct kndRelState *state;
    const char *val = NULL;
    size_t val_size = 0;

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. run set rel val..");

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "_impl", strlen("_impl"))) {
            val = arg->val;
            val_size = arg->val_size;
        }
    }

    if (!val_size) return knd_FAIL;
    if (val_size >= KND_NAME_SIZE)
        return knd_LIMIT;

    state = malloc(sizeof(struct kndRelState));
    if (!state) return knd_NOMEM;
    memset(state, 0, sizeof(struct kndRelState));
    self->states = state;
    self->num_states = 1;

    memcpy(state->val, val, val_size);
    state->val[val_size] = '\0';
    state->val_size = val_size;

    return knd_OK;
}

static int
export_reverse_rel_JSON(struct kndRel *self)
{
    struct kndObject *obj;
    struct kndOutput *out = self->out;
    int err = knd_FAIL;

    /*
    if (!self->elem) {
        err = out->write(out, "\"", 1);
        if (err) return err;
        err = out->write(out, self->name, self->name_size);
        if (err) return err;
        err = out->write(out, "\"", 1);
        if (err) return err;
        return knd_OK;
    }

    obj = self->elem->root;
    if (DEBUG_REL_LEVEL_2)
        knd_log(".. export reverse_rel to JSON..");

    obj->out = out;
    obj->depth = 0;
    err = obj->export(obj);
    if (err) return err;
    */
    
    return knd_OK;
}

static int export_GSP(struct kndRel *self)
{
    struct kndObject *obj;
    struct kndOutput *out = self->out;
    int err;

    /*if (!self->states) return knd_FAIL;

    obj = self->elem->root;
    
    
    err = out->write(out, "{", 1);
    if (err) return err;
    err = out->write(out, self->states->val, self->states->val_size);
    if (err) return err;
    err = out->write(out, "{c ", strlen("{c "));
    if (err) return err;

    err = out->write(out, obj->conc->name, obj->conc->name_size);
    if (err) return err;

    err = out->write(out, "}}", strlen("}}"));
    if (err) return err;
    */
    return knd_OK;
}

static int 
export_reverse_rel_GSP(struct kndRel *self)
{
    struct kndObject *obj;
    struct kndOutput *out;
    int err = knd_FAIL;

    /* obj = self->elem->root;
    out = self->out;

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. export reverse_rel to JSON..");

    obj->out = out;
    obj->depth = 0;

    err = out->write(out, "{", 1);
    if (err) return err;
    err = out->write(out, obj->id, KND_ID_SIZE);
    if (err) return err;

    err = out->write(out, " ", 1);
    if (err) return err;

    err = out->write(out, obj->name, obj->name_size);
    if (err) return err;

    err = out->write(out, "}", 1);
    if (err) return err;
    */
    
    return knd_OK;
}

static int export(struct kndRel *self)
{
    int err;

    switch (self->format) {
    case KND_FORMAT_JSON:
        /*err = export_JSON(self);
          if (err) return err; */
        break;
    case KND_FORMAT_GSP:
        err = export_GSP(self);
        if (err) return err;
        break;
    default:
        break;
    }
    
    return knd_OK;
}

static int export_reverse_rel(struct kndRel *self)
{
    int err;

    switch (self->format) {
    case KND_FORMAT_JSON:
        err = export_reverse_rel_JSON(self);
        if (err) return err;
        break;
    case KND_FORMAT_GSP:
        err = export_reverse_rel_GSP(self);
        if (err) return err;
        break;
    default:
        break;
    }
    
    return knd_OK;
}

static int import_rel(struct kndRel *self,
                      const char *rec,
                      size_t *total_size)
{
    struct kndRel *rel;
    int err;

    if (DEBUG_REL_LEVEL_TMP)
        knd_log(".. import Rel: \"%.*s\"..  mempool: %p", 32, rec, self->mempool);

    err  = self->mempool->new_rel(self->mempool, &rel);
    if (err) return err;

    knd_log("REL: %p", rel);
    rel->out = self->out;
    rel->log = self->log;
    rel->task = self->task;
    rel->mempool = self->mempool;
    rel->rel_idx = self->rel_idx;

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = rel
        }/*,
        { .type = KND_CHANGE_STATE,
          .name = "base",
          .name_size = strlen("base"),
          .parse = parse_baseclass,
          .obj = rel
          }*/,
        { .type = KND_CHANGE_STATE,
          .is_list = true,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .accu = rel,
          .alloc = gloss_alloc,
          .append = gloss_append,
          .parse = read_gloss
        },
        { .is_list = true,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .accu = rel,
          .alloc = gloss_alloc,
          .append = gloss_append,
          .parse = read_gloss
        }/*,
        { .type = KND_CHANGE_STATE,
          .name = "subj",
          .name_size = strlen("subj"),
          .parse = parse_aggr,
          .obj = rel
          },*/
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) goto final;

    if (!rel->name_size) {
        err = knd_FAIL;
        goto final;
    }

    /*dir = (struct kndConcDir*)self->class_idx->get(self->class_idx,
                                                   rel->name, rel->name_size);
    if (dir) {
        knd_log("-- %s class name doublet found :(", rel->name);

        self->log->reset(self->log);
        err = self->log->write(self->log,
                               rel->name,
                               rel->name_size);
        if (err) goto final;
        
        err = self->log->write(self->log,
                               " class name already exists",
                               strlen(" class name already exists"));
        if (err) goto final;
        
        err = knd_FAIL;
        goto final;
    }
    */

    /*if (!self->batch_mode) {
        rel->next = self->inbox;
        self->inbox = c;
        self->inbox_size++;
        }*/

    /*dir = malloc(sizeof(struct kndConcDir));
    memset(dir, 0, sizeof(struct kndConcDir));
    dir->conc = c;
    rel->dir = dir;
    err = self->class_idx->set(self->class_idx,
                               rel->name, rel->name_size, (void*)dir);
    if (err) goto final;
    */

    if (DEBUG_REL_LEVEL_TMP)
        rel->str(rel);
     
    return knd_OK;
 final:
    
    rel->del(rel);
    return err;
}


extern void 
kndRel_init(struct kndRel *self)
{
    self->del = del;
    self->str = str;
    self->export = export;
    self->resolve = kndRel_resolve;
    //self->parse = parse_GSL;
    self->import = import_rel;
}

extern int 
kndRel_new(struct kndRel **rel)
{
    struct kndRel *self;

    self = malloc(sizeof(struct kndRel));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndRel));
    
    kndRel_init(self);
    *rel = self;
    return knd_OK;
}
