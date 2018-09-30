#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_attr.h"
#include "knd_proc.h"
#include "knd_task.h"
#include "knd_class.h"
#include "knd_text.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_set.h"
#include "knd_mempool.h"

#include <gsl-parser.h>
#include <glb-lib/output.h>

#define DEBUG_ATTR_LEVEL_1 0
#define DEBUG_ATTR_LEVEL_2 0
#define DEBUG_ATTR_LEVEL_3 0
#define DEBUG_ATTR_LEVEL_4 0
#define DEBUG_ATTR_LEVEL_5 0
#define DEBUG_ATTR_LEVEL_TMP 1

static void str(struct kndAttr *self)
{
    struct kndTranslation *tr;
    //struct kndProc *proc;
    const char *type_name = knd_attr_names[self->type];

    if (self->is_a_set)
        knd_log("\n%*s[%s", self->depth * KND_OFFSET_SIZE, "", self->name);
    else
        knd_log("\n%*s{%s %s", self->depth * KND_OFFSET_SIZE, "", type_name, self->name);

    if (self->access_type == KND_ATTR_ACCESS_RESTRICTED) {
        knd_log("%*s  ACL:restricted",
                self->depth * KND_OFFSET_SIZE, "");
    }

    if (self->quant_type == KND_ATTR_SET) {
        knd_log("%*s  QUANT:SET",
                self->depth * KND_OFFSET_SIZE, "");
    }

    if (self->concise_level) {
        knd_log("%*s  CONCISE:%zu",
                self->depth * KND_OFFSET_SIZE, "", self->concise_level);
    }
    if (self->is_implied) {
        knd_log("%*s  (implied)",
                self->depth * KND_OFFSET_SIZE, "");
    }

    tr = self->tr;
    while (tr) {
        knd_log("%*s   ~ %s %s",
                self->depth * KND_OFFSET_SIZE, "", tr->locale, tr->val);
        tr = tr->next;
    }

    if (self->ref_classname_size) {
        knd_log("%*s  REF class template: %.*s",
                self->depth * KND_OFFSET_SIZE, "",
                self->ref_classname_size, self->ref_classname);
    }

    /*if (self->proc) {
        proc = self->proc;
        knd_log("%*s  PROC: %.*s",
                self->depth * KND_OFFSET_SIZE, "", proc->name_size, proc->name);
        proc->depth = self->depth + 1;
        proc->str(proc);
    }
    */
    /*if (self->calc_oper_size) {
        knd_log("%*s  oper: %s attr: %s",
                self->depth * KND_OFFSET_SIZE, "",
                self->calc_oper, self->calc_attr);
    }
    */
    if (self->idx_name_size) {
        knd_log("%*s  idx: %s",
                self->depth * KND_OFFSET_SIZE, "", self->idx_name);
    }

    /*if (self->default_val_size) {
        knd_log("%*s  default VAL: %s",
                self->depth * KND_OFFSET_SIZE, "", self->default_val);
    }
    */

    if (self->is_a_set)
        knd_log("%*s]", self->depth * KND_OFFSET_SIZE, "");
    else
        knd_log("%*s}",  self->depth * KND_OFFSET_SIZE, "");
}

static int export_JSON(struct kndAttr *self)
{
    struct kndTask *task = self->parent_class->entry->repo->task;
    struct glbOutput *out = self->parent_class->entry->repo->out;
    struct kndTranslation *tr;
    struct kndProc *p;
    const char *type_name = knd_attr_names[self->type];
    size_t type_name_size = strlen(knd_attr_names[self->type]);
    int err;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. JSON export attr: \"%.*s\"..",
                self->name_size, self->name);

    err = out->writec(out, '"');
    if (err) return err;
    err = out->write(out, self->name, self->name_size);
    if (err) return err;
    err = out->write(out, "\":{", strlen("\":{"));
    if (err) return err;

    err = out->write(out, "\"type\":\"", strlen("\"type\":\""));
    if (err) return err;
    err = out->write(out, type_name, type_name_size);
    if (err) return err;
    err = out->writec(out, '"');
    if (err) return err;

    if (self->is_a_set) {
        err = out->write(out, ",\"is_a_set\":true", strlen(",\"is_a_set\":true"));
        if (err) return err;
    }

    if (self->ref_classname_size) {
        err = out->write(out, ",\"refclass\":\"", strlen(",\"refclass\":\""));
        if (err) return err;
        err = out->write(out, self->ref_classname, self->ref_classname_size);
        if (err) return err;
        err = out->write(out, "\"", 1);
        if (err) return err;
   }

    /* choose gloss */
    tr = self->tr;
    while (tr) {
        if (DEBUG_ATTR_LEVEL_2)
            knd_log("LANG: %s == CURR LOCALE: %s [%lu] => %s",
                    tr->locale, task->locale,
                    (unsigned long)task->locale_size, tr->val);

        if (strncmp(task->locale, tr->locale, tr->locale_size)) {
            goto next_tr;
        }

        err = out->write(out,
                         ",\"_gloss\":\"", strlen(",\"_gloss\":\""));
        if (err) return err;

        err = out->write(out, tr->val,  tr->val_size);
        if (err) return err;

        err = out->write(out, "\"", 1);
        if (err) return err;
        break;

    next_tr:
        tr = tr->next;
    }

    if (self->proc) {
        err = out->write(out, ",\"proc\":", strlen(",\"proc\":"));
        if (err) return err;
        p = self->proc;
        p->depth = 0;
        p->max_depth = KND_MAX_DEPTH;
        err = p->export(p);
        if (err) return err;
    }

    err = out->writec(out, '}');
    if (err) return err;

    return knd_OK;
}

static int export_GSP(struct kndAttr *self, struct glbOutput *out)
{
    char buf[KND_NAME_SIZE] = {0};
    size_t buf_size = 0;
    struct kndTranslation *tr;

    const char *type_name = knd_attr_names[self->type];
    size_t type_name_size = strlen(knd_attr_names[self->type]);
    int err;

    err = out->write(out, "{", 1);
    if (err) return err;
    err = out->write(out, type_name, type_name_size);
    if (err) return err;
    err = out->write(out, " ", 1);
    if (err) return err;
    err = out->write(out, self->name, self->name_size);
    if (err) return err;

    if (self->is_a_set) {
        err = out->write(out, "{t set}", strlen("{t set}"));
        if (err) return err;
    }

    if (self->is_implied) {
        err = out->write(out, "{impl}", strlen("{impl}"));
        if (err) return err;
    }

    if (self->is_indexed) {
        err = out->write(out, "{idx}", strlen("{idx}"));
        if (err) return err;
    }

    if (self->concise_level) {
        buf_size = sprintf(buf, "%zu", self->concise_level);
        err = out->write(out, "{concise ", strlen("{concise "));
        if (err) return err;
        err = out->write(out, buf, buf_size);
        if (err) return err;
        err = out->writec(out, '}');
        if (err) return err;
    }

    if (self->ref_classname_size) {
        err = out->write(out, "{c ", strlen("{c "));
        if (err) return err;
        err = out->write(out, self->ref_classname, self->ref_classname_size);
        if (err) return err;
        err = out->write(out, "}", 1);
        if (err) return err;
    }

    if (self->ref_procname_size) {
        err = out->write(out, "{p ", strlen("{p "));
        if (err) return err;
        err = out->write(out, self->ref_procname, self->ref_procname_size);
        if (err) return err;
        err = out->write(out, "}", 1);
        if (err) return err;
    }

    /* choose gloss */
    if (self->tr) {
        err = out->write(out,
                         "[_g", strlen("[_g"));
        if (err) return err;
    }

    for (tr = self->tr; tr; tr = tr->next) {
        err = out->write(out, "{", 1);
        if (err) return err;
        err = out->write(out, tr->locale,  tr->locale_size);
        if (err) return err;
        err = out->write(out, "{t ", 3);
        if (err) return err;
        err = out->write(out, tr->val,  tr->val_size);
        if (err) return err;
        err = out->write(out, "}}", 2);
        if (err) return err;
    }
    if (self->tr) {
        err = out->write(out, "]", 1);
        if (err) return err;
    }

    err = out->write(out, "}", 1);
    if (err) return err;

    return knd_OK;
}

extern int knd_attr_export(struct kndAttr *self,
                           knd_format format, struct glbOutput *out)
{
    switch (format) {
    case KND_FORMAT_JSON: return export_JSON(self);
    case KND_FORMAT_GSP:  return export_GSP(self, out);
    default:              return knd_NO_MATCH;
    }
}

/**
 * PARSER
 */

static gsl_err_t run_set_quant(void *obj, const char *name, size_t name_size)
{
    struct kndAttr *self = (struct kndAttr*)obj;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. run set quant!\n");

    if (!name_size) return make_gsl_err(gsl_FAIL);
    if (name_size >= KND_SHORT_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    if (!memcmp("set", name, name_size)) {
        self->quant_type = KND_ATTR_SET;
        self->is_a_set = true;
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_idx(void *obj,
                             const char *name __attribute__((unused)),
                             size_t name_size __attribute__((unused)))
{
    struct kndAttr *self = obj;

    if (DEBUG_ATTR_LEVEL_1)
        knd_log(".. confirm IDX");
    self->is_indexed = true;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_implied(void *obj,
                                 const char *name __attribute__((unused)),
                                 size_t name_size __attribute__((unused)))
{
    struct kndAttr *self = obj;
    self->is_implied = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t alloc_gloss_item(void *obj,
                                  const char *name,
                                  size_t name_size,
                                  size_t count,
                                  void **item)
{
    struct kndAttr *self = obj;
    struct kndTranslation *tr;

    assert(name == NULL && name_size == 0);

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. %.*s: allocate gloss translation,  count: %zu",
                self->name_size, self->name, count);

    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return make_gsl_err_external(knd_NOMEM);

    memset(tr, 0, sizeof(struct kndTranslation));

    *item = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t append_gloss_item(void *accu,
                                   void *item)
{
    struct kndAttr *self = accu;
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

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. read gloss translation: \"%.*s\",  text: \"%.*s\"",
                tr->locale_size, tr->locale, tr->val_size, tr->val);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_gloss(void *obj,
                             const char *rec,
                             size_t *total_size)
{
    struct kndAttr *self = obj;
    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .alloc =        alloc_gloss_item,
        .append =       append_gloss_item,
        .accu =         self,
        .parse =        parse_gloss_item
    };

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. %.*s: reading gloss",
                self->name_size, self->name);

    return gsl_parse_array(&item_spec, rec, total_size);
}

static gsl_err_t parse_quant_type(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndAttr *self = obj;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_quant,
          .obj = self
        }, // TODO
        { .type = GSL_SET_STATE,
          .name = "uniq",
          .name_size = strlen("uniq"),
          .run = run_set_quant,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_proc(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndAttr *self = obj;
    struct kndProc *proc;
    struct kndProcEntry *entry;
    struct kndMemPool *mempool;
    int err;
    gsl_err_t parser_err;

    mempool = self->parent_class->entry->repo->mempool;
    err = knd_proc_new(mempool, &proc);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    err = knd_proc_entry_new(mempool, &entry);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    entry->repo = self->parent_class->entry->repo;
    entry->proc = proc;
    proc->entry = entry;

    parser_err = proc->read(proc, rec, total_size);
    if (parser_err.code) return parser_err;

    self->proc = proc;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_set_access_control(void *obj, const char *name, size_t name_size)
{
    struct kndAttr *self = (struct kndAttr*)obj;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. run set ACL..");

    if (!name_size) return make_gsl_err(gsl_FAIL);
    if (name_size >= KND_SHORT_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    if (!strncmp(name, "restrict", strlen("restrict"))) {
        self->access_type = KND_ATTR_ACCESS_RESTRICTED;
        if (DEBUG_ATTR_LEVEL_2)
            knd_log("** NB: restricted attr: %.*s!",
                    self->name_size, self->name);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_access_control(void *obj,
                                      const char *rec,
                                      size_t *total_size)
{
    struct kndAttr *self = (struct kndAttr*)obj;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. parsing ACL: \"%s\"", rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_access_control,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}


static gsl_err_t set_attr_name(void *obj, const char *name, size_t name_size)
{
    struct kndAttr *self = obj;

    if (!name_size) return make_gsl_err(gsl_FAIL);
    self->name = name;
    self->name_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_ref_class(void *obj, const char *name, size_t name_size)
{
    struct kndAttr *self = obj;
    if (!name_size) return make_gsl_err(gsl_FAIL);
    self->ref_classname = name;
    self->ref_classname_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_GSL(struct kndAttr *self,
                           const char *rec,
                           size_t *total_size)
{
    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. attr parsing: \"%.*s\"..", 32, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_name,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .parse = parse_gloss,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_g",
          .name_size = strlen("_g"),
          .parse = parse_gloss,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "c",
          .name_size = strlen("c"),
          .run = set_ref_class,
          .obj = self
        },
        { .name = "c",
          .name_size = strlen("c"),
          .run = set_ref_class,
          .obj = self
        }/*,
        { .type = GSL_SET_STATE,
          .name = "p",
          .name_size = strlen("p"),
          .buf = self->ref_procname,
          .buf_size = &self->ref_procname_size,
          .max_buf_size = sizeof self->ref_procname,
        },
        { .name = "p",
          .name_size = strlen("p"),
          .buf = self->ref_procname,
          .buf_size = &self->ref_procname_size,
          .max_buf_size = sizeof self->ref_procname,
          }*/,
        { .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "t",
          .name_size = strlen("t"),
          .parse = parse_quant_type,
          .obj = self
        },
        { .name = "t",
          .name_size = strlen("t"),
          .parse = parse_quant_type,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "acl",
          .name_size = strlen("acl"),
          .parse = parse_access_control,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "idx",
          .name_size = strlen("idx"),
          .run = confirm_idx,
          .obj = self
        },
        { .name = "idx",
          .name_size = strlen("idx"),
          .run = confirm_idx,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "impl",
          .name_size = strlen("impl"),
          .run = confirm_implied,
          .obj = self
        },
        { .name = "impl",
          .name_size = strlen("impl"),
          .run = confirm_implied,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "concise",
          .name_size = strlen("concise"),
          .parse = gsl_parse_size_t,
          .obj = &self->concise_level
        },
        { .name = "concise",
          .name_size = strlen("concise"),
          .parse = gsl_parse_size_t,
          .obj = &self->concise_level
        }/*,
        { .type = GSL_SET_STATE,
          .name = "validate",
          .name_size = strlen("validate"),
          .parse = parse_validator,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "val",
          .name_size = strlen("val"),
          .buf = self->default_val,
          .buf_size = &self->default_val_size,
          .max_buf_size = sizeof(self->default_val)
          }*/
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    if (self->type == KND_ATTR_INNER) {
        if (!self->ref_classname_size) {
            knd_log("-- ref class not specified in %.*s",
                    self->name_size, self->name);
            return make_gsl_err_external(knd_FAIL);
        }
    }

    // TODO: reject attr names starting with an underscore _

    return make_gsl_err(gsl_OK);
}

extern int knd_apply_attr_var_updates(struct kndClass *self,
                                      struct kndClassUpdate *class_update)
{
    struct kndState *state; //, *s, *next_state = NULL;
    //struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    int err;

    if (DEBUG_ATTR_LEVEL_TMP)
        knd_log(".. applying attr var updates..");

    err = knd_state_new(mempool, &state);
    if (err) return err;

    state->update = class_update->update;
    
    //for (s = self->attr_var_inbox; s; s = next_state) {
        //attr_var = s->val;

        /*knd_log("== attr var %.*s => %.*s",
                attr_var->name_size, attr_var->name,
                s->val_size, s->val); */

        //attr_var->val = s->val;
        //attr_var->val_size = s->val_size;

        //next_state = s->next;
        //s->next = attr_var->states;
        //attr_var->states = s;
        //attr_var->num_states++;
    //}

    state->next = self->states;
    self->states = state;
    self->num_states++;
    state->numid = self->num_states;
    state->phase = KND_UPDATED;

    return knd_OK;
}

extern void kndAttr_init(struct kndAttr *self)
{
    memset(self, 0, sizeof(struct kndAttr));
    self->str = str;
    self->parse = parse_GSL;
}

extern int kndAttr_new(struct kndAttr **c)
{
    struct kndAttr *self;

    self = malloc(sizeof(struct kndAttr));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndAttr));

    kndAttr_init(self);

    *c = self;

    return knd_OK;
}

extern int knd_copy_attr_ref(void *obj,
                             const char *elem_id __attribute__((unused)),
                             size_t elem_id_size __attribute__((unused)),
                             size_t count __attribute__((unused)),
                             void *elem)
{
    struct kndSet     *attr_idx = obj;
    struct kndAttrRef *src_ref = elem;
    struct kndAttr    *attr    = src_ref->attr;
    struct kndAttrRef *ref;
    struct kndMemPool *mempool = attr_idx->mempool;
    int err;

    if (DEBUG_ATTR_LEVEL_2) 
        knd_log(".. copying %.*s attr..", attr->name_size, attr->name);

    err = knd_attr_ref_new(mempool, &ref);                                        RET_ERR();
    ref->attr = attr;
    ref->attr_var = src_ref->attr_var;
    ref->class_entry = src_ref->class_entry;

    err = attr_idx->add(attr_idx,
                        attr->id, attr->id_size,
                        (void*)ref);                                              RET_ERR();

    return knd_OK;
}

extern int knd_register_attr_ref(void *obj,
                                 const char *elem_id __attribute__((unused)),
                                 size_t elem_id_size __attribute__((unused)),
                                 size_t count __attribute__((unused)),
                                 void *elem)
{
    struct kndClass     *self = obj;
    struct kndSet *attr_idx  = self->attr_idx;
    struct ooDict *attr_name_idx = self->entry->repo->attr_name_idx;
    struct kndAttrRef *src_ref = elem;
    struct kndAttr    *attr    = src_ref->attr;
    struct kndAttrRef *ref, *prev_attr_ref;
    struct kndMemPool *mempool = attr_idx->mempool;
    int err;

    if (DEBUG_ATTR_LEVEL_2) 
        knd_log(".. copying %.*s attr..", attr->name_size, attr->name);

    err = knd_attr_ref_new(mempool, &ref);                                        RET_ERR();
    ref->attr = attr;
    ref->attr_var = src_ref->attr_var;
    ref->class_entry = self->entry;

    err = attr_idx->add(attr_idx,
                        attr->id, attr->id_size,
                        (void*)ref);                                              RET_ERR();

    prev_attr_ref = attr_name_idx->get(attr_name_idx,
                                       attr->name, attr->name_size);
    ref->next = prev_attr_ref;
    err = attr_name_idx->set(attr_name_idx,
                             attr->name, attr->name_size,
                             (void*)ref);                                         RET_ERR();
    return knd_OK;
}

extern int knd_attr_var_new(struct kndMemPool *mempool,
                            struct kndAttrVar **result)
{
    void *page;
    int err;
    //knd_log("..attr var new [size:%zu]", sizeof(struct kndAttr));
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL_X2,
                            sizeof(struct kndAttrVar), &page);
    if (err) return err;

    // TEST
    //mempool->num_attr_vars++;

    *result = page;
    return knd_OK;
}

extern int knd_attr_ref_new(struct kndMemPool *mempool,
                            struct kndAttrRef **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                            sizeof(struct kndAttrRef), &page);
    if (err) return err;
    *result = page;
    return knd_OK;
}

extern int knd_attr_new(struct kndMemPool *mempool,
                        struct kndAttr **result)
{
    void *page;
    int err;
    //knd_log("..attr new [size:%zu]", sizeof(struct kndAttr));
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL_X2,
                            sizeof(struct kndAttr), &page);
    if (err) return err;
    *result = page;
    kndAttr_init(*result);
    return knd_OK;
}
