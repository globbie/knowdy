#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

#include <gsl-parser.h>
#include <glb-lib/output.h>

#include "knd_rel.h"
#include "knd_rel_arg.h"
#include "knd_task.h"
#include "knd_state.h"
#include "knd_repo.h"
#include "knd_class_inst.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_class.h"
#include "knd_mempool.h"
#include "knd_text.h"
#include "knd_utils.h"

#define DEBUG_REL_LEVEL_0 0
#define DEBUG_REL_LEVEL_1 0
#define DEBUG_REL_LEVEL_2 0
#define DEBUG_REL_LEVEL_3 0
#define DEBUG_REL_LEVEL_TMP 1

static int export_inst_JSON(struct kndRelInstance *inst,
                            struct kndTask *task);

static int read_instance(struct kndRel *self,
                         struct kndRelInstance *inst,
                         const char *rec,
                         size_t *total_size);

static int unfreeze_inst(struct kndRel *self,
                         struct kndRelInstEntry *entry);

static void
del(struct kndRel *self)
{
    free(self);
}

static void str(struct kndRel *self)
{
    struct kndRelArg *arg;

    knd_log("\n%*sREL: %.*s [id:%.*s]", self->depth * KND_OFFSET_SIZE, "",
            self->name_size, self->name, self->id_size, self->id);

    for (arg = self->args; arg; arg = arg->next) {
        arg->depth = self->depth + 1;
        arg->str(arg);
    }
}

static void reset_inbox(struct kndRel *self)
{
    struct kndRel *rel, *next_rel;
    struct kndRelInstance *inst, *next_inst;

    rel = self->inbox;
    while (rel) {
        rel->reset_inbox(rel);
        next_rel = rel->next;
        rel->next = NULL;
        rel = next_rel;
    }

    inst = self->inst_inbox;
    while (inst) {
        next_inst = inst->next;
        inst->next = NULL;
        inst = next_inst;
    }

    self->inst_inbox = NULL;
    self->inst_inbox_size = 0;
    self->inbox = NULL;
    self->inbox_size = 0;
}

static void inst_arg_str(struct kndRelArgInstance *inst)
{
    struct kndRelArg *relarg = inst->relarg;

    knd_log("ARG:%.*s    CLASS: \"%.*s\"",
            relarg->name_size, relarg->name,
            inst->classname_size, inst->classname);

    if (inst->objname_size)
        knd_log("            OBJ name: \"%.*s\"",
                inst->objname_size, inst->objname);
    /*if (inst->obj)
        knd_log("            OBJ: \"%.*s\"",
                inst->obj->name_size, inst->obj->name);
    */
    if (inst->val_size)
        knd_log("            VAL: \"%.*s\"",
                inst->val_size, inst->val);
}

static void inst_str(struct kndRel *self, struct kndRelInstance *inst)
{
    struct kndRelArgInstance *arg;

    knd_log("\n%*sRel Instance: \"%.*s\" NAME:%.*s ID:%.*s",
            self->depth * KND_OFFSET_SIZE, "",
            inst->rel->entry->name_size, inst->rel->entry->name,
            inst->name_size, inst->name,
            inst->id_size, inst->id);

    for (arg = inst->args; arg; arg = arg->next) {
        inst_arg_str(arg);
    }
}

static gsl_err_t run_present_rel(void *obj,
                                 const char *unused_var(val),
                                 size_t unused_var(val_size))
{
    struct kndTask *task = obj;
    struct kndRel *self = task->rel;
    struct kndRel *rel;
    int err;

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. presenting rel selection..");

    if (!self->curr_rel) return make_gsl_err(gsl_FAIL);

    rel = self->curr_rel;
    rel->depth = 0;
    rel->format = KND_FORMAT_JSON;

    /* export BODY */
    err = rel->export(rel, task);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_gloss_locale(void *obj, const char *name, size_t name_size)
{
    struct kndTranslation *self = obj;
    if (name_size >= KND_SHORT_NAME_SIZE) return make_gsl_err(gsl_LIMIT);
    self->curr_locale = name;
    self->curr_locale_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_gloss_value(void *obj, const char *name, size_t name_size)
{
    struct kndTranslation *self = obj;
    if (!name_size) return make_gsl_err(gsl_FAIL);
    self->val = name;
    self->val_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_gloss_item(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndRel *self = obj;
    struct kndTranslation *tr;

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. %.*s: allocate gloss translation",
                self->name_size, self->name);

    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return *total_size = 0, make_gsl_err_external(knd_NOMEM);
    memset(tr, 0, sizeof(struct kndTranslation));

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_gloss_locale,
          .obj = tr
        },
        { .name = "t",
          .name_size = strlen("t"),
          .run = set_gloss_value,
          .obj = tr
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    if (tr->curr_locale_size == 0 || tr->val_size == 0)
        return make_gsl_err(gsl_FORMAT);  // error: both of them are required

    tr->locale = tr->curr_locale;
    tr->locale_size = tr->curr_locale_size;

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. read gloss translation: \"%.*s\",  text: \"%.*s\"",
                tr->locale_size, tr->locale, tr->val_size, tr->val);

    // append
    tr->next = self->tr;
    self->tr = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_gloss(void *obj,
                             const char *rec,
                             size_t *total_size)
{
    struct kndRel *self = obj;
    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .parse = parse_gloss_item,
        .obj = self
    };

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. %.*s: reading gloss",
                self->name_size, self->name);

    return gsl_parse_array(&item_spec, rec, total_size);
}

static gsl_err_t run_set_name(void *obj, const char *name, size_t name_size)
{
    struct kndRel *self = obj;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    self->name = name;
    self->name_size = name_size;

    return make_gsl_err(gsl_OK);
}

static int export_JSON(struct kndRel *self,
                       struct kndTask *task)
{
    struct glbOutput *out = task->out;
    struct kndRelArg *arg;
    struct kndTranslation *tr;
    bool in_list = false;
    int err;

    err = out->write(out, "{", 1);
    if (err) return err;

    if (self->name_size) {
        err = out->write(out, "\"_name\":\"", strlen("\"_name\":\""));            RET_ERR();
        err = out->write(out, self->entry->name, self->entry->name_size);         RET_ERR();
        err = out->write(out, "\"", 1);                                           RET_ERR();
        in_list = true;
    }

    /* choose gloss */
    tr = self->tr;
    while (tr) {
        if (memcmp(task->locale, tr->locale, tr->locale_size)) {
            goto next_tr;
        }
        if (in_list) {
            err = out->write(out, ",", 1);                                    RET_ERR();
        }
        err = out->write(out, "\"_gloss\":\"", strlen("\"_gloss\":\""));        RET_ERR();
        err = out->write(out, tr->val,  tr->val_size);                            RET_ERR();
        err = out->write(out, "\"", 1);                                           RET_ERR();
        break;
    next_tr:
        tr = tr->next;
    }

    for (arg = self->args; arg; arg = arg->next) {
        arg->format = KND_FORMAT_JSON;
        arg->out = out;
        if (in_list) {
            err = out->write(out, ",", 1);                                    RET_ERR();
        }
        err = arg->export(arg);
        if (err) return err;
    }

    err = out->write(out, "}", 1);
    if (err) return err;

    return knd_OK;
}

static int export_GSP(struct kndRel *self,
                      struct kndTask *task)
{
    struct glbOutput *out = task->out;
    struct kndRelArg *arg;
    struct kndTranslation *tr;
    int err;

    err = out->writec(out, '{');
    if (err) return err;
    err = out->write(out, self->id, self->id_size);
    if (err) return err;

    err = out->writec(out, ' ');
    if (err) return err;

    err = out->write(out, self->name, self->name_size);
    if (err) return err;

    if (self->tr) {
        err = out->write(out, "[_g", strlen("[_g"));                    RET_ERR();
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

    for (arg = self->args; arg; arg = arg->next) {
        arg->format = KND_FORMAT_GSP;
        arg->out = self->out;
        err = arg->export(arg);
        if (err) return err;
    }

    err = out->write(out, "}", 1);
    if (err) return err;

    return knd_OK;
}

static int export_inst_GSP(struct kndRelInstance *inst,
                           struct kndTask *task)
{
    struct kndRelArg *relarg;
    struct kndRelArgInstance *relarg_inst;
    struct glbOutput *out = task->out;
    int err;

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. export rel inst %.*s..", inst->id_size, inst->id);

    err = out->writec(out, '{');                                             RET_ERR();
    err = out->write(out, inst->id, inst->id_size);                          RET_ERR();

    for (relarg_inst = inst->args;
         relarg_inst;
         relarg_inst = relarg_inst->next) {

        relarg = relarg_inst->relarg;
        /* skip over the selected obj */
        /*if (relarg_inst->obj) {
            entry = relarg_inst->obj;
            if (relarg->curr_obj == entry->obj) continue;
            }*/

        relarg->out = out;
        relarg->format = KND_FORMAT_GSP;

        err = out->writec(out, '{');                                              RET_ERR();
        err = out->write(out, relarg->name, relarg->name_size);                   RET_ERR();
        err = relarg->export_inst(relarg, relarg_inst);                           RET_ERR();
        err = out->writec(out, '}');                                              RET_ERR();
    }

    err = out->writec(out, '}');                                                  RET_ERR();
    return knd_OK;
}

static int export_inst_setelem_JSON(void *obj,
                                    const char *elem_id,
                                    size_t elem_id_size,
                                    size_t count,
                                    void *elem)
{
    struct kndTask *task = obj;
    struct kndRel *self = task->rel;
    if (count < task->start_from) return knd_OK;
    if (task->batch_size >= task->batch_max) return knd_RANGE;
    struct glbOutput *out = task->out;
    struct kndRelInstance *inst = elem;
    int err;

    if (DEBUG_REL_LEVEL_2)
        knd_log("..export inst elem: %.*s", elem_id_size, elem_id);

    // TODO unfreeze

    /* separator */
    if (task->batch_size) {
        err = out->writec(out, ',');                                              RET_ERR();
    }

    inst->depth = 0;
    inst->max_depth = 0;
    if (self->max_depth) {
        inst->max_depth = self->max_depth;
    }

    err = export_inst_JSON(inst, task);                                                 RET_ERR();

    task->batch_size++;

    return knd_OK;
}

static int export_inst_set_JSON(struct kndTask *task,
                                struct kndSet *set)
{
    struct glbOutput *out = task->out;
    int err;

    err = out->write(out, "{\"_set\":{",
                     strlen("{\"_set\":{"));                                      RET_ERR();

    err = out->writef(out, "\"total\":%lu",
                      (unsigned long)set->num_elems);                             RET_ERR();

    err = out->write(out, ",\"batch\":[",
                     strlen(",\"batch\":["));                                     RET_ERR();

    err = set->map(set, export_inst_setelem_JSON, (void*)task);
    if (err && err != knd_RANGE) return err;

    err = out->writec(out, ']');                                                  RET_ERR();

    err = out->writef(out, ",\"batch_max\":%lu",
                      (unsigned long)task->batch_max);                            RET_ERR();

    err = out->writef(out, ",\"batch_size\":%lu",
                      (unsigned long)task->batch_size);                           RET_ERR();

    err = out->writef(out, ",\"batch_from\":%lu",
                      (unsigned long)task->batch_from);                           RET_ERR();

    err = out->writec(out, '}');                                                  RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}

static int export_inst_JSON(struct kndRelInstance *inst,
                            struct kndTask *task)
{
    struct glbOutput *out = task->out;
    struct kndRelArg *relarg;
    struct kndRelArgInstance *relarg_inst;
    struct kndUpdate *update;
    struct kndState *state;
    struct kndClassInstEntry *obj_entry;
    struct kndClassInst *class_inst;
    bool in_list = false;
    int err;

    err = out->writec(out, '{');
    if (err) return err;

    err = out->write(out, "\"_name\":\"", strlen("\"_name\":\""));                       RET_ERR();
    err = out->write(out, inst->name, inst->name_size);                                  RET_ERR();
    err = out->writec(out, '"');
    in_list = true;

    /* show the latest state */
    if (inst->num_states) {
        state = inst->states;
        update = state->update;
        err = out->write(out, ",\"_state\":", strlen(",\"_state\":"));
        if (err) return err;
        err = out->writef(out, "%zu", update->numid);
        if (err) return err;

        /*time(&update->timestamp);
        localtime_r(&update->timestamp, &tm_info);
        buf_size = strftime(buf, KND_NAME_SIZE,
                            ",\"_modif\":\"%Y-%m-%d %H:%M:%S\"", &tm_info);
        err = out->write(out, buf, buf_size);
        if (err) return err;
        */

        switch (state->phase) {
        case KND_REMOVED:
            err = out->write(out,   ",\"_phase\":\"del\"",
                             strlen(",\"_phase\":\"del\""));                      RET_ERR();
            // NB: no more details
            err = out->write(out, "}", 1);
            if (err) return err;
            return knd_OK;
        case KND_UPDATED:
            err = out->write(out,   ",\"_phase\":\"upd\"",
                             strlen(",\"_phase\":\"upd\""));                      RET_ERR();
            break;
        default:
            break;
        }
    }

    for (relarg_inst = inst->args;
         relarg_inst;
         relarg_inst = relarg_inst->next) {

        relarg = relarg_inst->relarg;

        /* skip over the selected obj */
        if (task->class_inst && relarg_inst->obj) {
            obj_entry = relarg_inst->obj;
            if (task->class_inst == obj_entry->inst) continue;
        }

        relarg->out = out;
        if (in_list) {
            err = out->writec(out, ',');                                         RET_ERR();
        }
        err = out->write(out, "\"", strlen("\""));                               RET_ERR();
        err = out->write(out, relarg->name, relarg->name_size);                  RET_ERR();
        err = out->write(out, "\":", strlen("\":"));                             RET_ERR();

        // expand ref
        if (relarg_inst->obj) {
            class_inst = relarg_inst->obj->inst;
            // TODO max_depth
            class_inst->depth = 1;
            err = class_inst->export(class_inst, KND_FORMAT_JSON, task);
            if (err) return err;
        } else {
            if (relarg_inst->val_size) {
                err = out->writec(out, '"');                                         RET_ERR();
                err = out->write(out, relarg_inst->val, relarg_inst->val_size);      RET_ERR();
                err = out->writec(out, '"');                                         RET_ERR();
            } else {
                err = relarg->export_inst(relarg, relarg_inst);                      RET_ERR();
            }
        }

        in_list = true;
    }

    err = out->write(out, "}", 1);
    if (err) return err;

    return knd_OK;
}

static int export(struct kndRel *self,
                  struct kndTask *task)
{
    int err;

    switch (self->format) {
    case KND_FORMAT_JSON:
        err = export_JSON(self, task);
        if (err) return err;
        break;
    case KND_FORMAT_GSP:
        err = export_GSP(self, task);
        if (err) return err;
        break;
    default:
        break;
    }
    return knd_OK;
}

static int export_inst(struct kndRelInstance *inst,
                       struct kndTask *task)
{
    int err;

    switch (task->format) {
    case KND_FORMAT_JSON:
        err = export_inst_JSON(inst, task);
        if (err) return err;
        break;
    case KND_FORMAT_GSP:
        err = export_inst_GSP(inst, task);
        if (err) return err;
        break;
    default:
        break;
    }

    return knd_OK;
}


static gsl_err_t validate_rel_arg(void *obj,
                                  const char *name,
                                  size_t name_size,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndRel *self = obj;
    struct kndRelArg *arg;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. parsing the \"%.*s\" rel arg, rec:\"%.*s\"",
                name_size, name, 32, rec);

    /* TODO mempool */
    err = kndRelArg_new(&arg);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    arg->rel = self;

    if (!strncmp(name, "subj", strlen("subj"))) {
        arg->type = KND_RELARG_SUBJ;
    } else if (!strncmp(name, "obj", strlen("obj"))) {
        arg->type = KND_RELARG_OBJ;
    } else if (!strncmp(name, "ins", strlen("ins"))) {
        arg->type = KND_RELARG_INS;
    }

    parser_err = arg->parse(arg, rec, total_size);
    if (parser_err.code) {
        if (DEBUG_REL_LEVEL_TMP)
            knd_log("-- failed to parse rel arg: %d", gsl_err_to_knd_err_codes(parser_err));
        return parser_err;
    }

    if (!self->tail_arg) {
        self->tail_arg = arg;
        self->args = arg;
    }
    else {
        self->tail_arg->next = arg;
        self->tail_arg = arg;
    }

    self->num_args++;

    if (DEBUG_REL_LEVEL_1)
        arg->str(arg);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t import_rel(struct kndRel *self,
                            const char *rec,
                            size_t *total_size)
{
    struct kndRel *rel;
    struct kndRelEntry *entry;
    struct kndMemPool *mempool = self->task->mempool;
    struct glbOutput *log = self->task->log;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. import Rel: \"%.*s\"..", 32, rec);

    err = knd_rel_new(mempool, &rel);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    rel->rel_name_idx = self->rel_name_idx;
    rel->class_idx = self->class_idx;
    rel->class_name_idx = self->class_name_idx;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = rel
        }/*,
        { .type = GSL_SET_STATE,
          .name = "is",
          .name_size = strlen("is"),
          .parse = parse_baseclass,
          .obj = rel
          }*/,
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .parse = parse_gloss,
          .obj = rel
        },
        { .type = GSL_SET_STATE,
          .validate = validate_rel_arg,
          .obj = rel
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) goto final;

    if (!rel->name_size) {
        parser_err = make_gsl_err_external(knd_FAIL);
        goto final;
    }

    entry = (struct kndRelEntry*)self->rel_name_idx->get(self->rel_name_idx,
                                                     rel->name, rel->name_size);
    if (entry) {
        knd_log("-- %s relation name doublet found :(", rel->name);
        log->reset(log);
        err = log->write(log,
                               rel->name,
                               rel->name_size);
        if (err) { parser_err = make_gsl_err_external(err); goto final; }
        err = log->write(log,
                               " relation name already exists",
                               strlen(" relation name already exists"));
        if (err) { parser_err = make_gsl_err_external(err); goto final; }
        parser_err = make_gsl_err_external(knd_FAIL);
        goto final;
    }

    if (!self->batch_mode) {
        rel->next = self->inbox;
        self->inbox = rel;
        self->inbox_size++;
    }

    err = knd_rel_entry_new(mempool, &entry);
    if (err) { return make_gsl_err_external(err); }
    entry->rel = rel;
    entry->repo = self->entry->repo;
    rel->entry = entry;

    self->num_rels++;
    entry->numid = self->num_rels;
    knd_uid_create(entry->numid, entry->id, &entry->id_size);

    /* automatic name assignment if no explicit name given */
    entry->name = rel->name;
    entry->name_size = rel->name_size;

    if (!rel->name_size) {
        entry->name = entry->id;
        entry->name_size = entry->id_size;
    }

    err = self->rel_name_idx->set(self->rel_name_idx,
                                  entry->name, entry->name_size, (void*)entry);
    if (err) { parser_err = make_gsl_err_external(err); goto final; }

    if (DEBUG_REL_LEVEL_2)
        rel->str(rel);

    return make_gsl_err(gsl_OK);
 final:
    // FIXME(k15tfu): free rel & entry

    return parser_err;
}

static gsl_err_t confirm_rel_read(void *obj,
                                  const char *unused_var(val),
                                  size_t unused_var(val_size))
{
    struct kndRel *self = obj;

    if (DEBUG_REL_LEVEL_2)
        knd_log("== REL %.*s read OK!",
                self->name_size, self->name);

    return make_gsl_err(gsl_OK);
}


static int read_rel_incipit(struct kndRel *self,
                            struct kndRelEntry *entry)
{
    char buf[KND_NAME_SIZE + 1];
    size_t buf_size;
    off_t offset = 0;
    int fd = self->fd;
    int err;

    if (DEBUG_REL_LEVEL_2)
        knd_log("\n.. get rel name, global offset:%zu  block size:%zu",
                entry->global_offset, entry->block_size);

    buf_size = entry->block_size;
    if (entry->block_size > KND_NAME_SIZE)
        buf_size = KND_NAME_SIZE;

    offset = entry->global_offset;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        return knd_IO_FAIL;
    }
    err = read(fd, buf, buf_size);
    if (err == -1) return knd_IO_FAIL;

    if (DEBUG_REL_LEVEL_2)
        knd_log("== REL BODY incipit: %.*s",
                buf_size, buf);
    entry->id_size = KND_ID_SIZE;
    entry->name_size = KND_NAME_SIZE;

    /*err = knd_parse_incipit(buf, buf_size,
                            entry->id, &entry->id_size,
                            entry->name, &entry->name_size);
    if (err) return err;
    */
    if (DEBUG_REL_LEVEL_1)
        knd_log("== REL NAME:\"%.*s\" ID:%.*s",
                entry->name_size, entry->name, entry->id_size, entry->id);

    return knd_OK;
}

static gsl_err_t append_inst_entry_item(void *accu, void *item)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndRelEntry *parent_entry = accu;
    struct kndRelInstEntry *entry = item;
    struct kndMemPool *mempool = parent_entry->rel->task->mempool;
    struct kndSet *set;
    off_t offset = 0;
    int fd = parent_entry->fd;
    int err;

    entry->offset = parent_entry->curr_offset;

    if (DEBUG_REL_LEVEL_2)
        knd_log("\n.. RelDir: \"%.*s\" to append atomic entry"
                " (block size: %zu) offset:%zu",
                parent_entry->name_size, parent_entry->name,
                entry->block_size, entry->offset);

    buf_size = entry->block_size;
    if (entry->block_size > KND_NAME_SIZE)
        buf_size = KND_NAME_SIZE;

    offset = entry->offset;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        return make_gsl_err_external(knd_IO_FAIL);
    }

    err = read(fd, buf, buf_size);
    if (err == -1) return make_gsl_err_external(knd_IO_FAIL);

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. Rel inst incipit: \"%.*s\"",
                buf_size, buf);

    entry->id_size = KND_ID_SIZE;
    entry->name_size = KND_NAME_SIZE;
    err = knd_parse_incipit(buf, buf_size,
                            entry->id, &entry->id_size,
                            entry->name, &entry->name_size);
    if (err) return make_gsl_err_external(err);

    parent_entry->curr_offset += entry->block_size;


    /* TODO */
    if (entry->id_size == KND_ID_SIZE) {
        if (entry->name_size >= KND_ID_SIZE)
            return  make_gsl_err_external(knd_LIMIT);
        memcpy(entry->id, entry->name, entry->name_size);
        entry->id_size = entry->name_size;
    }

    if (DEBUG_REL_LEVEL_2)
        knd_log("== Rel inst id:%.*s name:%.*s",
                entry->id_size, entry->id,
                entry->name_size, entry->name);

    set = parent_entry->inst_idx;
    if (!set) {
        err = knd_set_new(mempool, &set);
        if (err) return make_gsl_err_external(err);
        set->type = KND_SET_REL_INST;
        parent_entry->inst_idx = set;
    }

    err = set->add(set, entry->id, entry->id_size, (void*)entry);
    if (err) return make_gsl_err_external(err);

    /* update name idx */
    /*err = parent_entry->obj_name_idx->set(parent_entry->obj_name_idx,
                                        entry->name, entry->name_size,
                                        entry);
    if (err) return make_gsl_err_external(err);
    */
    /*err = parent_entry->inst_name_idx->set(parent_entry->inst_name_idx,
                                         inst->name, inst->name_size, (void*)entry);
    if (err) return make_gsl_err_external(err);
    */
    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_inst_entry_item(void *obj,
                                     const char *val,
                                     size_t val_size)
{
    struct kndRelEntry *parent_entry = obj;
    struct kndRelInstEntry *entry = NULL;

    if (DEBUG_REL_LEVEL_1)
        knd_log(".. create Rel inst entry: %.*s  entry: %p",
                val_size, val, parent_entry);

    entry = malloc(sizeof(struct kndRelInstEntry));
    if (!entry) return make_gsl_err_external(knd_NOMEM);
    memset(entry, 0, sizeof(struct kndRelInstEntry));

    knd_gsp_num_to_num(val, val_size, &entry->block_size);


    // append
    return append_inst_entry_item(parent_entry, entry);
}

static gsl_err_t set_rel_body_size(void *obj, const char *val, size_t val_size)
{
    struct kndRelEntry *self = obj;

    if (!val_size) return make_gsl_err(gsl_FORMAT);
    if (val_size >= KND_SHORT_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    knd_gsp_num_to_num(val, val_size, &self->body_size);

    if (DEBUG_REL_LEVEL_1)
        knd_log("== Rel body size: %.*s => %zu",
                val_size, val, self->body_size);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_rel_body_size(void *obj,
                                       const char *rec,
                                       size_t *total_size)
{
    struct kndRelEntry *self = obj;
    gsl_err_t err;

    if (DEBUG_REL_LEVEL_1)
        knd_log(".. parsing rel body size: \"%.*s\"", 16, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_rel_body_size,
          .obj = self
        }
    };

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    self->curr_offset += self->body_size;

    return make_gsl_err(gsl_OK);
}

static int parse_dir_trailer(struct kndRel *self,
                             struct kndRelEntry *parent_entry)
{
    char *dir_buf = self->out->buf;
    size_t dir_buf_size = self->out->buf_size;
    size_t parsed_size = 0;
    gsl_err_t parser_err;

    struct gslTaskSpec inst_dir_spec = {
        .is_list_item = true,
        .run = run_inst_entry_item,
        .obj = parent_entry
    };
    
    struct gslTaskSpec specs[] = {
        { .name = "R",
          .name_size = strlen("R"),
          .parse = parse_rel_body_size,
          .obj = parent_entry
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "i",
          .name_size = strlen("i"),
          .parse = gsl_parse_array,
          .obj = &inst_dir_spec
        }
    };

    parent_entry->curr_offset = parent_entry->global_offset;

    if (DEBUG_REL_LEVEL_2)
        knd_log("  .. parsing \"%.*s\" DIR REC: \"%.*s\"  curr offset: %zu   [dir size:%zu]",
                KND_ID_SIZE, parent_entry->id, dir_buf_size, dir_buf,
                parent_entry->curr_offset, dir_buf_size);

    parser_err = gsl_parse_task(dir_buf, &parsed_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);
    
    return knd_OK;
}

static int read_dir_trailer(struct kndRel *self,
                            struct kndRelEntry *parent_entry,
                            int fd)
{
    size_t block_size = parent_entry->block_size;
    struct glbOutput *out = self->out;
    off_t offset = 0;
    size_t dir_size = 0;
    size_t chunk_size = 0;
    const char *val = NULL;
    size_t val_size = 0;
    int err;

    if (block_size <= KND_DIR_ENTRY_SIZE)
        return knd_NO_MATCH;

    offset = (parent_entry->global_offset + block_size) - KND_DIR_ENTRY_SIZE;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        return knd_IO_FAIL;
    }
    out->reset(out);
    out->buf_size = KND_DIR_ENTRY_SIZE;
    err = read(fd, out->buf, out->buf_size);
    if (err == -1) return knd_IO_FAIL;

    if (DEBUG_REL_LEVEL_2)
        knd_log("  .. DIR size field read: \"%.*s\" [%zu]",
                out->buf_size, out->buf, out->buf_size);

    err =  knd_parse_dir_size(out->buf, out->buf_size,
                              &val, &val_size, &chunk_size);
    if (err) {
        if (DEBUG_REL_LEVEL_2)
            knd_log("NB: no RelDir size field in \"%.*s\"",
                    out->buf_size, out->buf);
        return knd_NO_MATCH;
    }

    knd_gsp_num_to_num(val, val_size, &dir_size);

    if (DEBUG_REL_LEVEL_2)
        knd_log("== Rel DIR size: %.*s [chunk size:%zu] => %zu",
                val_size, val, chunk_size, dir_size);

    parent_entry->body_size = block_size - dir_size - chunk_size;
    parent_entry->dir_size = dir_size;

    if (DEBUG_REL_LEVEL_2)
        knd_log("  .. DIR: offset: %lu  block: %lu  body: %lu  dir: %lu",
                (unsigned long)parent_entry->global_offset,
                (unsigned long)parent_entry->block_size,
                (unsigned long)parent_entry->body_size,
                (unsigned long)parent_entry->dir_size);

    offset = (parent_entry->global_offset + block_size) - chunk_size - dir_size;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        return knd_IO_FAIL;
    }

    if (dir_size >= out->capacity) return knd_LIMIT;

    out->reset(out);
    out->buf_size = dir_size;
    err = read(fd, out->buf, out->buf_size);
    if (err == -1) return knd_IO_FAIL;
    out->buf[out->buf_size] = '\0';

    if (DEBUG_REL_LEVEL_2)
        knd_log("== DIR to parse: \"%.*s\"",
                out->buf_size, out->buf);

    parent_entry->fd = fd;
    err = parse_dir_trailer(self, parent_entry);
    if (err) {
        knd_log("-- failed to parse Rel dir trailer: \"%.*s\"",
                out->buf_size, out->buf);
        return err;
    }

    return knd_OK;
}


static int read_rel(struct kndRel *self,
                    struct kndRelEntry *entry,
                    int fd)
{
    int err;

    err = read_rel_incipit(self, entry);
    if (err) return err;

    err = self->rel_name_idx->set(self->rel_name_idx,
                                  entry->name, entry->name_size, (void*)entry);
    if (err) return err;

    err = self->rel_idx->set(self->rel_idx,
                             entry->id, entry->id_size, (void*)entry);                  RET_ERR();

    err = read_dir_trailer(self, entry, fd);
    if (err) {
        if (err == knd_NO_MATCH) {
            if (DEBUG_REL_LEVEL_2)
                knd_log("NB: no dir trailer found");
            return knd_OK;
        }
        return err;
    }

    return knd_OK;
}

static gsl_err_t read_GSP(struct kndRel *self,
                          const char *rec,
                          size_t *total_size)
{
    if (DEBUG_REL_LEVEL_2)
        knd_log(".. reading rel GSP: \"%.*s\"..", 64, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_g",
          .name_size = strlen("_g"),
          .parse = parse_gloss,
          .obj = self
        },
        { .validate = validate_rel_arg,
          .obj = self
        },
        { .is_default = true,
          .run = confirm_rel_read,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static int unfreeze_inst(struct kndRel *self,
                         struct kndRelInstEntry *entry)
{
    struct kndRelInstance *inst;
    struct kndMemPool *mempool = self->task->mempool;
    const char *filename;
    size_t filename_size;
    const char *c;
    size_t chunk_size;
    struct stat st;
    int fd;
    size_t file_size = 0;
    struct stat file_info;
    int err;

    /* parse DB rec */
    filename = self->frozen_output_file_name;
    filename_size = self->frozen_output_file_name_size;
    if (!filename_size) {
        knd_log("-- no file name to read in Rel %.*s :(",
                self->name_size, self->name);
        return knd_FAIL;
    }

    if (stat(filename, &st)) {
        knd_log("-- no such file: \"%.*s\"", filename_size, filename);
        return knd_NO_MATCH; 
    }

    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        knd_log("-- error reading FILE \"%.*s\": %d", filename_size, filename, fd);
        return knd_IO_FAIL;
    }
    fstat(fd, &file_info);
    file_size = file_info.st_size;  
    if (file_size <= KND_DIR_ENTRY_SIZE) {
        err = knd_LIMIT;
        goto final;
    }

    if (lseek(fd, entry->offset, SEEK_SET) == -1) {
        err = knd_IO_FAIL;
        goto final;
    }

    entry->block = malloc(entry->block_size + 1);
    if (!entry->block) return knd_NOMEM;

    err = read(fd, entry->block, entry->block_size);
    if (err == -1) {
        err = knd_IO_FAIL;
        goto final;
    }
    /* NB: current parser expects a null terminated string */
    entry->block[entry->block_size] = '\0';

    if (DEBUG_REL_LEVEL_2)
        knd_log("   == INST REC: \"%.*s\"",
                entry->block_size, entry->block);

    /* done reading */
    close(fd);

    err = knd_rel_inst_new(mempool, &inst);                      RET_ERR();
    err = knd_state_new(mempool, &inst->states);                  RET_ERR();
    inst->states->phase = KND_FROZEN;
    inst->entry = entry;
    entry->inst = inst;
    inst->rel = self;

    /* skip over initial brace '{' */
    c = entry->block + 1;
    //b = c;
    bool got_separ = false;
    /* ff the name */
    while (*c) {
        switch (*c) {
        case '{':
        case '}':
        case '[':
        case ']':
            got_separ = true;
            //e = c;
            break;
        default:
            break;
        }
        if (got_separ) break;
        c++;
    }

    if (!got_separ) {
        knd_log("-- inst name not found in \"%.*s\" :(",
                entry->block_size, entry->block);
        //inst->del(inst);
        return knd_FAIL;
    }

    inst->name = entry->name;
    inst->name_size = entry->name_size;

    err = read_instance(self, inst, c, &chunk_size);
    if (err) {
        knd_log("-- failed to parse inst %.*s :(",
                inst->name_size, inst->name);
        return err;
    }

    if (DEBUG_REL_LEVEL_2)
        inst_str(self, inst);

    return knd_OK;
    
 final:
    close(fd);
    return err;
}

static int get_rel(struct kndRel *self,
                   const char *name, size_t name_size,
                   struct kndRel **result)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    struct kndMemPool *mempool = self->task->mempool;
    //struct glbOutput *log = self->entry->repo->log;
    size_t chunk_size = 0;
    size_t *total_size;
    struct kndRelEntry *entry;
    struct kndRelArg *arg;
    struct kndRel *rel;
    const char *filename;
    size_t filename_size;
    const char *b;
    struct stat st;
    int fd;
    size_t file_size = 0;
    struct stat file_info;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_REL_LEVEL_1)
        knd_log(".. get rel: \"%.*s\"",
                name_size, name);

    entry = self->rel_name_idx->get(self->rel_name_idx,
                                  name, name_size);
    if (!entry) {
        knd_log("-- no such rel: \"%.*s\" :(", name_size, name);
        /*log->reset(log);
        err = log->write(log, name, name_size);
        if (err) return err;
        err = log->write(log, " Rel name not found",
                               strlen(" Rel name not found"));
                               if (err) return err; */
        return knd_NO_MATCH;
    }

    if (entry->rel) {
        rel = entry->rel;
        //rel->states->phase = KND_SELECTED;
        rel->next = NULL;
        if (!rel->is_resolved) {
            knd_log("-- Rel %.*s is not resolved yet..",
                    name_size, name);
        }
        *result = rel;
        return knd_OK;
    }

    /* parse DB rec */
    filename = self->frozen_output_file_name;
    filename_size = self->frozen_output_file_name_size;
    if (stat(filename, &st)) {
        knd_log("-- no such file: %.*s", filename_size, filename);
        return knd_NO_MATCH;
    }

    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        knd_log("-- error reading FILE \"%.*s\": %d",
                filename_size, filename, fd);
        return knd_IO_FAIL;
    }

    fstat(fd, &file_info);
    file_size = file_info.st_size;
    if (file_size <= KND_DIR_ENTRY_SIZE) {
        err = knd_LIMIT;
        goto final;
    }

    if (lseek(fd, entry->global_offset, SEEK_SET) == -1) {
        err = knd_IO_FAIL;
        goto final;
    }

    buf_size = entry->body_size;
    if (buf_size >= KND_TEMP_BUF_SIZE) {
        knd_log("-- block too large: %zu", entry->body_size);
        return knd_NOMEM;
    }

    err = read(fd, buf, buf_size);
    if (err == -1) {
        err = knd_IO_FAIL;
        goto final;
    }
    buf[buf_size] = '\0';

    if (DEBUG_REL_LEVEL_2)
        knd_log("== frozen Rel REC: \"%.*s\" [%zu]",
                buf_size, buf, buf_size);

    /* done reading */
    close(fd);

    // TODO rel entry
    
    err = knd_rel_new(mempool, &rel);                            RET_ERR();
    rel->rel_name_idx = self->rel_name_idx;
    rel->class_idx = self->class_idx;
    rel->class_name_idx = self->class_name_idx;
    rel->entry = entry;

    rel->name = entry->name;
    rel->name_size = entry->name_size;

    rel->frozen_output_file_name = self->frozen_output_file_name;
    rel->frozen_output_file_name_size = self->frozen_output_file_name_size;

    b = buf + 1;
    bool got_separ = false;
    while (*b) {
        switch (*b) {
        case '{':
        case '}':
        case '[':
        case ']':
            got_separ = true;
            break;
        default:
            break;
        }
        if (got_separ) break;
        b++;
    }

    if (!got_separ) {
        knd_log("-- rel name not found in %.*s :(",
                buf_size, buf);
        return knd_FAIL;
    }
    total_size = &chunk_size;
    parser_err = rel->read(rel, b, total_size);
    if (parser_err.code) err = gsl_err_to_knd_err_codes(parser_err);
    PARSE_ERR();

    /* resolve args of a Rel */
    for (arg = rel->args; arg; arg = arg->next) {
        err = arg->resolve(arg, NULL);                                            RET_ERR();
    }

    entry->rel = rel;
    rel->entry = entry;

    if (DEBUG_REL_LEVEL_2)
        rel->str(rel);

    *result = rel;
    return knd_OK;

 final:
    close(fd);
    return err;
}

static gsl_err_t run_get_rel(void *obj, const char *name, size_t name_size)
{
    struct kndRel *self = obj;
    struct kndRel *rel;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    self->curr_rel = NULL;
    err = get_rel(self, name, name_size, &rel);
    if (err) return make_gsl_err_external(err);

    self->curr_rel = rel;

    if (DEBUG_REL_LEVEL_2)
        knd_log("++ Rel selected!");
    return make_gsl_err(gsl_OK);
}


static gsl_err_t parse_rel_arg_inst(void *obj,
                                    const char *name, size_t name_size,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndRelInstance *inst = obj;
    struct kndRel *rel = inst->rel;
    struct kndRelArg *arg = NULL;
    struct kndMemPool *mempool = inst->task->mempool;
    struct kndRelArgInstance *arg_inst = NULL;
    int err;

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. parsing the \"%.*s\" rel arg instance, rec:\"%.*s\"\n",
                name_size, name, 128, rec);

    for (arg = rel->args; arg; arg = arg->next) {
        if (arg->name_size != name_size) continue;
        if (memcmp(arg->name, name, name_size)) continue;
        break;
    }

    if (!arg) {
        knd_log("-- no such rel arg: %.*s :(", name_size, name);
        return *total_size = 0, make_gsl_err(gsl_FAIL);
    }

    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL, sizeof(struct kndRelArgInstance), (void**)&arg_inst);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    arg_inst->relarg = arg;
    arg_inst->rel_inst = inst;

    err = arg->parse_inst(arg, arg_inst, rec, total_size);
    if (err) {
        knd_log("-- failed to parse rel arg instance: %d", err);
        knd_log("== \"%.*s\" rel arg instance, rec:\"%.*s\"\n",
                name_size, name, 128, rec);
        return make_gsl_err_external(err);
    }

    arg_inst->next = inst->args;
    inst->args = arg_inst;

    return make_gsl_err(gsl_OK);
}


static gsl_err_t set_inst_name(void *obj, const char *name, size_t name_size)
{
    struct kndRelInstance *self = obj;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    /* TODO: check doublets */
    self->name = name;
    self->name_size = name_size;
    return make_gsl_err(gsl_OK);
}

static int read_instance(struct kndRel *self,
                         struct kndRelInstance *inst,
                         const char *rec,
                         size_t *total_size)
{
    gsl_err_t parser_err;

    if (DEBUG_REL_LEVEL_2) {
        knd_log(".. reading \"%.*s\" rel instance..", 128, rec);
    }

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_inst_name,
          .obj = inst
        },
        { .validate = parse_rel_arg_inst,
          .obj = inst
        },
        { .type = GSL_SET_STATE,
          .validate = parse_rel_arg_inst,
          .obj = inst
        }/*,
        { .is_default = true,
          .run = run_select_rel,
          .obj = inst
          }*/
   };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    if (DEBUG_REL_LEVEL_1)
        inst_str(self, inst);

    return knd_OK;
}

static gsl_err_t parse_import_instance(void *data,
                                       const char *rec,
                                       size_t *total_size)
{
    struct kndRel *self = data;
    struct kndRel *rel;
    struct kndRelInstance *inst;
    struct kndRelInstEntry *entry;
    struct kndMemPool *mempool = self->task->mempool;
    struct kndTask *task = self->task;
    struct kndSet *set;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_REL_LEVEL_2) {
        knd_log(".. import \"%.*s\" Relation instance..", 128, rec);
    }

    if (!self->curr_rel) {
        knd_log("-- curr rel not set :(");
        return *total_size = 0, make_gsl_err(gsl_FAIL);
    }

    if (task->type != KND_LIQUID_STATE)
        task->type = KND_UPDATE_STATE;

    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL, sizeof(struct kndRelInstance), (void**)&inst);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    err = knd_state_new(mempool, &inst->states);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    inst->states->phase = KND_SUBMITTED;
    inst->rel = self->curr_rel;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_inst_name,
          .obj = inst
        },
        { .type = GSL_SET_STATE,
          .validate = parse_rel_arg_inst,
          .obj = inst
        }/*,
        { .is_default = true,
          .run = run_select_rel,
          .obj = inst
          }*/
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- import rel inst failed");
        return parser_err;
    }
    rel = inst->rel;

    /* save in inbox */
    inst->next = rel->inst_inbox;
    rel->inst_inbox = inst;
    rel->inst_inbox_size++;
    rel->entry->num_insts++;

    /* assign id/name */
    inst->numid = rel->entry->num_insts;
    knd_uid_create(inst->numid, inst->id, &inst->id_size);

    /* automatic name assignment if no explicit name given */
    if (!inst->name_size) {
        inst->name = inst->id;
        inst->name_size = inst->id_size;
    }

    if (DEBUG_REL_LEVEL_2)
        inst_str(rel, inst);

    // indices
    set = rel->entry->inst_idx;
    if (!rel->entry->inst_idx) {
        err = knd_set_new(mempool, &set);
        if (err) return make_gsl_err_external(err);
        set->type = KND_SET_REL_INST;
        rel->entry->inst_idx = set;
    }

    if (!rel->entry->inst_name_idx) {
        err = ooDict_new(&rel->entry->inst_name_idx, KND_MEDIUM_DICT_SIZE);
        if (err) return make_gsl_err_external(err);
    }
    
    entry = malloc(sizeof(struct kndRelInstEntry));
    if (!entry) return make_gsl_err_external(knd_NOMEM);
    memset(entry, 0, sizeof(struct kndRelInstEntry));
    entry->inst = inst;

    err = set->add(set, inst->id, inst->id_size, (void*)entry);
    if (err) return make_gsl_err_external(err);

    err = rel->entry->inst_name_idx->set(rel->entry->inst_name_idx,
                                         inst->name, inst->name_size, (void*)entry);
    if (err) return make_gsl_err_external(err);

    if (DEBUG_REL_LEVEL_2) {
        knd_log("\n\nREGISTER INST in %.*s [IDX total:%zu valid:%zu]",
                rel->entry->name_size, rel->entry->name,
                rel->entry->inst_name_idx->size,
                rel->entry->num_insts);
        inst_str(rel, inst);
    }

    return make_gsl_err(gsl_OK);
}

static int get_rel_inst(struct kndRel *self,
                        const char *name,
                        size_t name_size,
                        struct kndRelInstance **result)
{
    //struct glbOutput *log = self->entry->repo->log;
    struct kndTask *task = self->task;
    struct kndRelInstEntry *entry;
    struct kndRelInstance *inst;
    //int e;
    //int err;
    
    if (!self->entry->inst_name_idx) {
        knd_log("-- no inst name idx in \"%.*s\" :(",
                self->entry->name_size, self->entry->name);
        /*log->reset(log);
        e = log->write(log, self->entry->name, self->entry->name_size);
        if (e) return e;
        e = log->write(log, " class has no instances",
                             strlen(" class has no instances"));
                             if (e) return e;*/
        task->http_code = HTTP_NOT_FOUND;
        return knd_FAIL;
    }

    entry = self->entry->inst_name_idx->get(self->entry->inst_name_idx, name, name_size);
    if (!entry) {
        knd_log("-- no such inst: \"%.*s\"", name_size, name);
        /*log->reset(log);
        err = log->write(log, name, name_size);
        if (err) return err;
        err = log->write(log, " rel inst name not found",
                               strlen(" rel inst name not found"));
                               if (err) return err; */
        task->http_code = HTTP_NOT_FOUND;
        return knd_NO_MATCH;
    }

    if (DEBUG_REL_LEVEL_TMP)
        knd_log("++ got rel inst entry %.*s  size: %zu",
                name_size, name, entry->block_size);

    //if (!entry->obj) goto read_entry;

    if (entry->inst->states->phase == KND_REMOVED) {
        knd_log("-- \"%.*s\" inst was removed", name_size, name);
        /*log->reset(log);
        err = log->write(log, name, name_size);
        if (err) return err;
        err = log->write(log, " rel inst was removed",
                               strlen(" rel inst was removed"));
                               if (err) return err; */
        return knd_NO_MATCH;
    }

    inst = entry->inst;
    inst->states->phase = KND_SELECTED;
    *result = inst;
    return knd_OK;
}

static gsl_err_t run_get_rel_inst(void *obj, const char *name, size_t name_size)
{
    struct kndRel *self = obj;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    self->curr_inst = NULL;

    err = get_rel_inst(self, name, name_size, &self->curr_inst);
    if (err) {
        knd_log("-- failed to get rel inst: %.*s :(", name_size, name);
        return make_gsl_err_external(err);
    }
    self->curr_inst->max_depth = 0;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t remove_inst(void *data,
                             const char *unused_var(name),
                             size_t unused_var(name_size))
{
    struct kndRel *self         = data;
    struct kndRel  *root_rel    = self->entry->repo->root_rel;
    struct kndRelInstance *inst;
    struct kndState  *state;
    struct kndTask *task         = self->task;
    //struct glbOutput *log        = task->log;
    struct kndMemPool *mempool   = task->mempool;
    int err;

    if (!self->curr_inst) {
        knd_log("-- remove operation: no inst selected :(");
        return make_gsl_err(gsl_FAIL);
    }

    inst = self->curr_inst;

    if (DEBUG_REL_LEVEL_TMP)
        knd_log("== inst to remove: \"%.*s\"  Root Rel inbox:%p",
                inst->name_size, inst->name, root_rel->inbox);

    err = knd_state_new(mempool, &state);
    if (err) return make_gsl_err_external(err);

    state->phase = KND_REMOVED;
    state->next = inst->states;
    inst->states = state;

    /*log->reset(log);
    err = log->write(log, self->name, self->name_size);
    if (err) return make_gsl_err_external(err);
    err = log->write(log, " rel inst removed", strlen(" rel inst removed"));
    if (err) return make_gsl_err_external(err);
    */

    task->type = KND_UPDATE_STATE;
    inst->next = self->inst_inbox;
    self->inst_inbox = inst;
    self->inst_inbox_size++;

    /*self->next = root_rel->inbox;
    root_rel->inbox = self;
    root_rel->inbox_size++;
    */
    knd_log("Root Rel inbox:%zu   inst inbox:%zu  Rel:%p next:%p",
            root_rel->inbox_size, self->inst_inbox_size,
            self, self->next);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t present_inst_selection(void *data,
                                        const char *unused_var(val),
                                        size_t unused_var(val_size))
{
    struct kndTask *task = data;
    struct kndRel *self = task->rel;
    struct kndRelInstance *inst;
    struct glbOutput *out = task->out;
    //struct kndMemPool *mempool = base->entry->repo->mempool;
    struct kndSet *set;
    int err;

    if (task->type == KND_SELECT_STATE) {
        if (DEBUG_REL_LEVEL_TMP)
            knd_log(".. rel inst selection..");
        /* no sets found? */
        if (!task->num_sets) {
            err = out->write(out, "{}", strlen("{}"));
            if (err) return make_gsl_err_external(err);
            return make_gsl_err(gsl_OK);
        }

        /* TODO: intersection cache lookup  */
        set = task->sets[0];

        if (!set->num_elems) {
            err = out->write(out, "{}", strlen("{}"));
            if (err) return make_gsl_err_external(err);
            return make_gsl_err(gsl_OK);
        }

        /* final presentation in JSON 
           TODO: choose output format */
        //err = export_inst_set_JSON(self, set);
        //if (err) return make_gsl_err_external(err);
        return make_gsl_err(gsl_OK);
    }
    
    if (!self->curr_inst) {
        knd_log("-- no rel inst selected");
        return make_gsl_err(gsl_FAIL);
    }

    inst = self->curr_inst;
    inst->max_depth = self->max_depth;

    //err = obj->export(obj);
    //if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_rel_inst_select(void *data,
                                       const char *rec,
                                       size_t *total_size)
{
    struct kndRel *self = data;
    struct kndRel *rel = self->curr_rel;
    struct kndTask *task = self->task;
    struct glbOutput *log = task->log;
    gsl_err_t parser_err;
    int err;

    if (!rel) {
        knd_log("-- Relation not selected");
        /* TODO: log*/
        return *total_size = 0, make_gsl_err(gsl_FAIL);
    }

    if (DEBUG_REL_LEVEL_TMP)
        knd_log(".. select \"%.*s\" rel inst..  inst inbox:%p",
                16, rec, rel->inst_inbox);

    task->type = KND_GET_STATE;
    self->max_depth = 0;
    self->curr_inst = NULL;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = run_get_rel_inst,
          .obj = rel
        },
        {  .name = "_depth",
           .name_size = strlen("_depth"),
           .parse = gsl_parse_size_t,
           .is_selector = true,
           .obj = &self->max_depth
        },
        { .type = GSL_SET_STATE,
          .name = "_rm",
          .name_size = strlen("_rm"),
          .run = remove_inst,
          .obj = rel
        }/*,
        { .name = "_delta",
          .name_size = strlen("_delta"),
          .is_selector = true,
          .parse = parse_select_inst_delta,
          .obj = self
          }*/,
        { .is_default = true,
          .run = present_inst_selection,
          .obj = task
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- Rel Instance select parse error: \"%.*s\"",
                log->buf_size, log->buf);
        if (!log->buf_size) {
            err = log->write(log, "obj select parse failure",
                                 strlen("obj select parse failure"));
            if (err) return make_gsl_err_external(err);
        }
        return parser_err;
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_rel_select(struct kndRel *self,
                                  const char *rec,
                                  size_t *total_size,
                                  struct kndTask *task)
{
    //struct glbOutput *log = self->entry->repo->log;
    struct kndRel *rel;
    //int e;
    gsl_err_t parser_err;

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. parsing Rel select: \"%.*s\"",
                16, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = run_get_rel,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "inst",
          .name_size = strlen("inst"),
          .parse = parse_import_instance,
          .obj = self
        },
        { .name = "inst",
          .name_size = strlen("inst"),
          .parse = parse_rel_inst_select,
          .obj = self
        },
        { .is_default = true,
          .run = run_present_rel,
          .obj = task
        }
    };

    self->curr_rel = NULL;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        /*if (!log->buf_size) {
            e = log->write(log, "rel parse failure",
                           strlen("rel parse failure"));
            if (e) return make_gsl_err_external(e);
            }*/

        if (self->curr_rel) {
            rel = self->curr_rel;
            rel->reset_inbox(rel);
        }

        return parser_err;
    }

    /* any updates happened? */
    if (self->curr_rel) {
        if (self->curr_rel->inbox_size || self->curr_rel->inst_inbox_size) {
            self->curr_rel->next = self->inbox;
            self->inbox = self->curr_rel;
            self->inbox_size++;
        }
    }

    return make_gsl_err(gsl_OK);
}

static int resolve_inst(struct kndRel *self,
                        struct kndRelInstance *inst,
                        struct kndRelUpdate *rel_update)
{
    struct kndRelArg *arg;
    struct kndRelArgInstance *arg_inst;
    struct kndState *state = inst->states;
    int err;

    if (DEBUG_REL_LEVEL_2) {
        knd_log("\n%*s.. resolving Rel Instance: %.*s [id: %.*s]",
                self->depth * KND_OFFSET_SIZE, "",
                self->entry->name_size, self->entry->name,
                inst->id_size, inst->id);
        inst_str(self, inst);
    }
    
    state->update = rel_update->update;
    state->val = (void*)rel_update;

    for (arg_inst = inst->args; arg_inst; arg_inst = arg_inst->next) {
        arg = arg_inst->relarg;
        err = arg->resolve_inst(arg, arg_inst, rel_update);                       RET_ERR();
    }

    inst->num_states++;
    state->numid = inst->num_states;

    return knd_OK;
}

static int kndRel_resolve(struct kndRel *self,
                          struct kndRelUpdate *update)
{
    struct kndRelArg *arg;
    struct kndRelInstance *inst;
    int err;

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. resolving REL: \"%.*s\" "
                " is_resolved:%d   inst inbox size: %zu",
                self->entry->name_size, self->entry->name,
                self->is_resolved, self->inst_inbox_size);

    /* resolve instances */
    if (self->inst_inbox_size) {
        for (inst = self->inst_inbox; inst; inst = inst->next) {
            err = resolve_inst(self, inst, update);                               RET_ERR();
            update->insts[update->num_insts] = inst;
            update->num_insts++;
        }

        self->is_resolved = true;
        if (DEBUG_REL_LEVEL_2)
            knd_log("++ rel resolved: %.*s!",
                    self->entry->name_size, self->entry->name);

        return knd_OK;
    }

    for (arg = self->args; arg; arg = arg->next) {
        err = arg->resolve(arg, NULL);
        if (err) return err;
    }
    self->is_resolved = true;

    return knd_OK;
}

static int resolve_rels(struct kndRel *self)
{
    struct kndRel *rel;
    struct kndRelEntry *entry;
    struct kndRelInstance *inst;
    const char *key;
    void *val;
    int err;

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. resolving rels by \"%.*s\"",
                self->name_size, self->name);

    key = NULL;
    self->rel_name_idx->rewind(self->rel_name_idx);
    do {
        self->rel_name_idx->next_item(self->rel_name_idx, &key, &val);
        if (!key) break;

        entry = (struct kndRelEntry*)val;
        rel = entry->rel;
        
        if (rel->is_resolved) {
            /* check any new instances */
            if (rel->inst_inbox_size) {
                for (inst = rel->inst_inbox; inst; inst = inst->next) {
                    err = resolve_inst(rel, inst, NULL);                                RET_ERR();
                }
            }
            continue;
        }

        err = rel->resolve(rel, NULL);
        if (err) {
            knd_log("-- couldn't resolve the \"%s\" rel :(", rel->name);
            return err;
        }
    } while (key);

    return knd_OK;
}

static int kndRel_coordinate(struct kndRel *self)
{
    struct kndRel *rel;
    struct kndRelEntry *entry;
    const char *key;
    void *val;
    int err;

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. rel coordination in progress ..");

    err = resolve_rels(self);
    if (err) return err;

    /* assign ids */
    key = NULL;
    self->rel_name_idx->rewind(self->rel_name_idx);
    do {
        self->rel_name_idx->next_item(self->rel_name_idx, &key, &val);
        if (!key) break;

        entry = (struct kndRelEntry*)val;
        rel = entry->rel;
        //rel->phase = KND_CREATED;
    } while (key);

    /* display all rels */
    if (DEBUG_REL_LEVEL_2) {
        key = NULL;
        self->rel_name_idx->rewind(self->rel_name_idx);
        do {
            self->rel_name_idx->next_item(self->rel_name_idx, &key, &val);
            if (!key) break;
            entry = (struct kndRelEntry*)val;
            rel = entry->rel;
            rel->depth = self->depth + 1;
            rel->str(rel);
        } while (key);
    }
    return knd_OK;
}

static int kndRel_update_state(struct kndRel *self,
                               struct kndUpdate *update)
{
    struct kndRel *rel;
    struct kndRelUpdate *rel_update;
    struct kndMemPool *mempool = self->task->mempool;
    int err;

    if (DEBUG_REL_LEVEL_TMP)
        knd_log(".. updating Rel state..");

    /* create index of REL updates */
    // TODO
    /*rel_updates = realloc(update->rels,
                          (self->inbox_size * sizeof(struct kndRelUpdate*)));
    if (!rel_updates) return knd_NOMEM;
    update->rels = rel_updates;
    */

    for (rel = self->inbox; rel; rel = rel->next) {
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                                sizeof(struct kndRelUpdate), (void**)&rel_update);        RET_ERR();
        rel_update->rel = rel;
        rel_update->update = update;

        err = rel->resolve(rel, rel_update);                                      RET_ERR();
        //update->rels[update->num_rels] = rel_update;
        //update->num_rels++;
    }

    reset_inbox(self);

    return knd_OK;
}

/*static gsl_err_t set_liquid_rel_id(void *obj,
                                   const char *val,
                                   size_t unused_var(val_size))
{
    struct kndRel *self = (struct kndRel*)obj;
    struct kndRel *rel;
    long numval = 0;
    int err;

    if (!self->curr_rel) return make_gsl_err(gsl_FAIL);
    rel = self->curr_rel;

    err = knd_parse_num((const char*)val, &numval);
    if (err) return make_gsl_err_external(err);
    if (rel->entry) {
        rel->entry->numid = numval;
    }

    if (DEBUG_REL_LEVEL_TMP)
        knd_log(".. set curr liquid rel id: %zu",
                rel->id);

    return make_gsl_err(gsl_OK);
}
*/


 /*
static gsl_err_t run_get_liquid_rel(void *obj, const char *name, size_t name_size)
{
    struct kndRel *self = obj;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    err = get_rel(self, name, name_size, &self->curr_rel);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}
 */

static int export_updates(struct kndRel *self)
{
    struct kndRel *rel;
    struct kndRelInstance *inst;
    struct glbOutput *out = self->out;
    int err;

    for (rel = self->inbox; rel; rel = rel->next) {
        err = out->write(out, "{rel ", strlen("{rel "));   RET_ERR();
        err = out->write(out, rel->name, rel->name_size);

        if (rel->inst_inbox_size) {
            for (inst = self->inst_inbox; inst; inst = inst->next) {
                err = out->write(out, "(id ", strlen("(id "));         RET_ERR();
                err = out->write(out,inst->id, inst->id_size);         RET_ERR();
                err = out->write(out, ")", 1);                         RET_ERR();
            }
        }

        err = out->write(out, "}", 1);                                 RET_ERR();
    }

    return knd_OK;
}

static int freeze_insts(struct kndRel *self,
                        size_t *total_size,
                        struct kndTask *task)
{
    char buf[KND_SHORT_NAME_SIZE];
    size_t buf_size;
    struct kndRelInstance *inst;
    struct kndRelInstEntry *entry;
    struct glbOutput *out = self->out;
    struct glbOutput *trailer = self->dir_out;
    const char *key;
    void *val;
    size_t trailer_size = 0;
    size_t inst_block_offset = 0;
    size_t block_size = out->buf_size;
    int err;

    if (DEBUG_REL_LEVEL_1)
        knd_log(".. freezing insts of Rel \"%.*s\", total:%zu",
                self->name_size, self->name,
                self->entry->num_insts);

    inst_block_offset = out->buf_size;

    err = trailer->write(trailer, "[i", strlen("[i"));
    if (err) return err;

    key = NULL;
    self->entry->inst_name_idx->rewind(self->entry->inst_name_idx);
    do {
        self->entry->inst_name_idx->next_item(self->entry->inst_name_idx, &key, &val);
        if (!key) break;
        entry = val;
        inst = entry->inst;

        /*if (obj->state->phase != KND_CREATED) {
            knd_log("NB: skip freezing \"%.*s\"   phase: %d",
                    obj->name_size, obj->name, obj->state->phase);
            continue;
            }*/

        err = export_inst_GSP(inst, task);
        if (err) {
            knd_log("-- couldn't export GSP of the \"%.*s\" inst :(",
                    inst->name_size, inst->name);
            return err;
        }

        entry->block_size = out->buf_size - inst_block_offset;
        block_size += entry->block_size;

        inst_block_offset = out->buf_size;
        buf_size = 0;
        knd_num_to_gsp_num(entry->block_size, buf, &buf_size);

        err = trailer->writec(trailer, ' ');
        if (err) return err;

        err = trailer->write(trailer, buf, buf_size);
        if (err) return err;

        if (DEBUG_REL_LEVEL_2)
            knd_log("Rel body size: %zu [%.*s]",
                    entry->block_size, buf_size, buf);

    } while (key);

    err = trailer->writec(trailer, ']');
    if (err) return err;
    
    if (out->buf_size) {
        err = knd_append_file(self->frozen_output_file_name,
                              out->buf, out->buf_size);
        if (err) return err;
    }

    trailer_size = trailer->buf_size;

    err = trailer->write(trailer, "{L ", strlen("{L "));
    if (err) return err;
    buf_size = 0;
    knd_num_to_gsp_num(trailer_size, buf, &buf_size);
    err = trailer->write(trailer, buf, buf_size);
    if (err) return err;
    err = trailer->writec(trailer, '}');
    if (err) return err;

    err = knd_append_file(self->frozen_output_file_name,
                          trailer->buf, trailer->buf_size);
    if (err) return err;

    block_size += trailer->buf_size;
    *total_size += block_size;

    return knd_OK;
}

static int freeze(struct kndRel *self,
                  size_t *total_frozen_size,
                  char *output,
                  size_t *total_size,
                  struct kndTask *task)
{
    char buf[KND_SHORT_NAME_SIZE];
    size_t buf_size;
    size_t block_size;
    size_t output_size = 0;
    struct glbOutput *out = task->out;
    struct glbOutput *trailer = self->dir_out;
    int err;

    out->reset(out);
    trailer->reset(trailer);

    err = export_GSP(self, task);
    if (err) {
        knd_log("-- GSP export of %.*s Rel failed :(",
                self->name_size, self->name);
        return err;
    }

    if (DEBUG_REL_LEVEL_2)
        knd_log("== Rel body frozen GSP: %.*s",
                out->buf_size, out->buf);

    err = knd_append_file(self->frozen_output_file_name,
                          out->buf, out->buf_size);
    if (err) return err;
    block_size = out->buf_size;

    /* any instances to freeze? */
    if (self->entry->num_insts) {
        /* mark the size of the Rel body rec */
        err = trailer->write(trailer, "{R ", strlen("{R "));
        if (err) return err;
        buf_size = 0;
        knd_num_to_gsp_num(out->buf_size, buf, &buf_size);
        err = trailer->write(trailer, buf, buf_size);
        if (err) return err;
        err = trailer->writec(trailer, '}');
        if (err) return err;

        out->reset(out);
        err = freeze_insts(self, &block_size, task);       RET_ERR();
    }

    memcpy(output, " ", 1);
    output++;
    output_size++;

    buf_size = 0;
    knd_num_to_gsp_num(block_size, buf, &buf_size);
    memcpy(output, buf, buf_size);
    output      += buf_size;
    output_size += buf_size;

    *total_frozen_size += block_size;
    *total_size = output_size;

    return knd_OK;
}

extern void
kndRel_init(struct kndRel *self)
{
    self->del = del;
    self->str = str;
    self->export = export;
    self->export_updates = export_updates;
    self->resolve = kndRel_resolve;
    self->coordinate = kndRel_coordinate;
    self->freeze = freeze;
    self->unfreeze_inst = unfreeze_inst;
    self->import = import_rel;
    self->get_rel = get_rel;
    self->read = read_GSP;
    self->read_rel = read_rel;
    self->reset_inbox = reset_inbox;
    self->update = kndRel_update_state;
    self->select = parse_rel_select;
    self->read_inst = read_instance;
    self->export_inst = export_inst;
    // TODO formats
    self->export_inst_set = export_inst_set_JSON;
}

extern void
kndRelInstance_init(struct kndRelInstance *self)
{
    memset(self, 0, sizeof(struct kndRelInstance));
}

extern int kndRel_new(struct kndRel **rel,
                      struct kndMemPool *mempool)
{
    struct kndRel *self;
    struct kndRelEntry *entry;
    int err;

    self = malloc(sizeof(struct kndRel));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndRel));

    err = knd_rel_entry_new(mempool, &entry);                               RET_ERR();
    entry->name = "/";
    entry->name_size = 1;
    entry->rel = self;
    self->entry = entry;

    err = ooDict_new(&self->rel_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;
    err = ooDict_new(&self->rel_name_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;

    kndRel_init(self);
    *rel = self;
    return knd_OK;
 error:
    return err;
}

extern int knd_rel_entry_new(struct kndMemPool *mempool,
                              struct kndRelEntry **result)
{
    void *page;
    int err;

    //knd_log("..rel entry new [size:%zu]", sizeof(struct kndRelEntry));

    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL_X2,
                            sizeof(struct kndRelEntry), &page);  RET_ERR();
    *result = page;
    return knd_OK;
}

extern int knd_rel_inst_new(struct kndMemPool *mempool,
                            struct kndRelInstance **result)
{
    void *page;
    int err;

    knd_log("..rel inst [size:%zu]", sizeof(struct kndRelInstance));

    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                            sizeof(struct kndRelInstance), &page);  RET_ERR();
    *result = page;
    return knd_OK;
}

extern int knd_rel_new(struct kndMemPool *mempool,
                       struct kndRel **result)
{
    void *page;
    int err;

    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL_X4,
                            sizeof(struct kndRel), &page);  RET_ERR();
    *result = page;
    kndRel_init(*result);
    return knd_OK;
}
