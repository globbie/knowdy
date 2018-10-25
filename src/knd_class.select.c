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

#define DEBUG_CLASS_SELECT_LEVEL_1 0
#define DEBUG_CLASS_SELECT_LEVEL_2 0
#define DEBUG_CLASS_SELECT_LEVEL_3 0
#define DEBUG_CLASS_SELECT_LEVEL_4 0
#define DEBUG_CLASS_SELECT_LEVEL_5 0
#define DEBUG_CLASS_SELECT_LEVEL_TMP 1

static gsl_err_t select_by_baseclass(void *obj,
                                     const char *name, size_t name_size)
{
    struct kndClass *self = obj;
    struct kndClass *c;
    struct kndRepo *repo = self->entry->repo;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    err = knd_get_class(repo, name, name_size, &c);
    if (err) return make_gsl_err_external(err);

    if (DEBUG_CLASS_SELECT_LEVEL_2)
        c->str(c);

    if (!c->entry->class) {
        knd_log(".. resolve class to %.*s", c->name_size, c->name);
        c->entry->class = c;
    }

    if (!c->entry->descendants) {
        knd_log("-- no set of descendants found :(");
        return make_gsl_err(gsl_OK);
    }

    repo->curr_baseclass = c;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t select_by_attr(void *obj,
                                const char *name, size_t name_size)
{
    struct kndClass *self = obj;
    struct kndClass *c;
    struct kndSet *set;
    void *result;
    const char *class_id;
    size_t class_id_size;
    struct kndFacet *facet;
    struct kndRepo *repo =  self->entry->repo;
    struct glbOutput *log = repo->log;
    struct kndTask *task =  repo->task;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);
    log->reset(log);

    // TODO
    c = repo->curr_attr_ref->class_entry->class;

    if (DEBUG_CLASS_SELECT_LEVEL_2) {
        knd_log("\n\n== select by attr value: \"%.*s\" of \"%.*s\" (id:%.*s repo:%.*s)",
                name_size, name,
                c->name_size, c->name,
                c->entry->id_size, c->entry->id,
                c->entry->repo->name_size, c->entry->repo->name);
        c->str(c);
    }

    if (!c->entry->descendants) {
        knd_log("-- no descendants idx in \"%.*s\" :(",
                c->name_size, c->name);
        return make_gsl_err(gsl_FAIL);
    }
    set = c->entry->descendants;

    err = knd_set_get_facet(set, repo->curr_attr, &facet);
    if (err) {
        log->writef(log, "no such facet: %.*s", name_size, name);
        task->error = knd_NO_MATCH;
        task->http_code = HTTP_NOT_FOUND;
        return make_gsl_err_external(knd_NO_MATCH);
    }

    err = knd_get_class(self->entry->repo, name, name_size, &c);
    if (err) {
        log->writef(log, "-- no such class: %.*s", name_size, name);
        task->http_code = HTTP_NOT_FOUND;
        return make_gsl_err_external(err);
    }

    class_id = c->entry->id;
    class_id_size = c->entry->id_size;

    err = facet->set_idx->get(facet->set_idx,
                              class_id, class_id_size, &result);
    if (err) {
        if (c->entry->orig) {
            class_id = c->entry->orig->id;
            class_id_size = c->entry->orig->id_size;
        }
        err = facet->set_idx->get(facet->set_idx,
                                  class_id, class_id_size, &result);
        if (err) {
            log->writef(log, "no such facet class: %.*s", name_size, name);
            task->http_code = HTTP_NOT_FOUND;
            return make_gsl_err(gsl_FAIL);
        }
    }

    set = result;

    if (task->num_sets + 1 > KND_MAX_CLAUSES)
        return make_gsl_err(gsl_LIMIT);

    task->sets[task->num_sets] = set;
    task->num_sets++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_attr_select(void *obj,
                                   const char *name, size_t name_size,
                                   const char *rec, size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndAttrRef *attr_ref;
    struct kndAttr *attr;
    struct kndRepo *repo = self->entry->repo;
    struct glbOutput *log = repo->log;
    struct kndTask *task =  repo->task;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = select_by_attr,
          .obj = self
        }
    };
    int err;

    if (!repo->curr_baseclass)
        return *total_size = 0, make_gsl_err_external(knd_FAIL);

    err = knd_class_get_attr(repo->curr_baseclass, name, name_size, &attr_ref);
    if (err) {
        knd_log("-- no attr \"%.*s\" in class \"%.*s\"",
                name_size, name,
                repo->curr_baseclass->name_size,
                repo->curr_baseclass->name);
        log->writef(log, ": no such attribute: %.*s", name_size, name);
        task->http_code = HTTP_NOT_FOUND;
        return *total_size = 0, make_gsl_err_external(err);
    }
    attr = attr_ref->attr;

    if (DEBUG_CLASS_SELECT_LEVEL_2) {
        knd_log(".. select by attr \"%.*s\"..", name_size, name);
        knd_log(".. attr parent: %.*s conc: %.*s",
                attr->parent_class->name_size,
                attr->parent_class->name,
                attr->ref_class->name_size,
                attr->ref_class->name);
    }
    repo->curr_attr_ref = attr_ref;
    repo->curr_attr = attr;

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t set_curr_state(void *obj,
                                const char *val, size_t val_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndTask *task = obj;
    long numval;
    int err;

    if (!val_size) return make_gsl_err_external(knd_FAIL);
    if (val_size >= KND_NAME_SIZE) return make_gsl_err_external(knd_LIMIT);

    memcpy(buf, val, val_size);
    buf_size = val_size;
    buf[buf_size] = '\0';

    err = knd_parse_num(buf, &numval);
    if (err) return make_gsl_err_external(err);

    // TODO: check integer

    task->state_eq = (size_t)numval;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_set_attr_var(void *obj,
                                  const char *val, size_t val_size)
{
    struct kndClass *self = obj;
    struct kndRepo *repo = self->entry->repo;
    struct kndAttr *attr;
    struct kndAttrRef *attr_ref;
    struct kndAttrVar *attr_var;
    struct glbOutput *log = repo->log;
    struct kndTask *task = repo->task;
    struct kndMemPool *mempool = repo->mempool;
    struct kndSet *attr_idx = self->attr_idx;
    struct kndState *state;
    struct kndStateRef *state_ref;
    struct kndStateVal *state_val;
    void *elem;
    int err, e;

    if (!val_size) return make_gsl_err(gsl_FORMAT);
    if (val_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    attr = repo->curr_attr;
    if (!attr) {
        log->reset(log);
        e = log->write(log, "-- no attr selected",
                               strlen("-- no attr selected"));
        if (e) return make_gsl_err_external(e);
        task->http_code = HTTP_BAD_REQUEST;
        return make_gsl_err_external(knd_FAIL);
    }

    /*  attr var exists? */
    err = attr_idx->get(attr_idx, attr->id, attr->id_size, &elem);
    if (err) {
        knd_log("-- no attr \"%.*s\" in local attr idx?",
                attr->name_size, attr->name);
        return make_gsl_err_external(err);
    }

    attr_ref = elem;
    attr_var = attr_ref->attr_var;

    if (DEBUG_CLASS_SELECT_LEVEL_TMP) {
        knd_log(".. updating attr var %.*s with value: \"%.*s\"",
                attr_var->name_size, attr_var->name, val_size, val);
    }

    err = knd_state_new(mempool, &state);
    if (err) return make_gsl_err_external(err);
    state->phase = KND_UPDATED;

    err = knd_state_ref_new(mempool, &state_ref);
    if (err) {
        knd_log("-- state ref alloc failed");
        return make_gsl_err_external(err);
    }
    state_ref->state = state;
    state_ref->type = KND_STATE_ATTR_VAR;

    err = knd_state_val_new(mempool, &state_val);
    if (err) {
        knd_log("-- state val alloc failed");
        return make_gsl_err_external(err);
    }

    state_val->obj = (void*)attr_var;
    state_val->val      = val;
    state_val->val_size = val_size;
    state->val = state_val;

    attr_var->val = val;
    attr_var->val_size = val_size;

    state->next = attr_var->states;
    attr_var->states = state;
    attr_var->num_states++;
    state->numid = attr_var->num_states;

    task->type = KND_UPDATE_STATE;

    /* inform parent class */
    state_ref->next = repo->curr_class_state_refs;
    repo->curr_class_state_refs = state_ref;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t present_attr_var_selection(void *obj,
                                            const char *unused_var(val),
                                            size_t unused_var(val_size))
{
    struct kndClass *self = obj;
    struct kndAttr *attr;
    struct kndAttrVar *attr_var;
    struct kndRepo *repo = self->entry->repo;
    struct glbOutput *out = self->entry->repo->out;
    struct kndTask *task = self->entry->repo->task;
    int err;

    if (DEBUG_CLASS_SELECT_LEVEL_2)
        knd_log(".. presenting attrs of class \"%.*s\"..",
                self->name_size, self->name);

    out->reset(out);

    if (!repo->curr_attr) {
        knd_log("-- no attr to present");
        return make_gsl_err_external(knd_FAIL);
    }
    attr = repo->curr_attr;

    if (repo->curr_attr_var) {
        attr_var = repo->curr_attr_var;

        err = out->writec(out, '{');
        if (err) return make_gsl_err_external(err);

        err = knd_attr_var_export_JSON(attr_var, out);
        if (err) return make_gsl_err_external(err);
        
        err = out->writec(out, '}');
        if (err) return make_gsl_err_external(err);
        
        return make_gsl_err(gsl_OK);
    }

    // TODO
    err = out->writec(out, '{');
    if (err) return make_gsl_err_external(err);
    
    err = knd_attr_export(attr, task->format, out);
    if (err) {
        knd_log("-- attr export failed");
        return make_gsl_err_external(err);
    }

    err = out->writec(out, '}');
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_attr_var_select(void *obj,
                                       const char *name, size_t name_size,
                                       const char *rec, size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndAttrRef *attr_ref;
    struct kndAttr *attr;
    struct kndRepo *repo = self->entry->repo;
    struct glbOutput *log = repo->log;
    struct kndTask *task = repo->task;
    struct kndClass *c;
    int err, e;

    if (!repo->curr_class) return *total_size = 0, make_gsl_err_external(knd_FAIL);
    c = repo->curr_class;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_attr_var,
          .obj = c
        },
        { .is_default = true,
          .run = present_attr_var_selection,
          .obj = c
        }
    };

    err = knd_class_get_attr(c, name, name_size, &attr_ref);
    if (err) {
        knd_log("-- no attr \"%.*s\" in class \"%.*s\"",
                name_size, name,
                c->name_size,
                c->name);
        log->reset(log);
        e = log->write(log, name, name_size);
        if (e) return *total_size = 0, make_gsl_err_external(e);
        e = log->write(log, ": no such attribute",
                               strlen(": no such attribute"));
        if (e) return *total_size = 0, make_gsl_err_external(e);
        task->http_code = HTTP_NOT_FOUND;
        return *total_size = 0, make_gsl_err_external(err);
    }

    attr = attr_ref->attr;
    repo->curr_attr = attr;

    if (DEBUG_CLASS_SELECT_LEVEL_TMP) {
        knd_log("++ attr selected: \"%.*s\"..", name_size, name);
    }

    if (attr->is_a_set) {
        knd_log(".. parsing array selection..");
    }
    
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t rels_presentation(void *obj,
                                   const char *unused_var(val), size_t unused_var(val_size))
{
    struct kndTask *self = obj;
    self->show_rels = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_baseclass_select(void *obj,
                                        const char *rec,
                                        size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndTask *task = self->entry->repo->task;
    gsl_err_t err;

    if (DEBUG_CLASS_SELECT_LEVEL_2)
        knd_log(".. select by baseclass \"%.*s\"..", 64, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = select_by_baseclass,
          .obj = self
        },
        { .name = "_batch",
          .name_size = strlen("_batch"),
          .parse = gsl_parse_size_t,
          .obj = &task->batch_max
        },
        { .name = "_from",
          .name_size = strlen("_from"),
          .parse = gsl_parse_size_t,
          .obj = &task->batch_from
        },
        {  .name = "_depth",
           .name_size = strlen("_depth"),
           .parse = gsl_parse_size_t,
           .obj = &self->max_depth
        },
        {  .name = "_rels",
           .name_size = strlen("_rels"),
           .is_selector = true,
           .run = rels_presentation,
           .obj = task
        },
        { .is_validator = true,
          .validate = parse_attr_select,
          .obj = self
        }
    };

    task->type = KND_SELECT_STATE;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    if (task->batch_max > KND_RESULT_MAX_BATCH_SIZE) {
        knd_log("-- batch size exceeded: %zu (max limit: %d) :(",
                task->batch_max, KND_RESULT_MAX_BATCH_SIZE);
        return make_gsl_err(gsl_LIMIT);
    }

   task->start_from = task->batch_max * task->batch_from;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t present_class_selection(void *obj,
                                         const char *unused_var(val),
                                         size_t unused_var(val_size))
{
    struct kndClass *self = obj;
    struct kndClass *c;
    struct kndSet *set;
    struct kndRepo *repo = self->entry->repo;
    struct glbOutput *out = repo->out;
    struct kndMemPool *mempool = repo->mempool;
    struct kndTask *task = repo->task;
    int err;

    if (DEBUG_CLASS_SELECT_LEVEL_2)
        knd_log(".. presenting class \"%.*s\"..",
                self->entry->name_size, self->entry->name);

    out->reset(out);
    
    if (task->type == KND_SELECT_STATE) {

        if (DEBUG_CLASS_SELECT_LEVEL_TMP)
            knd_log(".. batch selection: batch size: %zu   start from: %zu  max depth:%zu",
                    task->batch_max, task->batch_from, self->max_depth);

        /* no sets found? */
        if (!task->num_sets) {
            if (repo->curr_baseclass && repo->curr_baseclass->entry->descendants) {
                set = repo->curr_baseclass->entry->descendants;

                // TODO
                err = knd_class_export_set_JSON(self, set, out);
                if (err) return make_gsl_err_external(err);

                return make_gsl_err(gsl_OK);
            }

            err = out->write(out, "{}", strlen("{}"));
            if (err) return make_gsl_err_external(err);
            return make_gsl_err(gsl_OK);
        }

        /* TODO: intersection cache lookup  */
        set = task->sets[0];

        /* intersection required */
        if (task->num_sets > 1) {
            err = knd_set_new(mempool, &set);
            if (err) return make_gsl_err_external(err);

            set->type = KND_SET_CLASS;
            set->mempool = mempool;
            set->base = task->sets[0]->base;

            err = knd_set_intersect(set, task->sets, task->num_sets);
            if (err) return make_gsl_err_external(err);
        }

        if (!set->num_elems) {
            knd_log("== empty set? %p", set);
            err = out->write(out, "{}", strlen("{}"));
            if (err) return make_gsl_err_external(err);
            return make_gsl_err(gsl_OK);
        }

        /* final presentation in JSON 
           TODO: choose output format */

        switch (set->type) {
        case KND_SET_STATE_UPDATE:
            knd_log(".. export state set..");
            err = out->write(out, "{}", strlen("{}"));
            if (err) return make_gsl_err_external(err);
            break;
        default:
            err = knd_class_export_set_JSON(self, set, out);
            if (err) return make_gsl_err_external(err);
        }

        return make_gsl_err(gsl_OK);
    }

    if (!repo->curr_class) {
        knd_log("-- no class to present :(");
        return make_gsl_err_external(knd_FAIL);
    }

    c = repo->curr_class;

    c->depth = 0;
    c->max_depth = 1;
    if (self->max_depth) {
        c->max_depth = self->max_depth;
    }

    err = knd_class_export(c, KND_FORMAT_JSON, out);
    if (err) {
        knd_log("-- class export failed");
        return make_gsl_err_external(err);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_get_class(void *obj, const char *name, size_t name_size)
{
    struct kndClass *self = obj;
    struct kndRepo *repo = self->entry->repo;
    struct kndClass *c;
    int err;

    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    repo->curr_class = NULL;
    err = knd_get_class(repo, name, name_size, &c);
    if (err) return make_gsl_err_external(err);

    repo->curr_class = c;
    repo->curr_class_state_refs = NULL;
    repo->curr_class_inst_state_refs = NULL;

    if (DEBUG_CLASS_SELECT_LEVEL_2) {
        c->str(c);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_get_class_by_numid(void *obj, const char *id, size_t id_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    struct kndClass *self = obj;
    struct kndClass *c;
    struct kndClassEntry *entry;
    struct kndRepo *repo = self->entry->repo;
    struct kndSet *class_idx = repo->class_idx;
    void *result;
    long numval;
    int err;

    if (id_size >= KND_NAME_SIZE)
        return make_gsl_err(gsl_FAIL);

    memcpy(buf, id, id_size);
    buf[id_size] = '0';

    err = knd_parse_num((const char*)buf, &numval);
    if (err) return make_gsl_err_external(err);

    if (numval <= 0) return make_gsl_err(gsl_FAIL);

    buf_size = 0;

    knd_uid_create((size_t)numval, buf, &buf_size);

    if (DEBUG_CLASS_SELECT_LEVEL_2)
        knd_log("ID: %zu => \"%.*s\" [size: %zu]",
                numval, buf_size, buf, buf_size);

    err = class_idx->get(class_idx, buf, buf_size, &result);
    if (err) return make_gsl_err(gsl_FAIL);
    entry = result;

    repo->curr_class = NULL;
    err = knd_get_class(repo, entry->name, entry->name_size, &c);
    if (err) return make_gsl_err_external(err);

    repo->curr_class = c;

    if (DEBUG_CLASS_SELECT_LEVEL_2) {
        c->str(c);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_remove_class(void *obj, const char *name, size_t name_size)
{
    struct kndClass *self = obj;
    struct kndClass *c;
    struct kndRepo *repo = self->entry->repo;
    struct glbOutput *log = repo->log;
    struct kndTask *task =   repo->task;
    size_t num_children;
    int err;

    if (!repo->curr_class) {
        knd_log("-- remove operation: class name not specified");
        log->reset(log);
        err = log->write(log, name, name_size);
        if (err) return make_gsl_err_external(err);
        err = log->write(log, " class name not specified",
                         strlen(" class name not specified"));
        if (err) return make_gsl_err_external(err);
        task->http_code = HTTP_BAD_REQUEST;
        return make_gsl_err(gsl_NO_MATCH);
    }
    
    c = repo->curr_class;

    /* check dependants */
    num_children = c->entry->num_children;
    if (c->desc_states && c->desc_states->val)
        num_children = c->desc_states->val->val_size;

    if (num_children) {
        knd_log("-- remove operation: descendants exist");
        log->reset(log);
        err = log->write(log, name, name_size);
        if (err) return make_gsl_err_external(err);
        err = log->write(log, "class conflict: descendants exist",
                         strlen("class conflict: descendants exist"));
        if (err) return make_gsl_err_external(err);
        task->http_code = HTTP_CONFLICT;
        return make_gsl_err(gsl_FAIL);
    }

    if (c->entry->num_insts) {
        knd_log("-- remove operation: instances exist");
        log->reset(log);
        err = log->write(log, name, name_size);
        if (err) return make_gsl_err_external(err);
        err = log->write(log, "class conflict: instances exist",
                         strlen("class conflict: instances exist"));
        if (err) return make_gsl_err_external(err);
        task->http_code = HTTP_CONFLICT;
        return make_gsl_err(gsl_FAIL);
    }

    // TODO: copy-on-write : add special entry
    //         for deleted classes from base repo


    task->type = KND_UPDATE_STATE;
    task->phase = KND_REMOVED;

    if (DEBUG_CLASS_SELECT_LEVEL_TMP)
        knd_log("NB: class removed: \"%.*s\"\n",
                c->name_size, c->name);

    log->reset(log);
    err = log->write(log, name, name_size);
    if (err) return make_gsl_err_external(err);
    err = log->write(log, " class removed",
                     strlen(" class removed"));
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t present_desc_state(void *obj,
                                    const char *unused_var(name),
                                    size_t unused_var(name_size))
{
    struct kndClass *self = obj;
    struct kndTask *task = self->entry->repo->task;
    struct glbOutput *out = self->entry->repo->out;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    struct kndSet *set;
    struct kndState *latest_state;
    int err;

    task->type = KND_SELECT_STATE;

    if (!self->desc_states)                                     goto show_curr_state;
    latest_state = self->desc_states;
    if (task->state_gt >= latest_state->numid)             goto show_curr_state;
    if (task->state_lt && task->state_lt < task->state_gt) goto show_curr_state;

    if (DEBUG_CLASS_SELECT_LEVEL_TMP) {
        knd_log(".. select class descendants update delta:  gt %zu  lt %zu  eq:%zu..",
                task->state_gt, task->state_lt, task->state_eq);
    }

    err = knd_set_new(mempool, &set);
    if (err) return make_gsl_err_external(err);
    set->mempool = mempool;

    err = knd_class_get_desc_updates(self,
                                     task->state_gt, task->state_lt,
                                     task->state_eq, set);
    if (err) return make_gsl_err_external(err);
    task->show_removed_objs = true;

    err =  knd_class_export_set_JSON(self, set, out);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);

 show_curr_state:
    err = out->writec(out, '{');
    if (err) return make_gsl_err_external(err);

    err = knd_export_class_state_JSON(self, out);
    if (err) return make_gsl_err_external(err);

    err = out->writec(out, '}');
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t present_state(void *obj,
                               const char *unused_var(name),
                               size_t unused_var(name_size))
{
    struct kndClass *self = obj;
    struct kndTask *task = self->entry->repo->task;
    struct glbOutput *out = self->entry->repo->out;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    struct kndSet *set;
    struct kndState *latest_state;
    int err;

    task->type = KND_SELECT_STATE;

    if (!self->states)                                     goto show_curr_state;
    latest_state = self->states;
    if (task->state_gt >= latest_state->numid)             goto show_curr_state;
    if (task->state_lt && task->state_lt < task->state_gt) goto show_curr_state;

    if (DEBUG_CLASS_SELECT_LEVEL_TMP) {
        knd_log(".. select class delta:  gt %zu  lt %zu  eq:%zu..",
                task->state_gt, task->state_lt, task->state_eq);
    }

    err = knd_set_new(mempool, &set);
    if (err) return make_gsl_err_external(err);
    set->mempool = mempool;

    err = knd_class_get_updates(self,
                                task->state_gt, task->state_lt,
                                task->state_eq, set);
    if (err) return make_gsl_err_external(err);
    task->show_removed_objs = true;

    err =  knd_class_export_set_JSON(self, set, out);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);

 show_curr_state:
    err = out->writec(out, '{');
    if (err) return make_gsl_err_external(err);

    err = knd_export_class_state_JSON(self, out);
    if (err) return make_gsl_err_external(err);

    err = out->writec(out, '}');
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static int select_class_desc_state(struct kndClass *self,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndRepo *repo = self->entry->repo;
    struct kndTask *task = repo->task;
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = set_curr_state,
          .obj = task
        },
        { .name = "gt",
          .name_size = strlen("gt"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_gt
        },
        { .name = "gte",
          .name_size = strlen("gte"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_gte
        },
        { .name = "lt",
          .name_size = strlen("lt"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_lt
        },
        { .name = "lte",
          .name_size = strlen("lte"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_lte
        },
        { .is_default = true,
          .run = present_desc_state,
          .obj = self
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    return knd_OK;
}

static gsl_err_t parse_class_desc_state(void *data,
                                        const char *rec,
                                        size_t *total_size)
{
    struct kndClass *self = data;
    int err;

    err = select_class_desc_state(self, rec, total_size);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static int select_class_state(struct kndClass *self,
                              const char *rec,
                              size_t *total_size)
{
    struct kndRepo *repo = self->entry->repo;
    struct kndTask *task = repo->task;
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = set_curr_state,
          .obj = task
        },
        { .name = "gt",
          .name_size = strlen("gt"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_gt
        },
        { .name = "gte",
          .name_size = strlen("gte"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_gte
        },
        { .name = "lt",
          .name_size = strlen("lt"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_lt
        },
        { .name = "lte",
          .name_size = strlen("lte"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_lte
        },
        { .name = "desc",
          .name_size = strlen("desc"),
          .parse = parse_class_desc_state,
          .obj = self
        },
        { .is_default = true,
          .run = present_state,
          .obj = self
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    return knd_OK;
}

static gsl_err_t parse_class_state(void *data,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndClass *self = data;
    struct kndRepo *repo = self->entry->repo;
    int err;

    if (!repo->curr_class) return make_gsl_err(gsl_FAIL);

    err = select_class_state(repo->curr_class, rec, total_size);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_select_class_inst(void *data,
                                         const char *rec,
                                         size_t *total_size)
{
    struct kndClass *self = data;
    struct kndRepo *repo = self->entry->repo;
    struct kndClassInst *root_inst;
    gsl_err_t parser_err;

    if (!repo->curr_class)  {
        knd_log("-- no baseclass selected");
        return make_gsl_err_external(knd_FAIL);
    }

    root_inst = repo->root_inst;
    root_inst->base = repo->curr_class;

    parser_err = knd_parse_select_inst(root_inst, rec, total_size);
    if (parser_err.code) return parser_err;
   
    return make_gsl_err(gsl_OK);
}

static int retrieve_inst_updates(struct kndStateRef *ref,
                                 struct kndSet *set)
{
    struct kndState *state = ref->state;
    struct kndClassInstEntry *inst_entry;
    struct kndStateRef *child_ref;
    int err;
    
    knd_log("++ state: %zu  type:%d", state->numid, ref->type);

    for (child_ref = state->children; child_ref; child_ref = child_ref->next) {
        err = retrieve_inst_updates(child_ref, set);                          RET_ERR();
    }

    switch (ref->type) {
    case KND_STATE_CLASS_INST:
        inst_entry = ref->obj;

        if (DEBUG_CLASS_SELECT_LEVEL_TMP) {
            knd_log("** inst id:%.*s", inst_entry->id_size, inst_entry->id);
        }

        err = set->add(set,
                       inst_entry->id,
                       inst_entry->id_size, (void*)inst_entry);                   RET_ERR();

        /* TODO: filter out the insts 
           that were created and removed _after_ 
           the requested update _gt */

        break;
    default:
        break;
    }


    

    return knd_OK;
}

extern int knd_class_get_inst_updates(struct kndClass *self,
                                      size_t gt, size_t lt,
                                      size_t unused_var(eq),
                                      struct kndSet *set)
{
    struct kndState *state;
    struct kndStateRef *ref;
    int err;

    if (DEBUG_CLASS_SELECT_LEVEL_2)
        knd_log(".. class %.*s (repo:%.*s) to extract instance updates",
                self->name_size, self->name,
                self->entry->repo->name_size, self->entry->repo->name);

    if (!lt) lt = self->num_inst_states + 1;

    for (state = self->inst_states; state; state = state->next) {
        if (state->numid >= lt) continue;
        if (state->numid <= gt) continue;

        // TODO
        if (!state->children) continue;

        for (ref = state->children; ref; ref = ref->next) {
            err = retrieve_inst_updates(ref, set);                    RET_ERR();
        }
    }

    return knd_OK;
}

static int retrieve_class_updates(struct kndStateRef *ref,
                                  struct kndSet *set)
{
    struct kndState *state = ref->state;
    struct kndClassEntry *entry;
    struct kndStateRef *child_ref;
    int err;
    
    knd_log("++ class state: %zu  type:%d", state->numid, ref->type);

    for (child_ref = state->children; child_ref; child_ref = child_ref->next) {
        err = retrieve_class_updates(child_ref, set);                             RET_ERR();
    }

    switch (ref->type) {
    case KND_STATE_CLASS:
        entry = ref->obj;

        if (!entry) {
            knd_log("-- no class ref in state ref");
            return knd_OK;
        }

        if (DEBUG_CLASS_SELECT_LEVEL_TMP) {
            knd_log("** class:%.*s", entry->name_size, entry->name);
        }

        err = set->add(set,
                       entry->id,
                       entry->id_size, (void*)entry);                   RET_ERR();

        /* TODO: filter out the insts
           that were created and removed _after_ 
           the requested update _gt */

        break;
    default:
        break;
    }

    return knd_OK;
}

extern int knd_class_get_updates(struct kndClass *self,
                                 size_t gt, size_t lt,
                                 size_t unused_var(eq),
                                 struct kndSet *set)
{
    struct kndState *state;
    struct kndStateRef *ref;
    int err;

    if (DEBUG_CLASS_SELECT_LEVEL_2)
        knd_log(".. class %.*s (repo:%.*s) to extract updates..",
                self->name_size, self->name,
                self->entry->repo->name_size, self->entry->repo->name);

    if (!lt) lt = self->states->numid + 1;

    for (state = self->states; state; state = state->next) {
        if (state->numid >= lt) continue;
        if (state->numid <= gt) continue;

        // TODO
        if (!state->children) continue;
        for (ref = state->children; ref; ref = ref->next) {
            err = retrieve_class_updates(ref, set);                               RET_ERR();
        }
    }

    return knd_OK;
}

extern int knd_class_get_desc_updates(struct kndClass *self,
                                      size_t gt, size_t lt,
                                      size_t unused_var(eq),
                                      struct kndSet *set)
{
    struct kndState *state;
    struct kndStateRef *ref;
    int err;

    if (DEBUG_CLASS_SELECT_LEVEL_2)
        knd_log(".. class %.*s (repo:%.*s) to extract descendant updates..",
                self->name_size, self->name,
                self->entry->repo->name_size, self->entry->repo->name);

    if (!lt) lt = self->desc_states->numid + 1;
    for (state = self->desc_states; state; state = state->next) {
        if (state->numid >= lt) continue;
        if (state->numid <= gt) continue;

        // TODO
        if (!state->children) continue;
        for (ref = state->children; ref; ref = ref->next) {
            err = retrieve_class_updates(ref, set);                               RET_ERR();
        }
    }

    return knd_OK;
}

extern gsl_err_t knd_class_select(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndRepo *repo = self->entry->repo;
    struct glbOutput *log = repo->log;
    struct kndTask   *task = repo->task;
    knd_state_phase phase;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CLASS_SELECT_LEVEL_TMP)
        knd_log(".. parsing class select rec: \"%.*s\"", 32, rec);

    self->depth = 0;
    self->max_depth = 0;
    repo->curr_class = NULL;
    repo->curr_baseclass = NULL;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = run_get_class,
          .obj = self
        },
        { .name = "_id",
          .name_size = strlen("_id"),
          .is_selector = true,
          .run = run_get_class_by_numid,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "_rm",
          .name_size = strlen("_rm"),
          .run = run_remove_class,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "instance",
          .name_size = strlen("instance"),
          .parse = knd_parse_import_class_inst,
          .obj = self
        },
        { .name = "instance",
          .name_size = strlen("instance"),
          .parse = parse_select_class_inst,
          .obj = self
        }, /* shortcuts */
        { .type = GSL_SET_STATE,
          .name = "inst",
          .name_size = strlen("inst"),
          .parse = knd_parse_import_class_inst,
          .obj = self
        },
        { .name = "inst",
          .name_size = strlen("inst"),
          .parse = parse_select_class_inst,
          .obj = self
        },
        { .name = "_is",
          .name_size = strlen("_is"),
          .is_selector = true,
          .parse = parse_baseclass_select,
          .obj = self
        },
        {  .name = "_depth",
           .name_size = strlen("_depth"),
           .parse = gsl_parse_size_t,
           .obj = &self->max_depth
        },
        { .name = "_state",
          .name_size = strlen("_state"),
          .parse = parse_class_state,
          .obj = self
        },
        { .is_validator = true,
          .validate = parse_attr_var_select,
          .obj = self
        },
        { .is_default = true,
          .run = present_class_selection,
          .obj = self
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        if (DEBUG_CLASS_SELECT_LEVEL_TMP) {
            knd_log("-- class select error %d: \"%.*s\"",
                    parser_err.code, log->buf_size, log->buf);
        }
        if (!log->buf_size) {
            err = log->write(log, "class parse failure",
                                 strlen("class parse failure"));
            if (err) return make_gsl_err_external(err);
        }

        /* TODO: release resources */
        return parser_err;
    }

    if (!repo->curr_class) return make_gsl_err(gsl_OK);

    /* any updates happened? */
    switch (task->type) {
    case KND_UPDATE_STATE:
        phase = KND_UPDATED;
        if (task->phase == KND_REMOVED)
            phase = KND_REMOVED;
        err = knd_update_state(repo->curr_class, phase);
        if (err) return make_gsl_err_external(err);
        break;
    default:
        break;
    }
    
    return make_gsl_err(gsl_OK);
}

