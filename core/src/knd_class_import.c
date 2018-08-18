#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

/* numeric conversion by strtol */
#include <errno.h>
#include <limits.h>

#include "knd_config.h"
#include "knd_mempool.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_user.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_http_codes.h"

#include <gsl-parser.h>
#include <glb-lib/output.h>

#define DEBUG_CLASS_IMPORT_LEVEL_1 0
#define DEBUG_CLASS_IMPORT_LEVEL_2 0
#define DEBUG_CLASS_IMPORT_LEVEL_3 0
#define DEBUG_CLASS_IMPORT_LEVEL_4 0
#define DEBUG_CLASS_IMPORT_LEVEL_5 0
#define DEBUG_CLASS_IMPORT_LEVEL_TMP 1

static gsl_err_t import_nested_attr_var(void *obj,
                                         const char *name, size_t name_size,
                                         const char *rec, size_t *total_size);
static gsl_err_t read_nested_attr_var(void *obj,
                                       const char *name, size_t name_size,
                                       const char *rec, size_t *total_size);
static void append_attr_var(struct kndClassVar *ci,
                             struct kndAttrVar *attr_var);


extern gsl_err_t knd_parse_import_class_inst(void *data,
                                             const char *rec,
                                             size_t *total_size)
{
    struct kndClass *self = data;
    struct kndClass *c;
    struct kndClassInst *obj;
    struct kndObjEntry *entry;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    struct kndTask *task = self->entry->repo->task;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2) {
        knd_log(".. import \"%.*s\" inst..", 128, rec);
    }
    if (!self->curr_class) {
        knd_log("-- class not specified :(");
        return *total_size = 0, make_gsl_err(gsl_FAIL);
    }
    err = mempool->new_obj(mempool, &obj);
    if (err) {
        knd_log("-- class inst alloc failed :(");
        return *total_size = 0, make_gsl_err_external(err);
    }
    err = mempool->new_state(mempool, &obj->states);
    if (err) {
        knd_log("-- state alloc failed :(");
        return *total_size = 0, make_gsl_err_external(err);
    }
    err = mempool->new_obj_entry(mempool, &entry);
    if (err) return make_gsl_err_external(err);

    obj->entry = entry;
    entry->obj = obj;
    obj->states->phase = KND_SUBMITTED;
    obj->base = self->curr_class;

    parser_err = obj->parse(obj, rec, total_size);
    if (parser_err.code) return parser_err;

    c = obj->base;
    obj->next = c->obj_inbox;

    c->obj_inbox = obj;
    c->obj_inbox_size++;
    c->num_objs++;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log("++ %.*s obj parse OK! total objs in %.*s: %zu",
                obj->name_size, obj->name,
                c->name_size, c->name, c->obj_inbox_size);

    obj->entry->numid = c->num_objs;
    knd_num_to_str(obj->entry->numid, obj->entry->id, &obj->entry->id_size, KND_RADIX_BASE);

    if (!c->entry) {
        if (c->root_class) {
            knd_log("-- no entry in %.*s :(", c->name_size, c->name);
            return make_gsl_err(gsl_FAIL);
        }
        return make_gsl_err(gsl_OK);
    }

    /* automatic name assignment if no explicit name given */
    if (!obj->name_size) {
        obj->name = obj->entry->id;
        obj->name_size = obj->entry->id_size;
    }

    if (!c->entry->obj_name_idx) {
        err = ooDict_new(&c->entry->obj_name_idx, KND_HUGE_DICT_SIZE);
        if (err) return make_gsl_err_external(err);
    }

    err = c->entry->obj_name_idx->set(c->entry->obj_name_idx,
                                      obj->name, obj->name_size,
                                      (void*)entry);
    if (err) return make_gsl_err_external(err);
    c->entry->num_objs++;

    if (DEBUG_CLASS_IMPORT_LEVEL_1) {
        knd_log("++ OBJ registered in \"%.*s\" IDX:  [total:%zu valid:%zu]",
                c->name_size, c->name, c->entry->obj_name_idx->size, c->entry->num_objs);
        obj->depth = self->depth + 1;
        obj->str(obj);
    }
    task->type = KND_UPDATE_STATE;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_class_name(void *obj, const char *name, size_t name_size)
{
    struct kndClass *self = obj;
    struct kndRepo *repo = self->root_class->entry->repo;
    struct glbOutput *log = repo->log;
    struct ooDict *class_name_idx = self->root_class->class_name_idx;
    struct kndMemPool *mempool = repo->mempool;
    struct kndTask *task = repo->task;
    struct kndClassEntry *entry;
    struct kndState *state;
    int err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. set class name: %.*s", name_size, name);

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= sizeof self->entry->name) return make_gsl_err(gsl_LIMIT);

    entry = class_name_idx->get(class_name_idx, name, name_size);
    if (!entry) {
        err = mempool->new_class_entry(mempool, &entry);
        if (err) return make_gsl_err_external(err);
        entry->repo = repo;
        entry->class = self;
        self->entry = entry;

        memcpy(entry->name, name, name_size);
        entry->name_size = name_size;
        self->name = self->entry->name;
        self->name_size = name_size;

        err = class_name_idx->set(class_name_idx,
                                  entry->name, name_size,
                                  (void*)entry);
        if (err) return make_gsl_err_external(err);

        return make_gsl_err(gsl_OK);
    }

    /* class entry already exists */

    if (!entry->class) {
        entry->class =    self;
        self->entry =     entry;
        self->name =      entry->name;
        self->name_size = name_size;
        return make_gsl_err(gsl_OK);
    }

    if (entry->class->states) {
        state = entry->class->states;
        if (state->phase == KND_REMOVED) {
            entry->class = self;
            self->entry =  entry;

            if (DEBUG_CLASS_IMPORT_LEVEL_2)
                knd_log("== class was removed recently");

            self->name =      entry->name;
            self->name_size = name_size;
            return make_gsl_err(gsl_OK);
        }
    }

    knd_log("-- \"%.*s\" class doublet found :(", name_size, name);

    log->reset(log);
    err = log->write(log, name, name_size);
    if (err) return make_gsl_err_external(err);

    err = log->write(log,   " class name already exists",
                     strlen(" class name already exists"));
    if (err) return make_gsl_err_external(err);

    task->http_code = HTTP_CONFLICT;
    
    return make_gsl_err(gsl_FAIL);
}

static gsl_err_t set_class_var(void *obj, const char *name, size_t name_size)
{
    struct kndClassVar *self      = obj;
    struct kndClass *root_class   = self->root_class;
    struct ooDict *class_name_idx = root_class->class_name_idx;
    struct kndMemPool *mempool    = root_class->entry->repo->mempool;
    struct kndClassEntry *entry;
    void *result;
    int err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. root class:\"%.*s\" to check class var name: %.*s",
                root_class->entry->name_size,
                root_class->entry->name,
                name_size, name);

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    result = class_name_idx->get(class_name_idx, name, name_size);
    if (result) {
        self->entry = result;
        return make_gsl_err(gsl_OK);
    }

    /* register new class entry */
    err = mempool->new_class_entry(mempool, &entry);
    if (err) return make_gsl_err_external(err);

    memcpy(entry->name, name, name_size);
    entry->name_size = name_size;

    err = class_name_idx->set(class_name_idx,
                              entry->name, name_size,
                              (void*)entry);
    if (err) return make_gsl_err_external(err);

    entry->repo = root_class->entry->repo;
    self->entry = entry;

    return make_gsl_err(gsl_OK);
}


static gsl_err_t alloc_gloss_item(void *obj,
                                  const char *name,
                                  size_t name_size,
                                  size_t count,
                                  void **item)
{
    struct kndClass *self = obj;
    struct kndTranslation *tr;

    assert(name == NULL && name_size == 0);

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. %.*s: allocate gloss translation,  count: %zu",
                self->entry->name_size, self->entry->name, count);

    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return make_gsl_err_external(knd_NOMEM);

    memset(tr, 0, sizeof(struct kndTranslation));
    *item = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t append_gloss_item(void *accu,
                                   void *item)
{
    struct kndClass *self =   accu;
    struct kndTranslation *tr = item;

    tr->next = self->tr;
    self->tr = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_gloss_item(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndTranslation *tr = obj;
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = tr->curr_locale,
          .buf_size = &tr->curr_locale_size,
          .max_buf_size = sizeof tr->curr_locale
        },
        { .name = "t",
          .name_size = strlen("t"),
          .buf = tr->val,
          .buf_size = &tr->val_size,
          .max_buf_size = sizeof tr->val
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    if (tr->curr_locale_size == 0 || tr->val_size == 0)
        return make_gsl_err(gsl_FORMAT);  // error: both of them are required

    tr->locale = tr->curr_locale;
    tr->locale_size = tr->curr_locale_size;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. read gloss translation: \"%.*s\",  text: \"%.*s\"",
                tr->locale_size, tr->locale, tr->val_size, tr->val);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_gloss_array(void *obj,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndClass *self = obj;

    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .alloc = alloc_gloss_item,
        .append = append_gloss_item,
        .accu = self,
        .parse = parse_gloss_item
    };

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. %.*s: reading gloss",
                self->entry->name_size, self->entry->name);

    return gsl_parse_array(&item_spec, rec, total_size);
}


static gsl_err_t alloc_summary_item(void *obj,
                                    const char *name,
                                    size_t name_size,
                                    size_t count,
                                    void **item)
{
    struct kndClass *self = obj;
    struct kndTranslation *tr;

    assert(name == NULL && name_size == 0);

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. %.*s: allocate summary translation,  count: %zu",
                self->entry->name_size, self->entry->name, count);

    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return make_gsl_err_external(knd_NOMEM);

    memset(tr, 0, sizeof(struct kndTranslation));
    *item = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t append_summary_item(void *accu,
                                   void *item)
{
    struct kndClass *self =   accu;
    struct kndTranslation *tr = item;

    tr->next = self->summary;
    self->summary = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_summary_item(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndTranslation *tr = obj;
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = tr->curr_locale,
          .buf_size = &tr->curr_locale_size,
          .max_buf_size = sizeof tr->curr_locale
        },
        { .name = "t",
          .name_size = strlen("t"),
          .buf = tr->val,
          .buf_size = &tr->val_size,
          .max_buf_size = sizeof tr->val
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    if (tr->curr_locale_size == 0 || tr->val_size == 0)
        return make_gsl_err(gsl_FORMAT);  // error: both of them are required

    tr->locale = tr->curr_locale;
    tr->locale_size = tr->curr_locale_size;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. read summary translation: \"%.*s\",  text: \"%.*s\"",
                tr->locale_size, tr->locale, tr->val_size, tr->val);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_summary_array(void *obj,
                                     const char *rec,
                                     size_t *total_size)
{
    struct kndClass *self = obj;

    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .alloc = alloc_summary_item,
        .append = append_summary_item,
        .accu = self,
        .parse = parse_summary_item
    };

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. %.*s: reading summary",
                self->entry->name_size, self->entry->name);

    return gsl_parse_array(&item_spec, rec, total_size);
}

static gsl_err_t atomic_elem_alloc(void *obj,
                                   const char *val,
                                   size_t val_size,
                                   size_t count  __attribute__((unused)),
                                   void **item __attribute__((unused)))
{
    struct kndClass *self = obj;
    struct kndSet *class_idx, *set;
    struct kndClassEntry *entry;
    void *elem;
    int err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2) {
        knd_log("Conc %.*s: atomic elem alloc: \"%.*s\"",
                self->entry->name_size, self->entry->name,
                val_size, val);
    }
    class_idx = self->class_idx;

    err = class_idx->get(class_idx, val, val_size, &elem);
    if (err) {
        knd_log("-- IDX:%p couldn't resolve class id: \"%.*s\" [size:%zu] :(",
                class_idx, val_size, val, val_size);
        return make_gsl_err_external(knd_NO_MATCH);
    }

    entry = elem;

    set = self->entry->descendants;
    err = set->add(set, entry->id, entry->id_size, (void*)entry);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t atomic_elem_append(void *accu  __attribute__((unused)),
                                    void *item __attribute__((unused)))
{

    return make_gsl_err(gsl_OK);
}

/* facet parsing */

static gsl_err_t facet_alloc(void *obj,
                             const char *name,
                             size_t name_size,
                             size_t count __attribute__((unused)),
                             void **item)
{
    struct kndSet *set = obj;
    struct kndFacet *f;
    struct kndMemPool *mempool = set->base->repo->mempool;
    int err;

    assert(name == NULL && name_size == 0);

    if (DEBUG_CLASS_IMPORT_LEVEL_1)
        knd_log(".. \"%.*s\" set to alloc a facet..",
                set->base->name_size, set->base->name);

    err = mempool->new_facet(mempool, &f);
    if (err) return make_gsl_err_external(err);

    /* TODO: mempool alloc */
    err = ooDict_new(&f->set_name_idx, KND_SMALL_DICT_SIZE);
    if (err) return make_gsl_err_external(err);

    f->parent = set;

    *item = (void*)f;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t facet_append(void *accu,
                              void *item)
{
    struct kndSet *set = accu;
    struct kndFacet *f = item;

    if (set->num_facets >= KND_MAX_ATTRS)
        return make_gsl_err_external(knd_LIMIT);

    set->facets[set->num_facets] = f;
    set->num_facets++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_facet_name(void *obj, const char *name, size_t name_size)
{
    struct kndFacet *f = obj;
    struct kndSet *parent = f->parent;
    struct kndClass *c;
    struct kndAttr *attr;
    int err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. set %.*s to add facet \"%.*s\"..",
                parent->base->name_size, parent->base->name, name_size, name);

    c = parent->base->class;
    err = c->get_attr(c, name, name_size, &attr);
    if (err) {
        knd_log("-- no such facet attr: \"%.*s\"",
                name_size, name);
        return make_gsl_err_external(err);
    }

    f->attr = attr;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_alloc(void *obj,
                           const char *name,
                           size_t name_size,
                           size_t count __attribute__((unused)),
                           void **item)
{
    struct kndFacet *self = obj;
    struct kndSet *set;
    struct kndMemPool *mempool = self->attr->parent_class->entry->repo->mempool;
    int err;

    assert(name == NULL && name_size == 0);

    err = mempool->new_set(mempool, &set);
    if (err) return make_gsl_err_external(err);

    set->type = KND_SET_CLASS;
    //set->mempool = self->entry->repo->mempool;
    set->parent_facet = self;

    *item = (void*)set;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_append(void *accu,
                            void *item)
{
    struct kndFacet *self = accu;
    struct kndSet *set = item;
    int err;

    if (!self->set_name_idx) return make_gsl_err(gsl_OK);

    err = self->set_name_idx->set(self->set_name_idx,
                                  set->base->name, set->base->name_size,
                                  (void*)set);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}


static gsl_err_t atomic_classref_alloc(void *obj,
                                       const char *val,
                                       size_t val_size,
                                       size_t count  __attribute__((unused)),
                                       void **item __attribute__((unused)))
{
    struct kndSet *self = obj;
    struct kndSet *class_idx;
    struct kndClassEntry *entry;
    void *elem;
    int err;

    entry = self->base;

    if (DEBUG_CLASS_IMPORT_LEVEL_2) {
        knd_log(".. base class \"%.*s\": atomic classref alloc: \"%.*s\"",
                entry->name_size, entry->name,
                val_size, val);
    }

    class_idx = entry->class_idx;

    err = class_idx->get(class_idx, val, val_size, &elem);
    if (err) {
        knd_log("-- IDX:%p couldn't resolve class id: \"%.*s\" [size:%zu] :(",
                class_idx, val_size, val, val_size);
        return make_gsl_err_external(knd_NO_MATCH);
    }
    entry = elem;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log("++ classref resolved: %.*s \"%.*s\"",
                val_size, val, entry->name_size, entry->name);

    err = self->add(self, val, val_size, (void*)entry);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t resolve_set_base(void *obj,
                                  const char *id, size_t id_size)
{
    struct kndSet *set = obj;
    struct kndFacet *parent_facet = set->parent_facet;
    struct kndClass *c;
    void *result;
    int err;

    c = parent_facet->parent->base->class;
    err = c->class_idx->get(c->class_idx,
                            id, id_size, &result);
    if (err) {
        knd_log("-- no such class: \"%.*s\":(", id_size, id);
        return make_gsl_err_external(err);
    }
    set->base = result;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log("++ base class: %.*s \"%.*s\"",
                id_size, id, set->base->name_size, set->base->name);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_read(void *obj,
                          const char *rec,
                          size_t *total_size)
{
    struct kndSet *set = obj;

    struct gslTaskSpec classref_spec = {
        .is_list_item = true,
        .alloc = atomic_classref_alloc,
        .append = atomic_elem_append,
        .accu = set
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = resolve_set_base,
          .obj = set
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "c",
          .name_size = strlen("c"),
          .parse = gsl_parse_array,
          .obj = &classref_spec
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    if (!set->base) {
        knd_log("-- no base class provided");
        return make_gsl_err(gsl_FORMAT);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t facet_read(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndFacet *f = obj;

    struct gslTaskSpec set_item_spec = {
        .is_list_item = true,
        .alloc = set_alloc,
        .append = set_append,
        .accu = f,
        .parse = set_read
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_facet_name,
          .obj = f
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "set",
          .name_size = strlen("set"),
          .parse = gsl_parse_array,
          .obj = &set_item_spec
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    if (f->attr == NULL) {
        knd_log("-- no facet name provided :(");
        return make_gsl_err(gsl_FORMAT);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_descendants(void *obj,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndSet *set;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    size_t total_elems = 0;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. parsing a set of descendants: \"%.*s\"", 300, rec);

    err = mempool->new_set(mempool, &set);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    set->type = KND_SET_CLASS;
    set->base = self->entry;
    self->entry->descendants = set;

    struct gslTaskSpec c_item_spec = {
        .is_list_item = true,
        .alloc = atomic_elem_alloc,
        .append = atomic_elem_append,
        .accu = self
    };

    struct gslTaskSpec facet_spec = {
        .is_list_item = true,
        .alloc = facet_alloc,
        .append = facet_append,
        .parse = facet_read,
        .accu = set
    };

    struct gslTaskSpec specs[] = {
        {  .name = "tot",
           .name_size = strlen("tot"),
           .parse = gsl_parse_size_t,
           .obj = &total_elems
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "c",
          .name_size = strlen("c"),
          .parse = gsl_parse_array,
          .obj = &c_item_spec
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "fc",
          .name_size = strlen("fc"),
          .parse = gsl_parse_array,
          .obj = &facet_spec
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (total_elems != set->num_elems) {
        knd_log("-- set total elems mismatch: %zu vs actual %zu",
                total_elems, set->num_elems);
        return make_gsl_err(gsl_FAIL);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_aggr(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndAttr *attr;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. parsing the AGGR attr: \"%.*s\"", 32, rec);

    err = kndAttr_new(&attr);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent_class = self;
    attr->type = KND_ATTR_AGGR;

    parser_err = attr->parse(attr, rec, total_size);
    if (parser_err.code) {
        if (DEBUG_CLASS_IMPORT_LEVEL_TMP)
            knd_log("-- failed to parse the AGGR attr: %d", parser_err.code);
        return parser_err;
    }

    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }
    self->num_attrs++;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        attr->str(attr);

    if (attr->is_implied)
        self->implied_attr = attr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_str(void *obj,
                           const char *rec,
                           size_t *total_size)
{
    struct kndClass *self = (struct kndClass*)obj;
    struct kndAttr *attr;
    int err;
    gsl_err_t parser_err;

    err = kndAttr_new(&attr);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent_class = self;
    attr->type = KND_ATTR_STR;

    parser_err = attr->parse(attr, rec, total_size);
    if (parser_err.code) {
        knd_log("-- failed to parse the STR attr of \"%.*s\" :(",
                self->entry->name_size, self->entry->name);
        return parser_err;
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }

    if (attr->is_implied)
        self->implied_attr = attr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_bin(void *obj,
                           const char *rec,
                           size_t *total_size)
{
    struct kndClass *self = (struct kndClass*)obj;
    struct kndAttr *attr;
    gsl_err_t parser_err;
    int  err;

    err = kndAttr_new(&attr);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent_class = self;
    attr->type = KND_ATTR_BIN;

    parser_err = attr->parse(attr, rec, total_size);
    if (parser_err.code) {
        knd_log("-- failed to parse the BIN attr: %d", parser_err.code);
        return parser_err;
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_num(void *obj,
                           const char *rec,
                           size_t *total_size)
{
    struct kndClass *self = (struct kndClass*)obj;
    struct kndAttr *attr;
    int err;
    gsl_err_t parser_err;

    err = kndAttr_new(&attr);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent_class = self;
    attr->type = KND_ATTR_NUM;

    parser_err = attr->parse(attr, rec, total_size);
    if (parser_err.code) {
        knd_log("-- failed to parse the NUM attr: %d", parser_err.code);
        return parser_err;
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }

    if (attr->is_implied)
        self->implied_attr = attr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_ref(void *obj,
                           const char *rec,
                           size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndAttr *attr;
    int err;
    gsl_err_t parser_err;

    err = kndAttr_new(&attr);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent_class = self;
    attr->type = KND_ATTR_REF;

    parser_err = attr->parse(attr, rec, total_size);
    if (parser_err.code) {
        knd_log("-- failed to parse the REF attr: %d", parser_err.code);
        return parser_err;
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }

    if (attr->is_implied)
        self->implied_attr = attr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_proc(void *obj,
                           const char *rec,
                           size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndAttr *attr;
    int err;
    gsl_err_t parser_err;

    err = kndAttr_new(&attr);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent_class = self;
    attr->type = KND_ATTR_PROC;

    parser_err = attr->parse(attr, rec, total_size);
    if (parser_err.code) {
        knd_log("-- failed to parse the PROC attr: %d", parser_err.code);
        return parser_err;
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }

    if (attr->is_implied)
        self->implied_attr = attr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_text(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndClass *self = (struct kndClass*)obj;
    struct kndAttr *attr;
    int err;
    gsl_err_t parser_err;

    err = kndAttr_new(&attr);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent_class = self;
    attr->type = KND_ATTR_TEXT;
    attr->is_a_set = true;

    parser_err = attr->parse(attr, rec, total_size);
    if (parser_err.code) {
        knd_log("-- failed to parse the TEXT attr: %d", parser_err.code);
        return parser_err;
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }
    return make_gsl_err(gsl_OK);
}



static gsl_err_t class_var_alloc(void *obj,
                                 const char *name __attribute__((unused)),
                                 size_t name_size __attribute__((unused)),
                                 size_t count  __attribute__((unused)),
                                 void **item)
{
    struct kndClass *self = obj;
    struct kndClassVar *class_var;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    int err;

    err = mempool->new_class_var(mempool, &class_var);
    if (err) return make_gsl_err_external(err);

    class_var->root_class = self;

    *item = class_var;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t class_var_append(void *accu,
                                  void *item)
{
    struct kndClass *self = accu;
    struct kndClassVar *ci = item;

    ci->next = self->baseclass_vars;
    self->baseclass_vars = ci;
    self->num_baseclass_vars++;

    return make_gsl_err(gsl_OK);
}


static gsl_err_t set_class_var_baseclass(void *obj,
                                         const char *id, size_t id_size)
{
    struct kndClassVar *class_var = obj;
    struct kndSet *class_idx;
    struct kndClassEntry *entry;
    void *result;
    int err;

    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    memcpy(class_var->id, id, id_size);
    class_var->id_size = id_size;

    class_idx = class_var->root_class->class_idx;
    err = class_idx->get(class_idx, id, id_size, &result);
    if (err) return make_gsl_err(gsl_FAIL);
    entry = result;

    if (!entry->class) {
        //err = unfreeze_class(class_var->root_class, entry, &entry->class);
        //if (err) return make_gsl_err_external(err);
    }

    class_var->entry = entry;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log("== conc item baseclass: %.*s (id:%.*s) CONC:%p",
                entry->name_size, entry->name, id_size, id, entry->class);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t aggr_item_alloc(void *obj,
                                 const char *name,
                                 size_t name_size,
                                 size_t count,
                                 void **result)
{
    struct kndAttrVar *self = obj;
    struct kndAttrVar *item;
    struct kndMemPool *mempool = self->attr->parent_class->entry->repo->mempool;
    int err;

    if (DEBUG_CLASS_IMPORT_LEVEL_1) {
        knd_log(".. alloc AGGR attr item..  conc id: \"%.*s\" attr:%p  parent:%p",
                name_size, name, self->attr,  self->attr->parent_class);
    }

    err = mempool->new_attr_var(mempool, &item);
    if (err) return make_gsl_err_external(err);

    item->name_size = sprintf(item->name, "%lu",
                              (unsigned long)count);

    item->attr = self->attr;
    item->parent = self;

    *result = item;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t attr_var_append(void *accu,
                                 void *obj)
{
    struct kndAttrVar *self = accu;
    struct kndAttrVar *attr_var = obj;

    if (!self->list_tail) {
        self->list_tail = attr_var;
        self->list = attr_var;
    }
    else {
        self->list_tail->next = attr_var;
        self->list_tail = attr_var;
    }
    self->num_list_elems++;
    attr_var->list_count = self->num_list_elems;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t aggr_item_set_baseclass(void *obj,
                                         const char *id, size_t id_size)
{
    struct kndAttrVar *item = obj;
    struct kndSet *class_idx;
    struct kndClassEntry *entry;
    struct kndClass *conc = item->attr->parent_class;
    void *result;
    int err;

    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE)
        return make_gsl_err(gsl_LIMIT);

    memcpy(item->id, id, id_size);
    item->id_size = id_size;

    class_idx = conc->class_idx;

    err = class_idx->get(class_idx, id, id_size, &result);
    if (err) {
        return make_gsl_err(gsl_FAIL);
    }
    entry = result;

    if (!entry->class) {
        //err = unfreeze_class(conc, entry, &entry->class);
        //if (err) return make_gsl_err_external(err);
    }

    item->class = entry->class;
    item->class_entry = entry;

    return make_gsl_err(gsl_OK);
}


static gsl_err_t aggr_item_parse(void *obj,
                                 const char *rec,
                                 size_t *total_size)
{
    struct kndAttrVar *item = obj;
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = aggr_item_set_baseclass,
          .obj = item
        },
        { .is_validator = true,
          .validate = read_nested_attr_var,
          .obj = item
        }
    };

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. parsing the aggr item: \"%.*s\"", 64, rec);

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}


static gsl_err_t attr_var_alloc(void *obj,
                                 const char *name,
                                 size_t name_size,
                                 size_t count  __attribute__((unused)),
                                 void **result)
{
    struct kndAttrVar *self = obj;
    struct kndAttrVar *attr_var;
    struct kndSet *class_idx;
    struct kndClassEntry *entry;
    struct kndMemPool *mempool = self->attr->parent_class->entry->repo->mempool;
    void *elem;
    int err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2) {
        knd_log(".. alloc attr item conc id: \"%.*s\" attr:%p",
                name_size, name, self->attr);
    }

    class_idx = self->attr->parent_class->class_idx;

    err = class_idx->get(class_idx, name, name_size, &elem);
    if (err) {
        knd_log("-- couldn't resolve class id: \"%.*s\" [size:%zu] :(",
                name_size, name, name_size);
        return make_gsl_err_external(knd_NO_MATCH);
    }

    entry = elem;

    err = mempool->new_attr_var(mempool, &attr_var);
    if (err) return make_gsl_err_external(err);

    attr_var->class_entry = entry;
    attr_var->attr = self->attr;

    *result = attr_var;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t validate_attr_var(void *obj,
                                    const char *name, size_t name_size,
                                    const char *rec, size_t *total_size)
{
    struct kndClassVar *class_var = obj;
    struct kndAttrVar *attr_var;
    struct kndAttr *attr;
    struct kndProc *root_proc;
    struct kndMemPool *mempool = class_var->entry->repo->mempool;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. class var \"%.*s\" to validate attr var: %.*s..",
                class_var->entry->name_size, class_var->entry->name,
                name_size, name);

    if (!class_var->entry->class) {
        knd_log("-- class var not yet resolved: %.*s",
                class_var->entry->name_size, class_var->entry->name);
        return make_gsl_err(gsl_FAIL);
    }

    err = mempool->new_attr_var(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_var->class_var = class_var;

    err = knd_class_get_attr(class_var->entry->class, name, name_size, &attr);
    if (err) {
        knd_log("-- no attr \"%.*s\" in class \"%.*s\"",
                name_size, name,
                class_var->entry->name_size, class_var->entry->name);
        return *total_size = 0, make_gsl_err_external(err);
    }

    attr_var->attr = attr;
    memcpy(attr_var->name, name, name_size);
    attr_var->name_size = name_size;

    struct gslTaskSpec attr_var_spec = {
        .is_list_item = true,
        .accu = attr_var,
        .alloc = attr_var_alloc,
        .append = attr_var_append
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = attr_var->val,
          .buf_size = &attr_var->val_size,
          .max_buf_size = sizeof attr_var->val
        },
        { .is_validator = true,
          .validate = read_nested_attr_var,
          .obj = attr_var
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "r",
          .name_size = strlen("r"),
          .parse = gsl_parse_array,
          .obj = &attr_var_spec
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    switch (attr->type) {
    case KND_ATTR_PROC:

        if (DEBUG_CLASS_IMPORT_LEVEL_2)
            knd_log("== proc attr: %.*s => %.*s",
                    attr_var->name_size, attr_var->name,
                    attr_var->val_size, attr_var->val);

        root_proc = class_var->root_class->proc;
        err = root_proc->get_proc(root_proc,
                                  attr_var->val,
                                  attr_var->val_size, &attr_var->proc);
        if (err) return make_gsl_err_external(err);
        break;
    default:
        break;
    }

    append_attr_var(class_var, attr_var);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t validate_attr_var_list(void *obj,
                                         const char *name, size_t name_size,
                                         const char *rec, size_t *total_size)
{
    struct kndClassVar *class_var = obj;
    struct kndAttrVar *attr_var;
    struct kndAttr *attr;
    struct ooDict *class_name_idx;
    struct kndClassEntry *entry;
    struct kndMemPool *mempool = class_var->entry->repo->mempool;
    int err;

    if (DEBUG_CLASS_IMPORT_LEVEL_1)
        knd_log("\n.. ARRAY: conc item \"%.*s\" to validate list item: %.*s..",
                class_var->entry->name_size, class_var->entry->name,
                name_size, name);

    err = mempool->new_attr_var(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    err = knd_class_get_attr(class_var->root_class, name, name_size, &attr);
    if (err) {
        knd_log("-- no attr \"%.*s\" in class \"%.*s\"",
                name_size, name,
                class_var->entry->name_size, class_var->entry->name);
        return *total_size = 0, make_gsl_err_external(err);
    }

    attr_var->attr = attr;
    attr_var->class_var = class_var;

    memcpy(attr_var->name, name, name_size);
    attr_var->name_size = name_size;

    switch (attr->type) {
    case KND_ATTR_AGGR:
        if (attr->conc) break;

        class_name_idx = class_var->root_class->class_name_idx;
        entry = class_name_idx->get(class_name_idx,
                                    attr->ref_classname,
                                    attr->ref_classname_size);
        if (!entry) {
            knd_log("-- aggr ref not resolved :( no such class: %.*s",
                    attr->ref_classname_size,
                    attr->ref_classname);
            return *total_size = 0, make_gsl_err(gsl_FAIL);
        }

        if (!entry->class) {
            //err = unfreeze_class(class_var->root_class, entry, &entry->class);
            //if (err) return *total_size = 0, make_gsl_err_external(err);
        }
        attr->conc = entry->class;
        break;
    default:
        break;
    }

    struct gslTaskSpec aggr_item_spec = {
        .is_list_item = true,
        .accu = attr_var,
        .alloc = aggr_item_alloc,
        .append = attr_var_append,
        .parse = aggr_item_parse
    };

    append_attr_var(class_var, attr_var);

    return gsl_parse_array(&aggr_item_spec, rec, total_size);
}

static gsl_err_t class_var_read(void *obj,
                                const char *rec, size_t *total_size)
{
    struct kndClassVar *ci = obj;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_var_baseclass,
          .obj = ci
        },
        { .is_validator = true,
          .type = GSL_SET_ARRAY_STATE,
          .validate = validate_attr_var_list,
          .obj = ci
        },
        { .is_validator = true,
          .validate = validate_attr_var,
          .obj = ci
        }
    };

    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size,
                                specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    knd_calc_num_id(ci->id, ci->id_size, &ci->numid);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_var_array(void *obj,
                                       const char *rec,
                                       size_t *total_size)
{
    struct kndClass *self = obj;

    struct gslTaskSpec ci_spec = {
        .is_list_item = true,
        .accu = self,
        .alloc = class_var_alloc,
        .append = class_var_append,
        .parse = class_var_read
    };

    return gsl_parse_array(&ci_spec, rec, total_size);
}

static gsl_err_t import_attr_var(void *obj,
                                 const char *name, size_t name_size,
                                 const char *rec, size_t *total_size)
{
    struct kndClassVar *self = obj;
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool;
    gsl_err_t parser_err;
    int err;

    /* class var not resolved */
    if (!self->entry) {
        knd_log("-- anonymous class var?  REC:%.*s", 64, rec);
        //return *total_size = 0, make_gsl_err_external(knd_FAIL);
    }

    mempool = self->root_class->entry->repo->mempool;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. import attr var: \"%.*s\" REC: %.*s",
                name_size, name, 64, rec);

    err = mempool->new_attr_var(mempool, &attr_var);
    if (err) {
        knd_log("-- attr item mempool exhausted");
        return *total_size = 0, make_gsl_err_external(err);
    }
    attr_var->class_var = self;

    memcpy(attr_var->name, name, name_size);
    attr_var->name_size = name_size;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = attr_var->val,
          .buf_size = &attr_var->val_size,
          .max_buf_size = sizeof attr_var->val
        },
        { .type = GSL_SET_STATE,
          .is_validator = true,
          .validate = import_nested_attr_var,
          .obj = attr_var
        },
        { .is_validator = true,
          .validate = import_nested_attr_var,
          .obj = attr_var
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    append_attr_var(self, attr_var);

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log("++ attr var import OK!");

    return make_gsl_err(gsl_OK);
}


static gsl_err_t import_attr_var_alloc(void *obj,
                                        const char *name __attribute__((unused)),
                                        size_t name_size __attribute__((unused)),
                                        size_t count  __attribute__((unused)),
                                        void **result)
{
    struct kndAttrVar *self = obj;
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = self->class_var->entry->repo->mempool;
    int err;

    err = mempool->new_attr_var(mempool, &attr_var);
    if (err) return make_gsl_err_external(err);
    attr_var->class_var = self->class_var;

    attr_var->is_list_item = true;

    *result = attr_var;
    return make_gsl_err(gsl_OK);
}


static gsl_err_t parse_nested_attr_var(void *obj,
                                        const char *rec,
                                        size_t *total_size)
{
    struct kndAttrVar *item = obj;

    if (DEBUG_CLASS_IMPORT_LEVEL_1)
        knd_log(".. parse import attr item..");

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = item->name,
          .buf_size = &item->name_size,
          .max_buf_size = sizeof item->name
        },
        { .is_validator = true,
          .validate = import_nested_attr_var,
          .obj = item
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t import_attr_var_list(void *obj,
                                       const char *name, size_t name_size,
                                       const char *rec, size_t *total_size)
{
    struct kndClassVar *self = obj;
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_CLASS_IMPORT_LEVEL_1)
        knd_log("== import attr attr_var list: \"%.*s\" REC: %.*s",
                name_size, name, 32, rec);

    err = mempool->new_attr_var(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_var->class_var = self;

    memcpy(attr_var->name, name, name_size);
    attr_var->name_size = name_size;

    append_attr_var(self, attr_var);

    struct gslTaskSpec import_attr_var_spec = {
        .is_list_item = true,
        .accu =   attr_var,
        .alloc =  import_attr_var_alloc,
        .append = attr_var_append,
        .parse =  parse_nested_attr_var
    };

    parser_err = gsl_parse_array(&import_attr_var_spec, rec, total_size);
    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t read_nested_attr_var(void *obj,
                                      const char *name, size_t name_size,
                                      const char *rec, size_t *total_size)
{
    struct kndAttrVar *self = obj;
    struct kndAttrVar *attr_var;
    struct kndAttr *attr;
    struct kndClass *conc = NULL;
    struct ooDict *class_name_idx;
    struct kndClassEntry *entry;
    struct kndMemPool *mempool = self->attr->parent_class->entry->repo->mempool;
    gsl_err_t parser_err;
    int err;

    err = mempool->new_attr_var(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_var->parent = self;
    attr_var->class_var = self->class_var;

    memcpy(attr_var->name, name, name_size);
    attr_var->name_size = name_size;

    if (DEBUG_CLASS_IMPORT_LEVEL_2) {
        knd_log("== reading nested attr item: \"%.*s\" REC: %.*s attr:%p",
                name_size, name, 16, rec, self->attr);
    }

    if (!self->attr->conc) {
        knd_log("-- no conc in attr: \"%.*s\"",
                self->attr->name_size, self->attr->name);
        return *total_size = 0, make_gsl_err_external(knd_FAIL);
    }

    conc = self->attr->conc;

    err = knd_class_get_attr(conc, name, name_size, &attr);
    if (err) {
        knd_log("-- no attr \"%.*s\" in class \"%.*s\" :(",
                name_size, name,
                conc->entry->name_size, conc->entry->name);
        if (err) return *total_size = 0, make_gsl_err_external(err);
    }

    switch (attr->type) {
    case KND_ATTR_AGGR:
        if (attr->conc) break;

        class_name_idx = conc->class_name_idx;
        entry = class_name_idx->get(class_name_idx,
                                  attr->ref_classname,
                                  attr->ref_classname_size);
        if (!entry) {
            knd_log("-- aggr ref not resolved :( no such class: %.*s",
                    attr->ref_classname_size,
                    attr->ref_classname);
            return *total_size = 0, make_gsl_err(gsl_FAIL);
        }

        if (!entry->class) {
            //err = unfreeze_class(conc, entry, &entry->class);
            //if (err) return *total_size = 0, make_gsl_err_external(err);
        }
        attr->conc = entry->class;
        break;
    default:
        break;
    }

    attr_var->attr = attr;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = attr_var->val,
          .buf_size = &attr_var->val_size,
          .max_buf_size = sizeof attr_var->val
        },
        { .is_validator = true,
          .validate = read_nested_attr_var,
          .obj = attr_var
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    attr_var->next = self->children;
    self->children = attr_var;
    self->num_children++;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_attr_var(void *obj __attribute__((unused)),
                                  const char *name __attribute__((unused)),
                                  size_t name_size __attribute__((unused)))
{
    return make_gsl_err(gsl_OK);
}

static gsl_err_t import_nested_attr_var(void *obj,
                                         const char *name, size_t name_size,
                                         const char *rec, size_t *total_size)
{
    struct kndAttrVar *self = obj;
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = self->class_var->root_class->entry->repo->mempool;
    gsl_err_t parser_err;
    int err;

    err = mempool->new_attr_var(mempool, &attr_var);
    if (err) {
        knd_log("-- mempool exhausted: attr item");
        return *total_size = 0, make_gsl_err_external(err);
    }
    attr_var->class_var = self->class_var;

    memcpy(attr_var->name, name, name_size);
    attr_var->name_size = name_size;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. import nested attr: \"%.*s\" REC: %.*s",
                attr_var->name_size, attr_var->name, 16, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = attr_var->val,
          .buf_size = &attr_var->val_size,
          .max_buf_size = sizeof attr_var->val
        },
        { .type = GSL_SET_STATE,
          .is_validator = true,
          .validate = import_nested_attr_var,
          .obj = attr_var
        },
        { .is_validator = true,
          .validate = import_nested_attr_var,
          .obj = attr_var
        },
        { .is_default = true,
          .run = confirm_attr_var,
          .obj = self
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log("++ attr var: \"%.*s\" val:%.*s",
                attr_var->name_size, attr_var->name,
                attr_var->val_size, attr_var->val);

    attr_var->next = self->children;
    self->children = attr_var;
    self->num_children++;

    return make_gsl_err(gsl_OK);
}

static void append_attr_var(struct kndClassVar *ci,
                             struct kndAttrVar *attr_var)
{
    struct kndAttrVar *curr_var;

    for (curr_var = ci->attrs; curr_var; curr_var = curr_var->next) {
        if (curr_var->name_size != attr_var->name_size) continue;
        if (!memcmp(curr_var->name, attr_var->name, attr_var->name_size)) {
            if (!curr_var->list_tail) {
                curr_var->list_tail = attr_var;
                curr_var->list = attr_var;
            }
            else {
                curr_var->list_tail->next = attr_var;
                curr_var->list_tail = attr_var;
            }
            curr_var->num_list_elems++;
            return;
        }
    }

    if (!ci->tail) {
        ci->tail  = attr_var;
        ci->attrs = attr_var;
    }
    else {
        ci->tail->next = attr_var;
        ci->tail = attr_var;
    }
    ci->num_attrs++;
}


extern gsl_err_t parse_class_var(struct kndClassVar *self,
                                 const char *rec,
                                 size_t *total_size)
{
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_var,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .is_validator = true,
          .validate = import_attr_var,
          .obj = self
        },
        { .is_validator = true,
          .validate = import_attr_var,
          .obj = self
        },
        { .is_validator = true,
          .type = GSL_SET_ARRAY_STATE,
          .validate = import_attr_var_list,
          .obj = self
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}

extern gsl_err_t import_class_var(struct kndClassVar *self,
                                  const char *rec,
                                  size_t *total_size)
{
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_var,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .is_validator = true,
          .validate = import_attr_var,
          .obj = self
        },
        { .is_validator = true,
          .validate = import_attr_var,
          .obj = self
        },
        { .is_validator = true,
          .type = GSL_SET_ARRAY_STATE,
          .validate = import_attr_var_list,
          .obj = self
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_baseclass(void *obj,
                                 const char *rec,
                                 size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndClassVar *classvar;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_CLASS_IMPORT_LEVEL_1)
        knd_log(".. parsing the base class: \"%.*s\"", 32, rec);

    err = mempool->new_class_var(mempool, &classvar);
    if (err) {
        knd_log("-- conc item alloc failed :(");
        return *total_size = 0, make_gsl_err_external(err);
    }
    classvar->root_class = self->root_class;

    parser_err = parse_class_var(classvar, rec, total_size);
    if (parser_err.code) return parser_err;

    classvar->next = self->baseclass_vars;
    self->baseclass_vars = classvar;
    self->num_baseclass_vars++;

    return make_gsl_err(gsl_OK);
}



extern gsl_err_t knd_import_class(void *obj,
                                        const char *rec,
                                        size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndClass *c;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. import \"%.*s\" class..", 128, rec);

    err = mempool->new_class(mempool, &c);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    c->root_class = self;

    
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_name,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "base",
          .name_size = strlen("base"),
          .parse = parse_baseclass,
          .obj = c
        },
        { .name = "base",
          .name_size = strlen("base"),
          .parse = parse_baseclass,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "is",
          .name_size = strlen("is"),
          .parse = parse_baseclass,
          .obj = c
        },
        { .name = "is",
          .name_size = strlen("is"),
          .parse = parse_baseclass,
          .obj = c
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .parse = parse_gloss_array,
          .obj = c
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_g",
          .name_size = strlen("_g"),
          .parse = parse_gloss_array,
          .obj = c
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_summary",
          .name_size = strlen("_summary"),
          .parse = parse_summary_array,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "aggr",
          .name_size = strlen("aggr"),
          .parse = parse_aggr,
          .obj = c
        },
        { .name = "aggr",
          .name_size = strlen("aggr"),
          .parse = parse_aggr,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "str",
          .name_size = strlen("str"),
          .parse = parse_str,
          .obj = c
        },
        { .name = "str",
          .name_size = strlen("str"),
          .parse = parse_str,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "bin",
          .name_size = strlen("bin"),
          .parse = parse_bin,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "num",
          .name_size = strlen("num"),
          .parse = parse_num,
          .obj = c
        },
        { .name = "num",
          .name_size = strlen("num"),
          .parse = parse_num,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "ref",
          .name_size = strlen("ref"),
          .parse = parse_ref,
          .obj = c
        },
        { .name = "ref",
          .name_size = strlen("ref"),
          .parse = parse_ref,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "text",
          .name_size = strlen("text"),
          .parse = parse_text,
          .obj = c
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- class parse failed: %d", parser_err.code);
        goto final;
    }

    if (!c->name_size) {
        parser_err = make_gsl_err(gsl_FAIL);
        goto final;
    }

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log("++  \"%.*s\" class import completed!",
                c->name_size, c->name);

    if (!self->batch_mode) {
        c->next = self->inbox;
        self->inbox = c;
        self->inbox_size++;
    }

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        c->str(c);

    return make_gsl_err(gsl_OK);

 final:
    c->del(c);
    return parser_err;
}
