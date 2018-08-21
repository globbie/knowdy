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


static gsl_err_t parse_select_inst(void *data,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndClass *self = data;
    struct kndClassInst *obj = self->curr_inst;
    struct kndTask *task = self->curr_class->entry->repo->task;
    gsl_err_t err;

    if (!self->curr_class) {
        knd_log("-- base class not set :(");
        /* TODO: log*/
        return *total_size = 0, make_gsl_err(gsl_FAIL);
    }

    if (DEBUG_CLASS_SELECT_LEVEL_2)
        knd_log(".. select \"%.*s\" obj.. task type: %d",
                16, rec, task->type);

    task->type = KND_GET_STATE;
    obj->base = self->curr_class;

    err = obj->select(obj, rec, total_size);
    if (err.code) return err;

    if (DEBUG_CLASS_SELECT_LEVEL_2) {
        if (obj->curr_inst)
            obj->curr_inst->str(obj->curr_inst);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t select_by_baseclass(void *obj,
                                     const char *name, size_t name_size)
{
    struct kndClass *self = obj;
    struct kndTask *task = self->entry->repo->task;
    struct kndClass *c;
    struct kndSet *set;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    err = knd_get_class(self, name, name_size, &c);
    if (err) return make_gsl_err_external(err);

    if (DEBUG_CLASS_SELECT_LEVEL_2)
        c->str(c);

    c->entry->class = c;
    if (!c->entry->descendants) {
        knd_log("-- no set of descendants found :(");
        return make_gsl_err(gsl_OK);
    }

    set = c->entry->descendants;
    if (task->num_sets + 1 > KND_MAX_CLAUSES)
        return make_gsl_err(gsl_LIMIT);
    task->sets[task->num_sets] = set;
    task->num_sets++;

    self->curr_baseclass = c;

    return make_gsl_err(gsl_OK);
}


static gsl_err_t select_by_attr(void *obj,
                                const char *name, size_t name_size)
{
    struct kndClass *self = obj;
    struct kndClass *c;
    struct kndSet *set;
    struct kndFacet *facet;
    struct glbOutput *log = self->entry->repo->log;
    struct kndTask *task = self->entry->repo->task;
    int err, e;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    c = self->curr_attr->parent_class;

    if (DEBUG_CLASS_SELECT_LEVEL_2) {
        knd_log("== select by attr value: \"%.*s\" of \"%.*s\" resolved:%d",
                name_size, name, c->name_size, c->name, c->is_resolved);
    }

    if (!c->entry->descendants) {
        knd_log("-- no descendants idx in \"%.*s\" :(",
                c->name_size, c->name);
        return make_gsl_err(gsl_FAIL);
    }

    set = c->entry->descendants;

    err = set->get_facet(set, self->curr_attr, &facet);
    if (err) {
        knd_log("-- no such facet: \"%.*s\" :(",
                self->curr_attr->name_size, self->curr_attr->name);
        log->reset(log);
        e = log->write(log, name, name_size);
        if (e) return make_gsl_err_external(e);

        e = log->write(log, " no such facet: ",
                               strlen(" no such facet: "));
        if (e) return make_gsl_err_external(e);
        task->http_code = HTTP_NOT_FOUND;
        return make_gsl_err_external(knd_NO_MATCH);
    }

    set = facet->set_name_idx->get(facet->set_name_idx,
                                   name, name_size);
    if (!set) return make_gsl_err(gsl_FAIL);

    err = knd_get_class(self, name, name_size, &set->base->class);
    if (err) {
        knd_log("-- no such class: %.*s", name_size, name);
        return make_gsl_err_external(err);
    }

    if (DEBUG_CLASS_SELECT_LEVEL_2)
        set->str(set, 1);

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
    struct kndAttr *attr;
    struct glbOutput *log = self->entry->repo->log;
    struct kndTask *task = self->entry->repo->task;
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = select_by_attr,
          .obj = self
        }
    };
    int err, e;

    if (!self->curr_baseclass) return *total_size = 0, make_gsl_err_external(knd_FAIL);

    err = knd_class_get_attr(self->curr_baseclass, name, name_size, &attr);
    if (err) {
        knd_log("-- no attr \"%.*s\" in class \"%.*s\"",
                name_size, name,
                self->curr_baseclass->name_size,
                self->curr_baseclass->name);
        log->reset(log);
        e = log->write(log, name, name_size);
        if (e) return *total_size = 0, make_gsl_err_external(e);

        e = log->write(log, ": no such attribute",
                               strlen(": no such attribute"));
        if (e) return *total_size = 0, make_gsl_err_external(e);
        task->http_code = HTTP_NOT_FOUND;

        return *total_size = 0, make_gsl_err_external(err);
    }

    if (DEBUG_CLASS_SELECT_LEVEL_2) {
        knd_log(".. select by attr \"%.*s\"..", name_size, name);
        knd_log(".. attr parent: %.*s conc: %.*s",
                attr->parent_class->name_size,
                attr->parent_class->name,
                attr->conc->name_size,
                attr->conc->name);
    }

    self->curr_attr = attr;

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
        knd_log(".. select by baseclass \"%.*s\"..", 16, rec);

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
    struct glbOutput *out = self->entry->repo->out;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    struct kndTask *task = self->entry->repo->task;
    int err;

    if (DEBUG_CLASS_SELECT_LEVEL_2)
        knd_log(".. presenting class \"%.*s\"..",
                self->entry->name_size, self->entry->name);

    out->reset(out);


    
    if (task->type == KND_SELECT_STATE) {
        if (DEBUG_CLASS_SELECT_LEVEL_2)
            knd_log(".. batch selection: batch size: %zu   start from: %zu",
                    task->batch_max, task->batch_from);

        /* no sets found? */
        if (!task->num_sets) {
            if (self->curr_baseclass && self->curr_baseclass->entry->descendants) {
                set = self->curr_baseclass->entry->descendants;

                // TODO
                //err = export_set_JSON(self, set);
                //if (err) return make_gsl_err_external(err);

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
            err = mempool->new_set(mempool, &set);
            if (err) return make_gsl_err_external(err);

            set->type = KND_SET_CLASS;
            set->mempool = mempool;

            // TODO: compound class
            set->base = task->sets[0]->base;

            err = set->intersect(set, task->sets, task->num_sets);
            if (err) return make_gsl_err_external(err);
        }

        if (!set->num_elems) {
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
            break;
            //err = export_set_JSON(self, set);
            //if (err) return make_gsl_err_external(err);
        }

        return make_gsl_err(gsl_OK);
    }

    if (!self->curr_class) {
        knd_log("-- no class to present :(");
        return make_gsl_err_external(knd_FAIL);
    }

    c = self->curr_class;

    c->depth = 0;
    c->max_depth = 1;
    if (self->max_depth) {
        c->max_depth = self->max_depth;
    }

    err = c->export(c);
    if (err) {
        knd_log("-- class export failed");
        return make_gsl_err_external(err);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_select_class_state(void *obj, const char *name, size_t name_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    struct kndClass *root_class = obj;
    struct kndClass *self = root_class->curr_class;
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

        err = out->writef(out, "%zu", self->num_objs);
        if (err) return make_gsl_err_external(err);

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
    struct kndClass *c;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    self->curr_class = NULL;
    err = knd_get_class(self, name, name_size, &c);
    if (err) return make_gsl_err_external(err);

    self->curr_class = c;

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

    err = self->class_idx->get(self->class_idx, buf, buf_size, &result);
    if (err) return make_gsl_err(gsl_FAIL);
    entry = result;

    self->curr_class = NULL;
    err = knd_get_class(self, entry->name, entry->name_size, &c);
    if (err) return make_gsl_err_external(err);

    self->curr_class = c;

    if (DEBUG_CLASS_SELECT_LEVEL_2) {
        c->str(c);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_remove_class(void *obj, const char *name, size_t name_size)
{
    struct kndClass *self = obj;
    struct kndClass *c;
    struct glbOutput *log = self->entry->repo->log;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    struct kndTask *task = self->entry->repo->task;
    struct kndState *state;
    int err;

    if (!self->curr_class) {
        knd_log("-- remove operation: class name not specified");

        log->reset(log);
        err = log->write(log, name, name_size);
        if (err) return make_gsl_err_external(err);
        err = log->write(log, " class name not specified",
                         strlen(" class name not specified"));
        if (err) return make_gsl_err_external(err);
        return make_gsl_err(gsl_NO_MATCH);
    }
    
    c = self->curr_class;

    if (DEBUG_CLASS_SELECT_LEVEL_2)
        knd_log(".. removing class: \"%.*s\"\n",
                c->name_size, c->name);

    err = mempool->new_state(mempool, &state);
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

    c->next = self->inbox;
    self->inbox = c;
    self->inbox_size++;

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
    struct kndClassUpdate *class_update;
    struct kndClass *c;
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

    err = mempool->new_set(mempool, &set);                                        MEMPOOL_ERR(kndSet);
    set->mempool = mempool;
    set->type = KND_SET_STATE_UPDATE;

    if (!task->state_lt)
        task->state_lt = self->init_state + self->states->update->numid + 1;
    
    for (state = self->states; state; state = state->next) {
        if (task->state_lt <= state->numid) continue;
        if (task->state_gt >= state->numid) continue;
        
        err = set->add(set, state->id, state->id_size,
                       (void*)state);                                          RET_ERR();
    }

    task->sets[0] = set;
    task->num_sets = 1;
    task->curr_inst = self;

    
    return knd_OK;
}

extern gsl_err_t parse_class_delta(void *data,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndClass *self = data;
    int err;

    if (!self->curr_class) return make_gsl_err(gsl_FAIL);

    err = knd_select_class_delta(self->curr_class, rec, total_size);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

extern gsl_err_t knd_select_class(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndClass *c;
    struct glbOutput *log = self->entry->repo->log;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CLASS_SELECT_LEVEL_TMP)
        knd_log(".. parsing class select rec: \"%.*s\"", 32, rec);

    self->depth = 0;
    self->curr_class = NULL;
    self->curr_baseclass = NULL;

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
          .name = "inst",
          .name_size = strlen("inst"),
          .parse = knd_parse_import_class_inst,
          .obj = self
        },
        { .name = "inst",
          .name_size = strlen("inst"),
          .parse = parse_select_inst,
          .obj = self
        },
        { .name = "_base",
          .name_size = strlen("_base"),
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
        if (self->curr_class) {
            c = self->curr_class;
            c->reset_inbox(c);
        }
        return parser_err;
    }

    /* any updates happened? */
    if (self->curr_class) {
        c = self->curr_class;
        if (c->inbox_size || c->obj_inbox_size) {
            c->next = self->inbox;
            self->inbox = c;
            self->inbox_size++;
        }
    }

    return make_gsl_err(gsl_OK);
}