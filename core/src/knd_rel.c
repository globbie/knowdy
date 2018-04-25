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
#include "knd_object.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_concept.h"
#include "knd_mempool.h"
#include "knd_text.h"
#include "knd_utils.h"

#define DEBUG_REL_LEVEL_0 0
#define DEBUG_REL_LEVEL_1 0
#define DEBUG_REL_LEVEL_2 0
#define DEBUG_REL_LEVEL_3 0
#define DEBUG_REL_LEVEL_TMP 1

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
    if (inst->obj)
        knd_log("            OBJ: \"%.*s\"",
                inst->obj->name_size, inst->obj->name);
    if (inst->val_size)
        knd_log("            VAL: \"%.*s\"",
                inst->val_size, inst->val);
}

static void inst_str(struct kndRel *self, struct kndRelInstance *inst)
{
    struct kndRelArgInstance *arg;

    knd_log("\n%*sRel Instance: %.*s NAME:%.*s ID:%.*s", self->depth * KND_OFFSET_SIZE, "",
            inst->rel->name_size, inst->rel->name,
            inst->name_size, inst->name,
            inst->id_size, inst->id);

    for (arg = inst->args; arg; arg = arg->next) {
        inst_arg_str(arg);
    }
}

static gsl_err_t run_present_rel(void *obj __attribute__((unused)),
                                 const char *val __attribute__((unused)),
                                 size_t val_size __attribute__((unused)))
{
    if (DEBUG_REL_LEVEL_TMP)
        knd_log(".. present rel..");


    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_select_rel(void *obj __attribute__((unused)),
                                const char *val __attribute__((unused)),
                                size_t val_size __attribute__((unused)))
{
    if (DEBUG_REL_LEVEL_TMP)
        knd_log(".. present rel..");


    return make_gsl_err(gsl_OK);
}

static gsl_err_t alloc_gloss_item(void *obj,
                                  const char *name,
                                  size_t name_size,
                                  size_t count,
                                  void **item)
{
    struct kndRel *self = obj;
    struct kndTranslation *tr;

    assert(name == NULL && name_size == 0);

    if (DEBUG_REL_LEVEL_2)
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
    struct kndRel *self = accu;
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

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. read gloss translation: \"%.*s\",  text: \"%.*s\"",
                tr->locale_size, tr->locale, tr->val_size, tr->val);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_gloss(void *obj,
                             const char *rec,
                             size_t *total_size)
{
    struct kndRel *self = obj;
    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .alloc = alloc_gloss_item,
        .append = append_gloss_item,
        .accu = self,
        .parse = parse_gloss_item
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

static int export_JSON(struct kndRel *self)
{
    struct glbOutput *out = self->out;
    struct kndRelArg *arg;
    struct kndTranslation *tr;
    int err;

    err = out->write(out, "{", 1);
    if (err) return err;

    err = out->write(out, self->name, self->name_size);
    if (err) return err;

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
        err = out->write(out, " ", 1);
        if (err) return err;
        err = out->write(out, tr->val,  tr->val_size);
        if (err) return err;
        err = out->write(out, "}", 1);
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

static int export_GSP(struct kndRel *self)
{
    struct glbOutput *out = self->out;
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

static int export_inst_GSP(struct kndRel *self,
                           struct kndRelInstance *inst)
{
    struct kndRelArg *relarg;
    struct kndRelArgInstance *relarg_inst;
    struct glbOutput *out = self->out;
    //struct kndObjEntry *entry = NULL;
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

static int export_inst_JSON(struct kndRel *self,
                            struct kndRelInstance *inst)
{
    struct kndRelArg *relarg;
    struct kndRelArgInstance *relarg_inst;
    struct glbOutput *out = self->out;
    bool in_list = false;
    int err;

    err = out->writec(out, '{');
    if (err) return err;

    err = out->write(out, "\"_name\":\"", strlen("\"_name\":\""));                       RET_ERR();
                     err = out->write(out, inst->name, inst->name_size);           RET_ERR();
                     err = out->writec(out, '"');
                     in_list = true;
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
        if (in_list) {
            err = out->writec(out, ',');                                         RET_ERR();
        }
        err = out->write(out, "\"", strlen("\""));                               RET_ERR();
        err = out->write(out, relarg->name, relarg->name_size);                  RET_ERR();
        err = out->write(out, "\":", strlen("\":"));                             RET_ERR();

        err = relarg->export_inst(relarg, relarg_inst);                          RET_ERR();

        in_list = true;
    }

    err = out->write(out, "}", 1);
    if (err) return err;

    return knd_OK;
}

static int export(struct kndRel *self)
{
    int err;

    switch (self->format) {
    case KND_FORMAT_JSON:
        err = export_JSON(self);
        if (err) return err;
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

static int export_inst(struct kndRel *self,
                       struct kndRelInstance *inst)
{
    int err;

    switch (self->format) {
    case KND_FORMAT_JSON:
        err = export_inst_JSON(self, inst);
        if (err) return err;
        break;
    case KND_FORMAT_GSP:
        err = export_inst_GSP(self, inst);
        if (err) return err;
        break;
    default:
        break;
    }

    return knd_OK;
}


static gsl_err_t validate_rel_arg(void *obj,
                                  const char *name, size_t name_size,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndRel *self = obj;
    struct kndRelArg *arg;
    int err;

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. parsing the \"%.*s\" rel arg, rec:\"%.*s\"", name_size, name, 32, rec);

    /* TODO mempool */
    err = kndRelArg_new(&arg);
    if (err) return make_gsl_err_external(err);
    arg->rel = self;

    if (!strncmp(name, "subj", strlen("subj"))) {
        arg->type = KND_RELARG_SUBJ;
    } else if (!strncmp(name, "obj", strlen("obj"))) {
        arg->type = KND_RELARG_OBJ;
    } else if (!strncmp(name, "ins", strlen("ins"))) {
        arg->type = KND_RELARG_INS;
    }

    err = arg->parse(arg, rec, total_size);
    if (err) {
        if (DEBUG_REL_LEVEL_TMP)
            knd_log("-- failed to parse rel arg: %d", err);
        return make_gsl_err_external(err);
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

    if (DEBUG_REL_LEVEL_2)
        arg->str(arg);

    return make_gsl_err(gsl_OK);
}

static int import_rel(struct kndRel *self,
                      const char *rec,
                      size_t *total_size)
{
    struct kndRel *rel;
    struct kndRelDir *dir;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. import Rel: \"%.*s\"..", 32, rec);

    err  = self->mempool->new_rel(self->mempool, &rel);
    if (err) return err;

    rel->out = self->out;
    rel->log = self->log;
    rel->task = self->task;
    rel->mempool = self->mempool;
    rel->rel_name_idx = self->rel_name_idx;
    rel->class_idx = self->class_idx;
    rel->class_name_idx = self->class_name_idx;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = rel
        }/*,
        { .type = GSL_SET_STATE,
          .name = "base",
          .name_size = strlen("base"),
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
          .is_validator = true,
          .validate = validate_rel_arg,
          .obj = rel
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        err = gsl_err_to_knd_err_codes(parser_err);
        goto final;
    }

    if (!rel->name_size) {
        err = knd_FAIL;
        goto final;
    }

    dir = (struct kndRelDir*)self->rel_name_idx->get(self->rel_name_idx,
                                                     rel->name, rel->name_size);
    if (dir) {
        knd_log("-- %s relation name doublet found :(", rel->name);
        self->log->reset(self->log);
        err = self->log->write(self->log,
                               rel->name,
                               rel->name_size);
        if (err) goto final;
        err = self->log->write(self->log,
                               " relation name already exists",
                               strlen(" relation name already exists"));
        if (err) goto final;
        err = knd_FAIL;
        goto final;
    }

    if (!self->batch_mode) {
        rel->next = self->inbox;
        self->inbox = rel;
        self->inbox_size++;
    }

    err = self->mempool->new_rel_dir(self->mempool, &dir);                        RET_ERR();
    dir->rel = rel;
    dir->mempool = rel->mempool;
    rel->dir = dir;
    self->num_rels++;
    dir->numid = self->num_rels;

    knd_num_to_str(dir->numid, dir->id, &dir->id_size, KND_RADIX_BASE);

    /* automatic name assignment if no explicit name given */
    memcpy(rel->dir->name, rel->name, rel->name_size);
    rel->dir->name_size = rel->name_size;

    if (!rel->name_size) {
        memcpy(dir->name, dir->id, dir->id_size);
        dir->name_size = dir->id_size;
    }
    rel->name = dir->name;
    rel->name_size = dir->name_size;
    rel->id = dir->id;
    rel->id_size = dir->id_size;

    err = self->rel_name_idx->set(self->rel_name_idx,
                                  dir->name, dir->name_size, (void*)dir);
    if (err) goto final;

    if (DEBUG_REL_LEVEL_1)
        rel->str(rel);

    return knd_OK;
 final:

    return err;
}

static gsl_err_t confirm_rel_read(void *obj,
                                  const char *val __attribute__((unused)),
                                  size_t val_size __attribute__((unused)))
{
    struct kndRel *self = obj;

    if (DEBUG_REL_LEVEL_2)
        knd_log("== REL %.*s read OK!",
                self->name_size, self->name);

    return make_gsl_err(gsl_OK);
}


static int read_rel_incipit(struct kndRel *self,
                            struct kndRelDir *dir)
{
    char buf[KND_NAME_SIZE + 1];
    size_t buf_size;
    off_t offset = 0;
    int fd = self->fd;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_REL_LEVEL_2)
        knd_log("\n.. get rel name, global offset:%zu  block size:%zu",
                dir->global_offset, dir->block_size);

    buf_size = dir->block_size;
    if (dir->block_size > KND_NAME_SIZE)
        buf_size = KND_NAME_SIZE;

    offset = dir->global_offset;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        return knd_IO_FAIL;
    }
    err = read(fd, buf, buf_size);
    if (err == -1) return knd_IO_FAIL;

    if (DEBUG_REL_LEVEL_2)
        knd_log("== REL BODY incipit: %.*s",
                buf_size, buf);
    dir->id_size = KND_ID_SIZE;
    dir->name_size = KND_NAME_SIZE;
    err = knd_parse_incipit(buf, buf_size,
                            dir->id, &dir->id_size,
                            dir->name, &dir->name_size);
    if (err) return err;

    if (DEBUG_REL_LEVEL_1)
        knd_log("== REL NAME:\"%.*s\" ID:%.*s",
                dir->name_size, dir->name, dir->id_size, dir->id);

    return knd_OK;
}


static gsl_err_t inst_entry_alloc(void *obj,
                                  const char *val,
                                  size_t val_size,
                                  size_t count  __attribute__((unused)),
                                  void **item)
{
    struct kndRelDir *parent_dir = obj;
    struct kndRelInstEntry *entry = NULL;

    if (DEBUG_REL_LEVEL_1)
        knd_log(".. create Rel inst entry: %.*s  dir: %p",
                val_size, val, parent_dir);

    entry = malloc(sizeof(struct kndRelInstEntry));
    if (!entry) return make_gsl_err_external(knd_NOMEM);
    memset(entry, 0, sizeof(struct kndRelInstEntry));

    knd_calc_num_id(val, val_size, &entry->block_size);

    *item = entry;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t inst_entry_append(void *accu,
                                   void *item)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndRelDir *parent_dir = accu;
    struct kndRelInstEntry *entry = item;
    struct kndSet *set;
    off_t offset = 0;
    int fd = parent_dir->fd;
    gsl_err_t parser_err;
    int err;

    entry->offset = parent_dir->curr_offset;

    if (DEBUG_REL_LEVEL_2)
        knd_log("\n.. RelDir: \"%.*s\" to append atomic entry"
                " (block size: %zu) offset:%zu",
                parent_dir->name_size, parent_dir->name,
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

    parent_dir->curr_offset += entry->block_size;
    

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
    
    set = parent_dir->inst_idx;
    if (!set) {
        err = parent_dir->mempool->new_set(parent_dir->mempool, &set);
        if (err) return make_gsl_err_external(err);
        set->type = KND_SET_REL_INST;
        parent_dir->inst_idx = set;
    }

    err = set->add(set, entry->id, entry->id_size, (void*)entry);
    if (err) return make_gsl_err_external(err);

    /* update name idx */
    /*err = parent_dir->obj_name_idx->set(parent_dir->obj_name_idx,
                                        entry->name, entry->name_size,
                                        entry);
    if (err) return make_gsl_err_external(err);
    */
    /*err = parent_dir->inst_name_idx->set(parent_dir->inst_name_idx,
                                         inst->name, inst->name_size, (void*)entry);
    if (err) return make_gsl_err_external(err);
    */
    return make_gsl_err(gsl_OK);
}


static gsl_err_t set_rel_body_size(void *obj, const char *val, size_t val_size)
{
    struct kndRelDir *self = obj;

    if (!val_size) return make_gsl_err(gsl_FORMAT);
    if (val_size >= KND_SHORT_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    knd_calc_num_id(val, val_size, &self->body_size);

    if (DEBUG_REL_LEVEL_1)
        knd_log("== Rel body size: %.*s => %zu",
                val_size, val, self->body_size);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_rel_body_size(void *obj,
                                       const char *rec,
                                       size_t *total_size)
{
    struct kndRelDir *self = obj;
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
                             struct kndRelDir *parent_dir)
{
    char *dir_buf = self->out->buf;
    size_t dir_buf_size = self->out->buf_size;
    size_t parsed_size = 0;
    gsl_err_t parser_err;

    struct gslTaskSpec inst_dir_spec = {
        .is_list_item = true,
        .accu = parent_dir,
        .alloc = inst_entry_alloc,
        .append = inst_entry_append,
    };
    
    struct gslTaskSpec specs[] = {
        { .name = "R",
          .name_size = strlen("R"),
          .parse = parse_rel_body_size,
          .obj = parent_dir
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "i",
          .name_size = strlen("i"),
          .parse = gsl_parse_array,
          .obj = &inst_dir_spec
        }
    };

    parent_dir->curr_offset = parent_dir->global_offset;

    if (DEBUG_REL_LEVEL_2)
        knd_log("  .. parsing \"%.*s\" DIR REC: \"%.*s\"  curr offset: %zu   [dir size:%zu]",
                KND_ID_SIZE, parent_dir->id, dir_buf_size, dir_buf,
                parent_dir->curr_offset, dir_buf_size);

    parser_err = gsl_parse_task(dir_buf, &parsed_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);
    
    return knd_OK;
}

static int read_dir_trailer(struct kndRel *self,
                            struct kndRelDir *parent_dir)
{
    size_t block_size = parent_dir->block_size;
    struct glbOutput *out = self->out;
    off_t offset = 0;
    size_t dir_size = 0;
    size_t chunk_size = 0;
    int fd = self->fd;
    const char *val = NULL;
    size_t val_size = 0;
    gsl_err_t parser_err;
    int err;

    if (block_size <= KND_DIR_ENTRY_SIZE)
        return knd_NO_MATCH;

    offset = (parent_dir->global_offset + block_size) - KND_DIR_ENTRY_SIZE;
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

    knd_calc_num_id(val, val_size, &dir_size);

    if (DEBUG_REL_LEVEL_2)
        knd_log("== Rel DIR size: %.*s [chunk size:%zu] => %zu",
                val_size, val, chunk_size, dir_size);

    parent_dir->body_size = block_size - dir_size - chunk_size;
    parent_dir->dir_size = dir_size;

    if (DEBUG_REL_LEVEL_2)
        knd_log("  .. DIR: offset: %lu  block: %lu  body: %lu  dir: %lu",
                (unsigned long)parent_dir->global_offset,
                (unsigned long)parent_dir->block_size,
                (unsigned long)parent_dir->body_size,
                (unsigned long)parent_dir->dir_size);

    offset = (parent_dir->global_offset + block_size) - chunk_size - dir_size;
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

    parent_dir->fd = fd;
    err = parse_dir_trailer(self, parent_dir);
    if (err) {
        knd_log("-- failed to parse Rel dir trailer: \"%.*s\"",
                out->buf_size, out->buf);
        return err;
    }

    return knd_OK;
}


static int read_rel(struct kndRel *self,
                    struct kndRelDir *dir)
{
    int err;

    err = read_rel_incipit(self, dir);
    if (err) return err;

    err = self->rel_name_idx->set(self->rel_name_idx,
                                  dir->name, dir->name_size, (void*)dir);
    if (err) return err;

    err = self->rel_idx->set(self->rel_idx,
                             dir->id, dir->id_size, (void*)dir);                  RET_ERR();

    err = read_dir_trailer(self, dir);
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

static int read_GSP(struct kndRel *self,
                    const char *rec,
                    size_t *total_size)
{
    gsl_err_t parser_err;

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
        { .is_validator = true,
          .validate = validate_rel_arg,
          .obj = self
        },
        { .is_default = true,
          .run = confirm_rel_read,
          .obj = self
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    return knd_OK;
}

static int unfreeze_inst(struct kndRel *self,
                         struct kndRelInstEntry *entry)
{
    struct kndRelInstance *inst;
    const char *filename;
    size_t filename_size;
    const char *c, *b, *e;
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

    err = self->mempool->new_rel_inst(self->mempool, &inst);                      RET_ERR();
    err = self->mempool->new_state(self->mempool, &inst->state);                  RET_ERR();
    inst->state->phase = KND_FROZEN;
    inst->entry = entry;
    entry->inst = inst;
    inst->rel = self;

    /* skip over initial brace '{' */
    c = entry->block + 1;
    b = c;
    bool got_separ = false;
    /* ff the name */
    while (*c) {
        switch (*c) {
        case '{':
        case '}':
        case '[':
        case ']':
            got_separ = true;
            e = c;
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

    if (DEBUG_REL_LEVEL_TMP)
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
    size_t chunk_size = 0;
    size_t *total_size;
    struct kndRelDir *dir;
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

    if (DEBUG_REL_LEVEL_1)
        knd_log(".. get rel: \"%.*s\"",
                name_size, name);

    dir = self->rel_name_idx->get(self->rel_name_idx,
                                  name, name_size);
    if (!dir) {
        knd_log("-- no such rel: \"%.*s\" :(", name_size, name);
        self->log->reset(self->log);
        err = self->log->write(self->log, name, name_size);
        if (err) return err;
        err = self->log->write(self->log, " Rel name not found",
                               strlen(" Rel name not found"));
        if (err) return err;
        return knd_NO_MATCH;
    }

    if (dir->rel) {
        rel = dir->rel;
        rel->phase = KND_SELECTED;
        rel->task = self->task;
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

    if (lseek(fd, dir->global_offset, SEEK_SET) == -1) {
        err = knd_IO_FAIL;
        goto final;
    }

    buf_size = dir->body_size;
    if (buf_size >= KND_TEMP_BUF_SIZE) {
        knd_log("-- block too large: %zu", dir->body_size);
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

    err = self->mempool->new_rel(self->mempool, &rel);                            RET_ERR();
    rel->out = self->out;
    rel->log = self->log;
    rel->task = self->task;
    rel->mempool = self->mempool;
    rel->rel_name_idx = self->rel_name_idx;
    rel->class_idx = self->class_idx;
    rel->class_name_idx = self->class_name_idx;
    rel->dir = dir;

    rel->name = dir->name;
    rel->name_size = dir->name_size;

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
    err = rel->read(rel, b, total_size);                                          PARSE_ERR();

    /* resolve args of a Rel */
    for (arg = rel->args; arg; arg = arg->next) {
        err = arg->resolve(arg);                                                  RET_ERR();
    }

    dir->rel = rel;
    rel->dir = dir;

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
    struct kndMemPool *mempool = inst->rel->mempool;
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
        return make_gsl_err(gsl_FAIL);
    }

    err = mempool->new_rel_arg_inst(mempool, &arg_inst);
    if (err) return make_gsl_err_external(err);
    arg_inst->relarg = arg;
    arg_inst->rel_inst = inst;

    err = arg->parse_inst(arg, arg_inst, rec, total_size);
    if (err) {
        knd_log("-- failed to parse rel arg instance: %d", err);
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
        { .is_validator = true,
          .validate = parse_rel_arg_inst,
          .obj = inst
        },
        { .type = GSL_SET_STATE,
          .is_validator = true,
          .validate = parse_rel_arg_inst,
          .obj = inst
        },
        { .is_default = true,
          .run = run_select_rel,
          .obj = inst
        }
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
    int err;
    gsl_err_t parser_err;

    if (DEBUG_REL_LEVEL_2) {
        knd_log(".. import \"%.*s\" rel instance..", 128, rec);
    }

    if (!self->curr_rel) {
        knd_log("-- curr rel not set :(");
        return make_gsl_err(gsl_FAIL);
    }

    if (self->task->type != KND_LIQUID_STATE)
        self->task->type = KND_UPDATE_STATE;

    err = self->mempool->new_rel_inst(self->mempool, &inst);
    if (err) return make_gsl_err_external(err);
    err = self->mempool->new_state(self->mempool, &inst->state);
    if (err) return make_gsl_err_external(err);

    inst->state->phase = KND_SUBMITTED;
    inst->rel = self->curr_rel;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_inst_name,
          .obj = inst
        },
        { .type = GSL_SET_STATE,
          .is_validator = true,
          .validate = parse_rel_arg_inst,
          .obj = inst
        },
        { .is_default = true,
          .run = run_select_rel,
          .obj = inst
        }
   };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;
    rel = inst->rel;

    /* save in inbox */
    inst->next = rel->inst_inbox;
    rel->inst_inbox = inst;
    rel->inst_inbox_size++;
    /*if (!rel->dir) {
        return make_gsl_err(gsl_FAIL);
        }*/
    rel->dir->num_insts++;

    /* assign id/name */
    inst->numid = rel->dir->num_insts;
    knd_num_to_str(inst->numid, inst->id, &inst->id_size, KND_RADIX_BASE);

    /* automatic name assignment if no explicit name given */
    if (!inst->name_size) {
        inst->name = inst->id;
        inst->name_size = inst->id_size;
    }

    if (DEBUG_REL_LEVEL_TMP)
        inst_str(rel, inst);
    
    if (!rel->dir->inst_idx) {
        err = self->mempool->new_set(self->mempool, &rel->dir->inst_idx);
        if (err) return make_gsl_err_external(err);
        rel->dir->inst_idx->type = KND_SET_REL_INST;
    }

    if (!rel->dir->inst_name_idx) {
        err = ooDict_new(&rel->dir->inst_name_idx, KND_MEDIUM_DICT_SIZE);
        if (err) return make_gsl_err_external(err);
    }
    
    entry = malloc(sizeof(struct kndRelInstEntry));
    if (!entry) return make_gsl_err_external(knd_NOMEM);
    memset(entry, 0, sizeof(struct kndRelInstEntry));
    entry->inst = inst;

    err = rel->dir->inst_name_idx->set(rel->dir->inst_name_idx,
                                       inst->name, inst->name_size, (void*)entry);
    if (err) return make_gsl_err_external(err);

    if (DEBUG_REL_LEVEL_TMP) {
        knd_log("\n\nREGISTER INST in %.*s IDX:  [total:%zu valid:%zu]",
                rel->name_size, rel->name, rel->dir->inst_name_idx->size, rel->dir->num_insts);
        inst_str(rel, inst);
    }

    return make_gsl_err(gsl_OK);
}

static int parse_rel_select(struct kndRel *self,
                            const char *rec,
                            size_t *total_size)
{
    int e;
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
        { .is_default = true,
          .run = run_present_rel,
          .obj = self
        }
    };

    self->curr_rel = NULL;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- rel parse error: \"%.*s\"",
                self->log->buf_size, self->log->buf);
        if (!self->log->buf_size) {
            e = self->log->write(self->log, "rel parse failure",
                                 strlen("rel parse failure"));
            if (e) return e;
        }
        return gsl_err_to_knd_err_codes(parser_err);
    }

    /* any updates happened? */
    if (self->curr_rel) {
        if (self->curr_rel->inbox_size || self->curr_rel->inst_inbox_size) {
            self->curr_rel->next = self->inbox;
            self->inbox = self->curr_rel;
            self->inbox_size++;
        }
    }

    return knd_OK;
}

static int resolve_inst(struct kndRel *self,
                        struct kndRelInstance *inst)
{
    struct kndRelArg *arg;
    struct kndRelArgInstance *arg_inst;
    int err;

    if (DEBUG_REL_LEVEL_TMP)
        knd_log("\n%*s.. resolving Rel Instance: %.*s [%zu]",
                self->depth * KND_OFFSET_SIZE, "",
                self->name_size, self->name, inst->id);

    for (arg_inst = inst->args; arg_inst; arg_inst = arg_inst->next) {
        arg = arg_inst->relarg;
        err = arg->resolve_inst(arg, arg_inst);                            RET_ERR();
    }

    return knd_OK;
}

static int kndRel_resolve(struct kndRel *self)
{
    struct kndRelArg *arg;
    struct kndRelInstance *inst;
    int err;

    if (DEBUG_REL_LEVEL_2)
        knd_log("\n.. resolving REL: \"%.*s\"  is_resolved:%d   inst inbox size: %zu",
                self->name_size, self->name, self->is_resolved, self->inst_inbox_size);

    /* resolve instances */
    if (self->inst_inbox_size) {
        for (inst = self->inst_inbox; inst; inst = inst->next) {
            err = resolve_inst(self, inst);                                RET_ERR();
        }
        self->is_resolved = true;
        return knd_OK;
    }

    for (arg = self->args; arg; arg = arg->next) {
        err = arg->resolve(arg);
        if (err) return err;
    }
    self->is_resolved = true;

    return knd_OK;
}

static int resolve_rels(struct kndRel *self)
{
    struct kndRel *rel;
    struct kndRelDir *dir;
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

        dir = (struct kndRelDir*)val;
        rel = dir->rel;
        
        if (rel->is_resolved) {
            /* check any new instances */
            if (rel->inst_inbox_size) {
                for (inst = rel->inst_inbox; inst; inst = inst->next) {
                    err = resolve_inst(rel, inst);                                RET_ERR();
                }
            }
            continue;
        }

        err = rel->resolve(rel);
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
    struct kndRelDir *dir;
    const char *key;
    void *val;
    int err;

    if (DEBUG_REL_LEVEL_1)
        knd_log(".. rel coordination in progress ..");

    err = resolve_rels(self);
    if (err) return err;

    /* assign ids */
    key = NULL;
    self->rel_name_idx->rewind(self->rel_name_idx);
    do {
        self->rel_name_idx->next_item(self->rel_name_idx, &key, &val);
        if (!key) break;

        dir = (struct kndRelDir*)val;
        rel = dir->rel;
        rel->phase = KND_CREATED;
    } while (key);

    /* display all rels */
    if (DEBUG_REL_LEVEL_2) {
        key = NULL;
        self->rel_name_idx->rewind(self->rel_name_idx);
        do {
            self->rel_name_idx->next_item(self->rel_name_idx, &key, &val);
            if (!key) break;
            dir = (struct kndRelDir*)val;
            rel = dir->rel;
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
    struct kndRelUpdate **rel_updates;
    int err;

    if (DEBUG_REL_LEVEL_1)
        knd_log(".. updating Rel state..");

    /* create index of REL updates */
    rel_updates = realloc(update->rels,
                          (self->inbox_size * sizeof(struct kndRelUpdate*)));
    if (!rel_updates) return knd_NOMEM;
    update->rels = rel_updates;

    for (rel = self->inbox; rel; rel = rel->next) {
        err = rel->resolve(rel);                                                  RET_ERR();
        err = self->mempool->new_rel_update(self->mempool, &rel_update);          RET_ERR();

        rel_update->rel = rel;
        update->rels[update->num_rels] = rel_update;
        update->num_rels++;
    }

    reset_inbox(self);

    return knd_OK;
}

static gsl_err_t set_liquid_rel_id(void *obj, const char *val, size_t val_size __attribute__((unused)))
{
    struct kndRel *self = (struct kndRel*)obj;
    struct kndRel *rel;
    long numval = 0;
    int err;

    if (!self->curr_rel) return make_gsl_err(gsl_FAIL);
    rel = self->curr_rel;

    err = knd_parse_num((const char*)val, &numval);
    if (err) return make_gsl_err_external(err);
    //rel->id = numval;
    if (rel->dir) {
        rel->dir->numid = numval;
    }

    //self->curr_rel->update_id = self->curr_update->id;

    if (DEBUG_REL_LEVEL_TMP)
        knd_log(".. set curr liquid rel id: %zu",
                rel->id);

    return make_gsl_err(gsl_OK);
}


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

static gsl_err_t parse_liquid_rel_id(void *obj,
                                     const char *rec, size_t *total_size)
{
    struct kndRel *self = obj;
    struct kndUpdate *update = self->curr_update;
    struct kndRel *rel;
    struct kndRelUpdate *rel_update;
    struct kndRelUpdateRef *rel_update_ref;
    int err;
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_liquid_rel_id,
          .obj = self
        }
    };

    if (!self->curr_rel) return make_gsl_err(gsl_FAIL);

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    rel = self->curr_rel;

    /* register rel update */
    err = self->mempool->new_rel_update(self->mempool, &rel_update);
    if (err) return make_gsl_err_external(err);
    rel_update->rel = rel;

    update->rels[update->num_rels] = rel_update;
    update->num_rels++;

    err = self->mempool->new_rel_update_ref(self->mempool, &rel_update_ref);
    if (err) return make_gsl_err_external(err);
    rel_update_ref->update = update;

    rel_update_ref->next = rel->updates;
    rel->updates =  rel_update_ref;
    rel->num_updates++;

    return make_gsl_err(gsl_OK);
}

static int parse_liquid_updates(struct kndRel *self,
                                const char *rec,
                                size_t *total_size)
{
    struct kndUpdate *update = self->curr_update;
    struct kndRelUpdate **rel_updates;
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_get_liquid_rel,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "id",
          .name_size = strlen("id"),
          .parse = parse_liquid_rel_id,
          .obj = self
        }
    };
    gsl_err_t parser_err;

    if (DEBUG_REL_LEVEL_2)
        knd_log("..parsing liquid REL updates..");

    /* create index of rel updates */
    rel_updates = realloc(update->rels,
                          (self->inbox_size * sizeof(struct kndRelUpdate*)));
    if (!rel_updates) return knd_NOMEM;
    update->rels = rel_updates;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    return knd_OK;
}

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
                       size_t *total_size)
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
    size_t start_offset = 0;
    size_t inst_block_offset = 0;
    size_t block_size = out->buf_size;
    int err;

    if (DEBUG_REL_LEVEL_1)
        knd_log(".. freezing insts of Rel \"%.*s\", total:%zu",
                self->name_size, self->name,
                self->dir->num_insts);

    start_offset = out->buf_size;
    inst_block_offset = out->buf_size;

    err = trailer->write(trailer, "[i", strlen("[i"));
    if (err) return err;

    key = NULL;
    self->dir->inst_name_idx->rewind(self->dir->inst_name_idx);
    do {
        self->dir->inst_name_idx->next_item(self->dir->inst_name_idx, &key, &val);
        if (!key) break;
        entry = val;
        inst = entry->inst;

        /*if (obj->state->phase != KND_CREATED) {
            knd_log("NB: skip freezing \"%.*s\"   phase: %d",
                    obj->name_size, obj->name, obj->state->phase);
            continue;
            }*/

        err = export_inst_GSP(self, inst);
        if (err) {
            knd_log("-- couldn't export GSP of the \"%.*s\" inst :(",
                    inst->name_size, inst->name);
            return err;
        }

        entry->block_size = out->buf_size - inst_block_offset;
        block_size += entry->block_size;

        inst_block_offset = out->buf_size;
        buf_size = 0;
        knd_num_to_str(entry->block_size, buf, &buf_size, KND_RADIX_BASE);

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
    knd_num_to_str(trailer_size, buf, &buf_size, KND_RADIX_BASE);
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
                  size_t *total_size)
{
    char buf[KND_SHORT_NAME_SIZE];
    size_t buf_size;
    size_t block_size;
    size_t output_size = 0;
    struct glbOutput *out = self->out;
    struct glbOutput *trailer = self->dir_out;
    int err;

    out->reset(out);
    trailer->reset(trailer);

    err = export_GSP(self);
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
    if (self->dir->num_insts) {
        /* mark the size of the Rel body rec */
        err = trailer->write(trailer, "{R ", strlen("{R "));
        if (err) return err;
        buf_size = 0;
        knd_num_to_str(out->buf_size, buf, &buf_size, KND_RADIX_BASE);
        err = trailer->write(trailer, buf, buf_size);
        if (err) return err;
        err = trailer->writec(trailer, '}');
        if (err) return err;

        out->reset(out);
        err = freeze_insts(self, &block_size);       RET_ERR();
    }

    memcpy(output, " ", 1);
    output++;
    output_size++;

    buf_size = 0;
    knd_num_to_str(block_size, buf, &buf_size, KND_RADIX_BASE);
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
    self->parse_liquid_updates = parse_liquid_updates;
    self->select = parse_rel_select;
    self->read_inst = read_instance;
    self->export_inst = export_inst;
}

extern void
kndRelInstance_init(struct kndRelInstance *self)
{
    memset(self, 0, sizeof(struct kndRelInstance));
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
