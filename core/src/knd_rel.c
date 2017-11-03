#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

#include "knd_rel.h"
#include "knd_rel_arg.h"
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

static void str(struct kndRel *self)
{
    struct kndRelArg *arg;

    knd_log("\n%*sREL: %.*s", self->depth * KND_OFFSET_SIZE, "", self->name_size, self->name);

    for (arg = self->args; arg; arg = arg->next) {
        arg->depth = self->depth + 1;
        arg->str(arg);
    }
    
}

static int run_set_translation_text(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndTranslation *tr = (struct kndTranslation*)obj;
    struct kndTaskArg *arg;
    const char *val = NULL;
    size_t val_size = 0;

    if (DEBUG_REL_LEVEL_2)
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

    if (DEBUG_REL_LEVEL_2)
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
        /*err = export_reverse_rel_JSON(self);
          if (err) return err; */
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

static int validate_rel_arg(void *obj,
                            const char *name, size_t name_size,
                            const char *rec,
                            size_t *total_size)
{
    struct kndRel *self = obj;
    struct kndRelArg *arg;
    int err;

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. parsing the \"%.*s\" rel arg, rec:\"%.*s\"", name_size, name, 32, rec);

    err = kndRelArg_new(&arg);
    if (err) return err;
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
        return err;
    }

    if (!self->tail_arg) {
        self->tail_arg = arg;
        self->args = arg;
    }
    else {
        self->tail_arg->next = arg;
        self->tail_arg = arg;
    }

    if (DEBUG_REL_LEVEL_2)
        arg->str(arg);

    return knd_OK;
}

static int import_rel(struct kndRel *self,
                      const char *rec,
                      size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndRel *rel;
    struct kndRelDir *dir;
    int err;

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. import Rel: \"%.*s\"..", 32, rec);

    err  = self->mempool->new_rel(self->mempool, &rel);
    if (err) return err;

    rel->out = self->out;
    rel->log = self->log;
    rel->task = self->task;
    rel->mempool = self->mempool;
    rel->rel_idx = self->rel_idx;
    rel->class_idx = self->class_idx;

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
        },
        { .type = KND_CHANGE_STATE,
          .name = "arg",
          .name_size = strlen("arg"),
          .buf = buf,
          .buf_size = &buf_size,
          .max_buf_size = KND_NAME_SIZE,
          .is_validator = true,
          .validate = validate_rel_arg,
          .obj = rel
        }
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) goto final;

    if (!rel->name_size) {
        err = knd_FAIL;
        goto final;
    }

    dir = (struct kndRelDir*)self->rel_idx->get(self->rel_idx,
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

    dir = malloc(sizeof(struct kndRelDir));
    memset(dir, 0, sizeof(struct kndRelDir));
    dir->rel = rel;
    rel->dir = dir;
    err = self->rel_idx->set(self->rel_idx,
                             rel->name, rel->name_size, (void*)dir);
    if (err) goto final;

    if (DEBUG_REL_LEVEL_TMP)
        rel->str(rel);

    return knd_OK;
 final:
    
    rel->del(rel);
    return err;
}


static int get_rel(struct kndRel *self,
                   const char *name, size_t name_size,
                   struct kndRel **result)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    size_t chunk_size;
    struct kndRelDir *dir;
    struct kndRel *rel;
    const char *filename;
    size_t filename_size;
    const char *b;
    struct stat st;
    int fd;
    size_t file_size = 0;
    struct stat file_info;
    int err;

    if (DEBUG_REL_LEVEL_TMP)
        knd_log(".. %.*s to get rel: \"%.*s\"..",
                self->name_size, self->name, name_size, name);

    dir = (struct kndRelDir*)self->rel_idx->get(self->rel_idx, name, name_size);
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
        knd_log("-- error reading FILE \"%.*s\": %d", filename_size, filename, fd);
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

    buf_size = dir->block_size;
    err = read(fd, buf, buf_size);
    if (err == -1) {
        err = knd_IO_FAIL;
        goto final;
    }
    buf[buf_size] = '\0';

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== frozen Rel REC: \"%.*s\"", buf_size, buf);

    /* done reading */
    close(fd);

    /*err = kndConcept_new(&c);
    if (err) return err;

    memcpy(c->name, dir->name, dir->name_size);
    c->name_size = dir->name_size;
    c->out = self->out;
    c->log = self->log;
    c->task = self->task;
    c->root_class = self->root_class ? self->root_class : self;
    c->dir = dir;
    dir->conc = c;

    c->frozen_output_file_name = self->frozen_output_file_name;
    c->frozen_output_file_name_size = self->frozen_output_file_name_size;

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
        knd_log("-- conc name not found in %.*s :(", buf_size, buf);
        c->del(c);
        return knd_FAIL;
    }
    err = c->read(c, b, &chunk_size);
    if (err) {
        c->del(c);
        goto final;
    }
    */
    *result = rel;
    return knd_OK;

 final:
    close(fd);
    return err;
}

static int run_get_rel(void *obj,
                         struct kndTaskArg *args, size_t num_args)
{
    struct kndRel *self = obj;
    struct kndRel *rel;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;
    int err;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!memcmp(arg->name, "_impl", strlen("_impl"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }
    
    if (!name_size) return knd_FAIL;
    if (name_size >= KND_NAME_SIZE) return knd_LIMIT;

    self->curr_rel = NULL;
    err = get_rel(self, name, name_size, &rel);
    if (err) return err;

    self->curr_rel = rel;

    if (DEBUG_REL_LEVEL_TMP) {
        rel->str(rel);
    }
    return knd_OK;
}


static int parse_rel_arg_inst(void *obj,
                              const char *name, size_t name_size,
                              const char *rec,
                              size_t *total_size)
{
    struct kndRelInstance *self = obj;
    struct kndRel *rel = self->rel;
    struct kndRelArg *arg;
    int err;

    if (DEBUG_REL_LEVEL_TMP)
        knd_log(".. parsing the \"%.*s\" rel arg instance, rec:\"%.*s\"", name_size, name, 32, rec);

    /*    rel->mempool->new
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
        return err;
    }

    if (!self->tail_arg) {
        self->tail_arg = arg;
        self->args = arg;
    }
    else {
        self->tail_arg->next = arg;
        self->tail_arg = arg;
    }

    if (DEBUG_REL_LEVEL_2)
        arg->str(arg);
    */
    return knd_OK;
}

static int parse_import_instance(void *data,
                                 const char *rec,
                                 size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndRel *self = data;
    struct kndRel *rel;
    struct kndRelInstance *inst;
    struct kndRelInstEntry *entry;
    int err;

    if (DEBUG_REL_LEVEL_TMP) {
        knd_log(".. import \"%.*s\" rel instance..", 128, rec);
    }

    if (!self->curr_rel) {
        knd_log("-- curr rel not set :(");
        return knd_FAIL;
    }
    self->task->type = KND_CHANGE_STATE;

    err = self->mempool->new_rel_inst(self->mempool, &inst);
    if (err) return err;

    inst->phase = KND_SUBMITTED;
    inst->rel = self->curr_rel;

    struct kndTaskSpec specs[] = {
         { .type = KND_CHANGE_STATE,
           .name = "relarg",
           .name_size = strlen("relarg"),
           .buf = buf,
           .buf_size = &buf_size,
           .max_buf_size = KND_NAME_SIZE,
           .is_validator = true,
           .validate = parse_rel_arg_inst,
           .obj = inst
         }
   };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    if (DEBUG_REL_LEVEL_TMP)
        knd_log("++ %.*s rel inst parse OK!", inst->name_size, inst->name);

    /* send to the root rel inbox */
    inst->next = self->inst_inbox;
    self->inst_inbox = inst;
    self->inst_inbox_size++;

    rel = inst->rel;
    if (!rel->dir) {
        return knd_OK;
    }

    if (!rel->dir->inst_idx) {
        err = ooDict_new(&rel->dir->inst_idx, KND_MEDIUM_DICT_SIZE);
        if (err) return err;
    }

    entry = malloc(sizeof(struct kndRelInstEntry));
    if (!entry) return knd_NOMEM;
    memset(entry, 0, sizeof(struct kndRelInstEntry));
    entry->inst = inst;

    err = rel->dir->inst_idx->set(rel->dir->inst_idx,
                                  inst->name, inst->name_size, (void*)entry);
    if (err) return err;
        
    /*if (DEBUG_CONC_LEVEL_2) {
        knd_log("\n\nREGISTER INST in %.*s IDX:  [total:%zu valid:%zu]",
                c->name_size, c->name, c->dir->inst_idx->size, c->dir->num_insts);
        inst->depth = self->depth + 1;
        inst->str(inst);
    }
    */
   
    return knd_OK;
}

static int parse_rel_select(struct kndRel *self,
                            const char *rec,
                            size_t *total_size)
{
    int err = knd_FAIL, e;

    if (DEBUG_REL_LEVEL_TMP)
        knd_log(".. parsing Rel select: \"%.*s\"", 16, rec);

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = run_get_rel,
          .obj = self
        },
        { .type = KND_CHANGE_STATE,
          .name = "inst",
          .name_size = strlen("inst"),
          .parse = parse_import_instance,
          .obj = self
        }/*,
        { .name = "default",
          .name_size = strlen("default"),
          .is_default = true,
          .run = run_select_rel,
          .obj = self
          }*/
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) {
        knd_log("-- rel parse error: \"%.*s\"", self->log->buf_size, self->log->buf);
        if (!self->log->buf_size) {
            e = self->log->write(self->log, "rel parse failure",
                                 strlen("rel parse failure"));
            if (e) return e;
        }
        return err;
    }

    return knd_OK;
}

static int kndRel_resolve(struct kndRel *self)
{
    struct kndRelArg *arg;
    int err;

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. resolving REL: %.*s",
                self->name_size, self->name);

    for (arg = self->args; arg; arg = arg->next) {
        err = arg->resolve(arg);
        if (err) return err;
    }

    return knd_OK;
}

static int resolve_rels(struct kndRel *self)
{
    struct kndRel *rel;
    struct kndRelDir *dir;
    const char *key;
    void *val;
    int err;

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. resolving rels by \"%.*s\"",
                self->name_size, self->name);
    key = NULL;
    self->rel_idx->rewind(self->rel_idx);
    do {
        self->rel_idx->next_item(self->rel_idx, &key, &val);
        if (!key) break;

        dir = (struct kndRelDir*)val;
        rel = dir->rel;
        if (rel->is_resolved) continue;

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

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. rel coordination in progress ..");

    err = resolve_rels(self);
    if (err) return err;

    /* assign ids */
    key = NULL;
    self->rel_idx->rewind(self->rel_idx);
    do {
        self->rel_idx->next_item(self->rel_idx, &key, &val);
        if (!key) break;

        dir = (struct kndRelDir*)val;
        rel = dir->rel;

        /* assign id */
        err = knd_next_state(self->next_id);
        if (err) return err;
        
        memcpy(rel->id, self->next_id, KND_ID_SIZE);
        rel->phase = KND_CREATED;
    } while (key);

    /* display all reles */
    if (DEBUG_REL_LEVEL_TMP) {
        key = NULL;
        self->rel_idx->rewind(self->rel_idx);
        do {
            self->rel_idx->next_item(self->rel_idx, &key, &val);
            if (!key) break;
            dir = (struct kndRelDir*)val;
            rel = dir->rel;
            rel->depth = self->depth + 1;
            rel->str(rel);
        } while (key);
    }

    return knd_OK;
}



extern void 
kndRel_init(struct kndRel *self)
{
    self->del = del;
    self->str = str;
    self->export = export;
    self->resolve = kndRel_resolve;
    self->coordinate = kndRel_coordinate;
    //self->parse = parse_GSL;
    self->import = import_rel;
    self->select = parse_rel_select;
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
