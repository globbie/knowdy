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
#include "knd_attr_stm.h"
#include "knd_task.h"
#include "knd_dict.h"
#include "knd_shared_dict.h"
#include "knd_user.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#include <gsl-parser.h>

#define DEBUG_ATTR_STM_DECODE_LEVEL_1 0
#define DEBUG_ATTR_STM_DECODE_LEVEL_2 0
#define DEBUG_ATTR_STM_DECODE_LEVEL_3 0
#define DEBUG_ATTR_STM_DECODE_LEVEL_4 0
#define DEBUG_ATTR_STM_DECODE_LEVEL_5 0
#define DEBUG_ATTR_STM_DECODE_LEVEL_TMP 1

struct LocalContext {
    struct kndClassBasePred *class_var;
    struct kndClass    *class;
    struct kndClass    *inner_class;
    struct kndAttrStm  *list_parent;
    struct kndSet      *attr_idx;
    struct kndAttr     *attr;
    struct kndAttrStm  *attr_stm;
    struct kndRepo     *repo;
    struct kndTask     *task;
};

#if 0
static gsl_err_t read_nested_attr_stm(void *obj, const char *id, size_t id_size,
                                      const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrStm *self = ctx->attr_stm;
    struct kndTask    *task = ctx->task;
    struct kndAttrStm *attr_stm;
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndAttrRef *ref;
    struct kndAttr *attr;
    gsl_err_t parser_err;
    int err;

    assert(ctx->class != NULL);

    err = knd_set_get(ctx->class->attr_idx, id, id_size, (void**)&ref);
    if (err) {
        KND_TASK_LOG("class \"%.*s\" failed to decode attr id \"%.*s\"",
                     ctx->class->name_size, ctx->class->name, id_size, id);
        return *total_size = 0, make_gsl_err_external(err);
    }
    assert(ref->attr != NULL);
    attr = ref->attr;

    err = knd_attr_stm_new(mempool, &attr_stm);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_stm->parent = self;

    //    attr_stm->attr = attr;
    //attr_stm->name = attr->name;
    //attr_stm->name_size = attr->name_size;

    struct LocalContext attr_stm_ctx = {
        .attr_stm = attr_stm,
        .task = task
    };

    switch (attr->type) {
    case KND_ATTR_INNER:
        assert(attr->ref_class_entry != NULL);
        err = knd_class_acquire(attr->ref_class_entry, &attr_stm_ctx.class, task);
        if (err) {
            KND_TASK_LOG("failed to acquire class \"%.*s\"",
                         attr->ref_class_entry->name_size, attr->ref_class_entry->name);
            return *total_size = 0, make_gsl_err_external(err);
        }
        if (DEBUG_ATTR_STM_DECODE_LEVEL_2)
            knd_log(">> attr var inner class: \"%.*s\"",
                    attr->ref_class_entry->name_size, attr->ref_class_entry->name);
        break;
    default:
        break;
    }

    if (DEBUG_ATTR_STM_DECODE_LEVEL_2) {
        knd_log(".. read nested attr var: \"%.*s\" (parent item:%.*s)",
                attr_stm->name_size, attr_stm->name, self->name_size, self->name);
    }
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_stm_val_id,
          .obj = &attr_stm_ctx
        },
        { .name = "_t",
          .name_size = strlen("_t"),
          .parse = parse_text,
          .obj = &attr_stm_ctx
        },
        { .name = "_p",
          .name_size = strlen("_p"),
          .parse = parse_proc_ref,
          .obj = &attr_stm_ctx
        },
        { .validate = read_nested_attr_stm,
          .obj = &attr_stm_ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = read_nested_attr_stm_list,
          .obj = &attr_stm_ctx
        },
        { .is_default = true,
          .run = confirm_attr_stm,
          .obj = attr_stm
        }
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        KND_TASK_LOG("attr var reading failed: %d", parser_err.code);
        return parser_err;
    }

    if (DEBUG_ATTR_STM_DECODE_LEVEL_2)
        knd_log("++ attr var: \"%.*s\" val:%.*s (parent item: %.*s)",
                attr_stm->name_size, attr_stm->name, attr_stm->val_size, attr_stm->val,
                self->name_size, self->name);

    attr_stm->next = self->children;
    self->children = attr_stm;
    self->num_children++;
    return make_gsl_err(gsl_OK);
}





static gsl_err_t set_attr_stm_name(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrStm *self = ctx->attr_stm;
    struct kndTask    *task = ctx->task;
    struct kndClass *c = ctx->class;
    struct kndCharSeq *seq;
    struct kndAttr *attr = self->parent ? self->parent->attr : self->attr;
    int err;

    assert(attr != NULL);

    if (DEBUG_ATTR_STM_DECODE_LEVEL_2)
        knd_log(".. set \"%.*s\" (%d) attr var name \"%.*s\"",
                attr->name_size, attr->name, attr->type, name_size, name);

    if (!name_size) return make_gsl_err(gsl_FORMAT);

    if (c && c->implied_attr) {
        if (DEBUG_ATTR_STM_DECODE_LEVEL_2)
            knd_log(">> implied attr: %.*s (type:%d)",
                    c->implied_attr->name_size, c->implied_attr->name, c->implied_attr->type);

        self->implied_attr = c->implied_attr;
        attr = c->implied_attr;
    }

    switch (attr->type) {
    case KND_ATTR_REL:
        // fall through
    case KND_ATTR_REF:
        err = knd_get_class_entry_by_id(task->repo, name, name_size, &self->class_entry, task);
        if (err) {
            KND_TASK_LOG("no such class entry: %.*s", name_size, name);
            if (err) return make_gsl_err_external(err);
        }
        if (DEBUG_ATTR_STM_DECODE_LEVEL_3)
            knd_log("== REF: %.*s", self->class_entry->name_size, self->class_entry->name);
        break;
    case KND_ATTR_STR:
        err = knd_charseq_decode(name, name_size, &seq, task);
        if (err) {
            KND_TASK_LOG("failed to decode a charseq");
            if (err) return make_gsl_err_external(err);
        }
        self->name = seq->val;
        self->name_size = seq->val_size;
        break;
    default:
        self->name = name;
        self->name_size = name_size;
        break;
    }
    return make_gsl_err(gsl_OK);
}



static gsl_err_t set_attr_stm_val_id(void *obj, const char *val_id, size_t val_id_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrStm *self = ctx->attr_stm;
    struct kndTask    *task = ctx->task;
    struct kndRepo *repo = task->repo;
    struct kndClassEntry *entry;
    struct kndCharSeq *seq;
    struct kndClass *c = ctx->class;
    int err;

    if (DEBUG_ATTR_STM_DECODE_LEVEL_2) {
        knd_log(".. set \"%.*s\" (%d) attr var value: \"%.*s\" => \"%.*s\"",
                self->attr->name_size, self->attr->name, self->attr->type,
                self->name_size, self->name, val_size, val);
    }
    if (!val_size) return make_gsl_err(gsl_FORMAT);
    if (val_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);
    
    memcpy(self->val_id, val_id, val_id_size);
    self->val_id_size = val_id_size;

    switch (self->attr->type) {
    case KND_ATTR_NUM:
    case KND_ATTR_FLOAT:
    case KND_ATTR_STR:
        err = knd_charseq_decode(val, val_size, &seq, task);
        if (err) {
            KND_TASK_LOG("failed to decode a charseq");
            if (err) return make_gsl_err_external(err);
        }
        if (DEBUG_ATTR_STM_DECODE_LEVEL_2)
            knd_log(">> \"%.*s\" => decoded str val:%.*s",
                    self->name_size, self->name, seq->val_size, seq->val);
        self->val = seq->val;
        self->val_size = seq->val_size;
        break;
    case KND_ATTR_INNER:
        if (!c || !c->implied_attr) break;
        err = set_implied_attr_stm(c, val, val_size, self, task);
        if (err) {
            KND_TASK_LOG("failed to set implied attr \"%.*s\" to %.*s",
                         c->implied_attr->name_size, c->implied_attr->name, val_size, val);
            return make_gsl_err(gsl_FAIL);
        }
        break;
    case KND_ATTR_REL:
        // fall through
    case KND_ATTR_REF:
        err = knd_shared_set_get(task->idxs->class_idx, val, val_size, (void**)&entry);
        if (err) {
            KND_TASK_LOG("class \"%.*s\" not found in repo %.*s",
                         val_size, val, repo->name_size, repo->name);
            return make_gsl_err(gsl_FAIL);
        }
        self->class_entry = entry;
        
        if (DEBUG_ATTR_STM_DECODE_LEVEL_3)
            knd_log(">> set class ref: %.*s (id:%.*s)", entry->name_size, entry->name, val_size, val);
        break;
    default:
        break;
    }
    return make_gsl_err(gsl_OK);
}

static int build_attr_stm(struct kndClassBasePred *self, const char *id, size_t id_size,
                          struct kndAttrStm **result, struct kndTask *task)
{
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndClassEntry *entry = self->entry;
    struct kndClass *c = self->parent;
    struct kndAttr *attr;
    struct kndAttrStm *var;
    struct kndAttrRef *ref;
    int err;

    err = knd_set_get(c->attr_idx, id, id_size, (void**)&ref);
    KND_TASK_ERR("no attr \"%.*s\" in class \"%.*s\"", id_size, id, c->name_size, c->name);
    attr = ref->attr;

    if (DEBUG_ATTR_STM_DECODE_LEVEL_2)
        knd_log(">> class \"%.*s\" to read \"%.*s\" var (origin: %.*s) (id:%.*s, type: %s)",
                self->parent->name_size, self->parent->name,
                attr->name_size, attr->name, entry->name_size, entry->name,
                id_size, id, knd_attr_names[attr->type]);

    err = knd_attr_stm_new(mempool, &var);
    KND_TASK_ERR("failed to alloc an attr var");
    var->class_var = self;
    var->name = attr->name;
    var->name_size = attr->name_size;
    var->attr = attr;
    // set inherited attr var
    ref->attr_stm = var;
    append_attr_stm(self, var);

    switch (attr->type) {
    case KND_ATTR_INNER:
        assert(attr->ref_class_entry != NULL);
        err = knd_class_acquire(attr->ref_class_entry, &attr->ref_class, task);
        KND_TASK_ERR("failed to acquire class \"%.*s\"",
                     attr->ref_class_entry->name_size, attr->ref_class_entry->name);
        if (DEBUG_ATTR_STM_DECODE_LEVEL_2)
            knd_log(">> inner class: \"%.*s\"",
                    attr->ref_class_entry->name_size, attr->ref_class_entry->name);
        break;
    default:
        break;
    }
    *result = var;
    return knd_OK;
}
#endif

int knd_decode_attr_stms(struct kndClass *base, struct kndAttrStm *attr_stms, struct kndTask *task)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    struct kndAttrStm *stm;
    struct kndAttrRef *ref;
    struct kndAttr *attr;
    struct kndProc *proc;
    int err;

    if (DEBUG_ATTR_STM_DECODE_LEVEL_TMP) {
        knd_log(".. decoding attr stms of {baseclass %.*s}",
                base->name_size, base->name);
    }

    FOREACH (stm, attr_stms) {
        err = knd_shared_set_get(task->idxs->attr_idx, stm->id, stm->id_size, (void**)&ref);
        KND_TASK_ERR("failed to get attr ref {attr %.*s}", stm->id_size, stm->id);
        stm->name = ref->name;
        stm->name_size = ref->name_size;

        knd_log(".. {base %.*s {attr-stm %.*s {id %.*s} {val %.*s}}}",
                base->name_size, base->name,
                stm->name_size, stm->name, stm->id_size, stm->id,
                stm->val_id_size, stm->val_id);

        if (!ref->attr) {
            knd_log(".. failed to decode {ref %.*s} {base %.*s {attr-stm %.*s {id %.*s}}}",
                    ref->name_size, ref->name,
                    base->name_size, base->name,
                    stm->name_size, stm->name, stm->id_size, stm->id);
        }

        assert (ref->attr != NULL);

        stm->attr = ref->attr;
    }

    return knd_OK;
}
