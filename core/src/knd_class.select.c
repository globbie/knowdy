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

    c->entry->class = c;
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
    struct kndRepo *repo = self->entry->repo;
    struct glbOutput *log = self->entry->repo->log;
    struct kndTask *task = self->entry->repo->task;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);
    log->reset(log);

    c = repo->curr_attr_ref->class_entry->class;

    if (DEBUG_CLASS_SELECT_LEVEL_TMP) {
        knd_log("\n== select by attr value: \"%.*s\" of \"%.*s\" (repo:%.*s)",
                name_size, name,
                c->name_size, c->name,
                c->entry->repo->name_size, c->entry->repo->name);
    }

    if (!c->entry->descendants) {
        knd_log("-- no descendants idx in \"%.*s\" :(",
                c->name_size, c->name);
        return make_gsl_err(gsl_FAIL);
    }
    set = c->entry->descendants;

    err = knd_set_get_facet(set, repo->curr_attr, &facet);
    if (err) {
        log->writef(log, "no such facet: \"%.*s\"", name_size, name);
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
    set = result;
    set->base->class = c;
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

static gsl_err_t run_set_attr_var(void *obj,
                                  const char *val, size_t val_size)
{
    struct kndClass *self = obj;
    struct kndRepo *repo = self->entry->repo;
    struct kndClass *c = repo->curr_class;
    struct kndAttr *attr;
    struct kndAttrVar *attr_var;
    struct glbOutput *log = repo->log;
    struct kndTask *task = repo->task;
    struct kndMemPool *mempool = repo->mempool;
    struct kndState *state;
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

    attr_var = repo->curr_attr_var;

    if (!attr_var) {
        knd_log(".. set attr \"%.*s\" with value %.*s",
                attr->name_size, attr->name, val_size, val);
        return make_gsl_err(gsl_OK);
    }

    if (DEBUG_CLASS_SELECT_LEVEL_TMP) {
        knd_log(".. updating attr var %.*s with value: \"%.*s\"",
                attr_var->name_size, attr_var->name, val_size, val);
    }

    err = knd_state_new(mempool, &state);
    if (err) return make_gsl_err_external(err);
    state->phase = KND_UPDATED;

    state->val = (void*)attr_var;
    // TODO
    //state->val = (void*)val;
    //state->val_size = val_size;
    //state->next = self->attr_var_inbox;

    //c->attr_var_inbox = state;
    //c->attr_var_inbox_size++;
    task->type = KND_UPDATE_STATE;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t present_attr_var_selection(void *obj,
                                            const char *val __attribute__((unused)),
                                            size_t val_size __attribute__((unused)))
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
                repo->curr_class->name_size, repo->curr_class->name);

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
    struct glbOutput *log = self->entry->repo->log;
    struct kndTask *task = self->entry->repo->task;
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_attr_var,
          .obj = self
        },
        { .is_default = true,
          .run = present_attr_var_selection,
          .obj = self
        }
    };
    int err, e;

    if (!repo->curr_class) return *total_size = 0, make_gsl_err_external(knd_FAIL);

    err = knd_class_get_attr(repo->curr_class, name, name_size, &attr_ref);
    if (err) {
        knd_log("-- no attr \"%.*s\" in class \"%.*s\"",
                name_size, name,
                repo->curr_class->name_size,
                repo->curr_class->name);
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

    // TODO
    /*err = knd_get_attr_var(repo->curr_class, name, name_size, &attr_var);
    if (!err) {
        knd_log(".. \"%.*s\" attr var exists!", name_size, name);
        repo->curr_attr_var = attr_var;
        }*/

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
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
                                         const char *val __attribute__((unused)),
                                         size_t val_size __attribute__((unused)))
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
            knd_log(".. batch selection: batch size: %zu   start from: %zu",
                    task->batch_max, task->batch_from);

        /* no sets found? */
        if (!task->num_sets) {
            if (repo->curr_baseclass && repo->curr_baseclass->entry->descendants) {
                set = repo->curr_baseclass->entry->descendants;

                // TODO
                err = knd_class_export_set_JSON(self, out, set);
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
            err = knd_class_export_set_JSON(self, out, set);
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

static gsl_err_t run_select_class_state(void *obj,
                                        const char *name __attribute__((unused)),
                                        size_t name_size __attribute__((unused)))
{
    struct kndClass *root_class = obj;
    struct kndClass *self = root_class->entry->repo->curr_class;
    struct glbOutput *out = self->entry->repo->out;
    bool in_list = false;
    int err;

    if (!self) return make_gsl_err_external(knd_FAIL);

    if (DEBUG_CLASS_SELECT_LEVEL_TMP)
        knd_log(".. present state of class \"%.*s\"..",
                self->name_size, self->name);

    err = out->writec(out, '{');
    if (err) return make_gsl_err_external(err);

    if (self->num_states) {
        //err = export_class_state_JSON(self);
        //if (err) return make_gsl_err_external(err);
        in_list = true;
    }

    if (self->num_inst_states) {
        if (in_list) {
            err = out->writec(out, ',');
            if (err) return make_gsl_err_external(err);
        }
        err = out->write(out, "\"instances\":{", strlen("\"instances\":{"));

        err = out->write(out, "\"_total\":",
                         strlen("\"_total\":"));
        if (err) return make_gsl_err_external(err);

        //err = out->writef(out, "%zu", self->num_insts);
        //if (err) return make_gsl_err_external(err);

        err = out->writec(out, ',');
        if (err) return make_gsl_err_external(err);
        
        //err = export_class_inst_state_JSON(self);
        //if (err) return make_gsl_err_external(err);

        err = out->writec(out, '}');
        if (err) return make_gsl_err_external(err);
    }

    err = out->writec(out, '}');
    if (err) return make_gsl_err_external(err);
    
    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_get_class(void *obj, const char *name, size_t name_size)
{
    struct kndClass *self = obj;
    struct kndRepo *repo = self->entry->repo;
    struct kndClass *c;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    repo->curr_class = NULL;
    err = knd_get_class(repo, name, name_size, &c);
    if (err) return make_gsl_err_external(err);

    c->inst_state_refs = NULL;
    repo->curr_class = c;

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
    knd_num_to_str((size_t)numval, buf, &buf_size, KND_RADIX_BASE);

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
    struct kndMemPool *mempool = repo->mempool;
    struct kndTask *task =   repo->task;
    struct kndState *state;
    int err;

    if (!repo->curr_class) {
        knd_log("-- remove operation: class name not specified");

        log->reset(log);
        err = log->write(log, name, name_size);
        if (err) return make_gsl_err_external(err);
        err = log->write(log, " class name not specified",
                         strlen(" class name not specified"));
        if (err) return make_gsl_err_external(err);
        return make_gsl_err(gsl_NO_MATCH);
    }
    
    c = repo->curr_class;

    if (DEBUG_CLASS_SELECT_LEVEL_2)
        knd_log(".. removing class: \"%.*s\"\n",
                c->name_size, c->name);

    err = knd_state_new(mempool, &state);
    if (err) return make_gsl_err_external(err);

    state->next = c->states;
    c->states = state;
    c->num_states++;
    state->numid = c->num_states;
    state->phase = KND_REMOVED;

    // TODO: inform parents and dependants

    task->type = KND_UPDATE_STATE;

    log->reset(log);
    err = log->write(log, name, name_size);
    if (err) return make_gsl_err_external(err);
    err = log->write(log, " class removed",
                     strlen(" class removed"));
    if (err) return make_gsl_err_external(err);

    /*    c->next = self->inbox;
    self->inbox = c;
    self->inbox_size++;
    */

    return make_gsl_err(gsl_OK);
}


static int knd_select_class_delta(struct kndClass *self,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndTask *task = self->entry->repo->task;
    struct glbOutput *log = self->entry->repo->log;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    struct kndState *state;
    struct kndSet *set;
    int e, err;
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .name = "eq",
          .name_size = strlen("eq"),
          .parse = gsl_parse_size_t,
          .obj = &task->batch_eq
        },
        { .name = "gt",
          .name_size = strlen("gt"),
          .parse = gsl_parse_size_t,
          .obj = &task->batch_gt
        },
        { .name = "lt",
          .name_size = strlen("lt"),
          .parse = gsl_parse_size_t,
          .obj = &task->batch_lt
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    if (DEBUG_CLASS_SELECT_LEVEL_TMP)
        knd_log(".. select %.*s class delta:  gt %zu  lt %zu ..",
                self->name_size, self->name,
                task->batch_gt, task->batch_lt);

    task->type = KND_SELECT_STATE;
    if (!self->num_states) {
        log->reset(log);
        e = log->write(log, "no states available",
                       strlen("no states available"));
        if (e) return e;
        task->http_code = HTTP_BAD_REQUEST;
        return knd_FAIL;
    }

    if (task->state_lt && task->state_lt < task->state_gt) {
        log->reset(log);
        e = log->write(log, "invalid state range given",
                       strlen("invalid state range given"));
        if (e) return e;
        task->http_code = HTTP_BAD_REQUEST;
        return knd_FAIL;
    }

    if (task->state_gt >= self->num_states) {
        log->reset(log);
        e = log->write(log, "invalid state range given",
                       strlen("invalid state range given"));
        if (e) return e;
        task->http_code = HTTP_BAD_REQUEST;
        return knd_FAIL;
    }

    err = knd_set_new(mempool, &set);                                        MEMPOOL_ERR(kndSet);
    set->mempool = mempool;
    set->type = KND_SET_STATE_UPDATE;

    if (!task->state_lt)
        task->state_lt = self->init_state + self->states->update->numid + 1;
    
    for (state = self->states; state; state = state->next) {
        if (task->state_lt <= state->numid) continue;
        if (task->state_gt >= state->numid) continue;

        // TODO
        //err = set->add(set, state->id, state->id_size,
        //               (void*)state);                                          RET_ERR();
    }

    task->sets[0] = set;
    task->num_sets = 1;
    //task->curr_inst = self;
    return knd_OK;
}

extern gsl_err_t parse_class_delta(void *data,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndClass *self = data;
    struct kndRepo *repo = self->entry->repo;
    int err;

    if (!repo->curr_class) return make_gsl_err(gsl_FAIL);

    err = knd_select_class_delta(repo->curr_class, rec, total_size);
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
                                      size_t eq __attribute__((unused)),
                                      struct kndSet *set)
{
    struct kndState *state;
    struct kndStateRef *ref;
    int err;

    if (DEBUG_CLASS_SELECT_LEVEL_TMP)
        knd_log(".. class %.*s (repo:%.*s) to extract updates",
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

extern gsl_err_t knd_class_select(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndClass *c;
    struct kndRepo *repo = self->entry->repo;
    struct glbOutput *log = repo->log;
    struct kndTask *task = repo->task;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CLASS_SELECT_LEVEL_TMP)
        knd_log(".. parsing class select rec: \"%.*s\"", 32, rec);

    self->depth = 0;
    //self->selected_state_numid = 0;
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
        }/*,
        { .name = "_state",
          .name_size = strlen("_state"),
          .parse = gsl_parse_size_t,
          .obj = &self->selected_state_numid
          }*/,
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
          .run = run_select_class_state,
          .obj = self
        },
        { .name = "_delta",
          .name_size = strlen("_delta"),
          .is_selector = true,
          .parse = parse_class_delta,
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
        /*if (repo->curr_class) {
            c = repo->curr_class;
            c->reset_inbox(c, true);
            }*/
        return parser_err;
    }

    if (!repo->curr_class) return make_gsl_err(gsl_OK);

    /* any updates happened? */
    c = repo->curr_class;
    if (c->inst_state_refs) {
        err = knd_register_inst_states(c);
        if (err) return make_gsl_err_external(err);

        task->type = KND_UPDATE_STATE;

        c->inst_state_refs = NULL;
        return make_gsl_err(gsl_OK);
    }

    return make_gsl_err(gsl_OK);
}

