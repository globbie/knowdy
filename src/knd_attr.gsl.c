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
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_state.h"
#include "knd_user.h"
#include "knd_repo.h"
#include "knd_mempool.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_http_codes.h"

#include <glb-lib/output.h>

#define DEBUG_ATTR_GSL_LEVEL_1 0
#define DEBUG_ATTR_GSL_LEVEL_2 0
#define DEBUG_ATTR_GSL_LEVEL_3 0
#define DEBUG_ATTR_GSL_LEVEL_4 0
#define DEBUG_ATTR_GSL_LEVEL_5 0
#define DEBUG_ATTR_GSL_LEVEL_TMP 1

static int attr_var_list_export_GSL(struct kndAttrVar *parent_item,
                                    struct kndTask *task);

static int inner_item_export_GSL(struct kndAttrVar *parent_item,
                                  struct kndTask *task)
{
    struct glbOutput *out = task->out;
    struct kndAttrVar *item;
    struct kndAttr *attr;
    struct kndClass *c;
    bool in_list = false;
    int err;

    c = parent_item->attr->parent_class;

    if (DEBUG_ATTR_GSL_LEVEL_2) {
        knd_log(".. GSL export inner item: %.*s",
                parent_item->name_size, parent_item->name);
    }

    err = out->writec(out, '{');
    if (err) return err;

    if (parent_item->id_size) {
        err = out->write(out, "\"_id\":", strlen("\"_id\":"));
        if (err) return err;
        err = out->writec(out, '"');
        if (err) return err;
        err = out->write(out, parent_item->id, parent_item->id_size);
        if (err) return err;
        err = out->writec(out, '"');
        if (err) return err;
        in_list = true;

        c = parent_item->attr->ref_class;

        /*if (c->num_computed_attrs) {
            if (DEBUG_ATTR_GSL_LEVEL_2)
                knd_log(".. present computed attrs in %.*s (val:%.*s)",
                        parent_item->name_size, parent_item->name,
                        parent_item->val_size, parent_item->val);

            err = knd_present_computed_inner_attrs(parent_item, task);
            if (err) return err;
            }*/
    }

    /* export a class ref */
    if (parent_item->class) {
        attr = parent_item->attr;
        c = parent_item->attr->ref_class;

        /* TODO: check assignment */
        if (parent_item->implied_attr) {
            attr = parent_item->implied_attr;
        }

        if (c->implied_attr)
            attr = c->implied_attr;

        c = parent_item->class;

        if (in_list) {
            err = out->writec(out, ',');
            if (err) return err;
        }

        err = out->writec(out, '"');
        if (err) return err;
        err = out->write(out, attr->name, attr->name_size);
        if (err) return err;
        err = out->writec(out, '"');
        if (err) return err;
        err = out->writec(out, ':');
        if (err) return err;

        c->depth = 1;
        c->max_depth = 1;

        err = knd_class_export(c, KND_FORMAT_GSL, task);
        if (err) return err;
        in_list = true;
    }

    if (!parent_item->class) {
        /* terminal string value */
        if (parent_item->val_size) {
            c = parent_item->attr->ref_class;
            attr = parent_item->attr;

            if (c->implied_attr) {
                attr = c->implied_attr;
                err = out->writec(out, '"');
                if (err) return err;
                err = out->write(out, attr->name, attr->name_size);
                if (err) return err;
                err = out->writec(out, '"');
                if (err) return err;
                err = out->writec(out, ':');
                if (err) return err;
            } else {
                err = out->write(out, "\"_val\":", strlen("\"_val\":"));
                if (err) return err;
            }

            /* string or numeric value? */
            switch (attr->type) {
            case KND_ATTR_NUM:
                err = out->write(out, parent_item->val, parent_item->val_size);
                if (err) return err;
                break;
            default:
                err = out->writec(out, '"');
                if (err) return err;
                err = out->write(out, parent_item->val, parent_item->val_size);
                if (err) return err;
                err = out->writec(out, '"');
                if (err) return err;
            }
            in_list = true;
        }
    }

    for (item = parent_item->children; item; item = item->next) {
        if (in_list) {
            err = out->writec(out, ',');
            if (err) return err;
        }

        err = out->writec(out, '"');
        if (err) return err;
        err = out->write(out, item->name, item->name_size);
        if (err) return err;
        err = out->writec(out, '"');
        if (err) return err;
        err = out->writec(out, ':');
        if (err) return err;

        switch (item->attr->type) {
        case KND_ATTR_REF:
            assert(item->class != NULL);
            c = item->class;
            c->depth = 1;
            c->max_depth = 1;
            err = knd_class_export(c, KND_FORMAT_GSL, task);
            if (err) return err;
            break;
        case KND_ATTR_INNER:
            err = inner_item_export_GSL(item, task);
            if (err) return err;
            break;
        default:
            err = out->writec(out, '"');
            if (err) return err;
            err = out->write(out, item->val, item->val_size);
            if (err) return err;
            err = out->writec(out, '"');
            if (err) return err;
            break;
        }

        in_list = true;
    }

    err = out->writec(out, '}');
    if (err) return err;

    return knd_OK;
}

extern int knd_export_inherited_attr_GSL(void *obj,
                                         const char *unused_var(elem_id),
                                         size_t unused_var(elem_id_size),
                                         size_t unused_var(count),
                                         void *elem)
{
    struct kndTask *task = obj;
    struct kndClass   *self = task->class;
    struct kndAttrRef *ref = elem;
    struct kndAttr *attr = ref->attr;
    struct kndAttrVar *attr_var = ref->attr_var;
    struct kndRepo *repo = self->entry->repo;
    struct glbOutput *out = repo->out;
    struct kndMemPool *mempool = repo->mempool;
    size_t numval = 0;
    int err;

    if (DEBUG_ATTR_GSL_LEVEL_2) {
        knd_log(".. class \"%.*s\" to export inherited attr \"%.*s\"..",
                self->name_size, self->name,
                attr->name_size, attr->name);
    }

    /* skip over immediate attrs */
    if (attr->parent_class == self) return knd_OK;

    if (attr_var && attr_var->class_var) {
        /* already exported by parent */
        if (attr_var->class_var->parent == self) return knd_OK;
    }

    /* NB: display only concise fields */
    if (!attr->concise_level) {
        return knd_OK;
    }

    if (attr->proc) {
        if (DEBUG_ATTR_GSL_LEVEL_2)
            knd_log("..computed attr: %.*s!", attr->name_size, attr->name);

        if (!attr_var) {
            err = knd_attr_var_new(mempool, &attr_var);                           RET_ERR();
            attr_var->attr = attr;
            attr_var->name = attr->name;
            attr_var->name_size = attr->name_size;
            ref->attr_var = attr_var;
        }

        switch (attr->type) {
        case KND_ATTR_NUM:
            numval = attr_var->numval;

            if (!attr_var->is_cached) {
                err = knd_compute_class_attr_num_value(self, attr_var);
                if (err) return err;
                numval = attr_var->numval;
                attr_var->numval = numval;
                attr_var->is_cached = true;
            }

            err = out->writec(out, ',');
            if (err) return err;
            err = out->writec(out, '"');
            if (err) return err;
            err = out->write(out, attr->name, attr->name_size);
            if (err) return err;
            err = out->writec(out, '"');
            if (err) return err;
            err = out->writec(out, ':');
            if (err) return err;
            
            err = out->writef(out, "%zu", numval);                                RET_ERR();
            break;
        default:
            break;
        }
        return knd_OK;
    }

    if (!attr_var) {
        // TODO
        return knd_OK;
        //err = knd_get_attr_var(self, attr->name, attr->name_size, &attr_var);
        //if (err) return knd_OK;
    }

    attr_var->depth = self->depth;
    attr_var->max_depth = self->max_depth;

    err = out->writec(out, ',');                                          RET_ERR();

    if (attr->is_a_set) {
        return attr_var_list_export_GSL(attr_var, task);
    }

    err = out->writec(out, '"');                                          RET_ERR();
    err = out->write(out, attr_var->name, attr_var->name_size);           RET_ERR();
    err = out->write(out, "\":", strlen("\":"));                          RET_ERR();

    switch (attr->type) {
    case KND_ATTR_NUM:
        err = out->write(out, attr_var->val, attr_var->val_size);             RET_ERR();
        break;
    case KND_ATTR_INNER:
        err = inner_item_export_GSL(attr_var, task);
        if (err) return err;
        break;
    case KND_ATTR_STR:
        err = out->write(out, "\"", strlen("\""));                            RET_ERR();
        err = out->write(out, attr_var->val, attr_var->val_size);             RET_ERR();
        err = out->write(out, "\"", strlen("\""));                            RET_ERR();
        break;
    default:
        err = out->write(out, "{}", strlen("{}"));                            RET_ERR();
        break;
    }
    
    return knd_OK;
}

static int ref_item_export_GSL(struct kndAttrVar *item,
                                struct kndTask *task)
{
    struct kndClass *c;
    int err;

    // TODO
    assert(item->class != NULL);

    c = item->class;
    c->depth = item->depth;
    c->max_depth = item->max_depth;

    err = knd_class_export(c, KND_FORMAT_GSL, task);                 RET_ERR();
    return knd_OK;
}

static int proc_item_export_GSL(struct kndAttrVar *item,
                                 struct kndTask *task)
{
    struct glbOutput *out = task->out;
    struct kndProc *proc;
    int err;

    assert(item->proc != NULL);
    proc = item->proc;

    err = knd_proc_export(proc, KND_FORMAT_GSL, out);
    if (err) return err;

    return knd_OK;
}

static int attr_var_list_export_GSL(struct kndAttrVar *parent_item,
                                     struct kndTask *task)
{
    struct glbOutput *out = task->out;
    struct kndAttrVar *item;
    bool in_list = false;
    size_t count = 0;
    int err;

    if (DEBUG_ATTR_GSL_LEVEL_2) {
        knd_log(".. export GSL list: %.*s\n\n",
                parent_item->name_size, parent_item->name);
    }

    err = out->writec(out, '"');
    if (err) return err;
    err = out->write(out, parent_item->name, parent_item->name_size);
    if (err) return err;
    err = out->write(out, "\":[", strlen("\":["));
    if (err) return err;

    /* first elem: TODO */
    if (parent_item->class) {
        switch (parent_item->attr->type) {
        case KND_ATTR_INNER:
            parent_item->id_size = sprintf(parent_item->id, "%lu",
                                           (unsigned long)count);
            count++;

            err = inner_item_export_GSL(parent_item, task);
            if (err) return err;
            break;
        case KND_ATTR_REF:
            err = ref_item_export_GSL(parent_item, task);
            if (err) return err;
            break;
        case KND_ATTR_PROC:
            if (parent_item->proc) {
                err = proc_item_export_GSL(parent_item, task);
                if (err) return err;
            }
            break;
        default:
            err = out->writec(out, '"');
            if (err) return err;
            err = out->write(out, parent_item->val, parent_item->val_size);
            if (err) return err;
            err = out->writec(out, '"');
            if (err) return err;
            break;
        }
        in_list = true;
    }

    for (item = parent_item->list; item; item = item->next) {
        /* TODO */
        if (!item->attr) {
            knd_log("-- no attr: %.*s (%p)",
                    item->name_size, item->name, item);
            continue;
        }

        if (in_list) {
            err = out->writec(out, ',');
            if (err) return err;
        }

        switch (parent_item->attr->type) {
        case KND_ATTR_INNER:
            item->id_size = sprintf(item->id, "%lu",
                                    (unsigned long)count);
            count++;

            err = inner_item_export_GSL(item, task);
            if (err) return err;
            break;
        case KND_ATTR_REF:
            err = ref_item_export_GSL(item, task);
            if (err) return err;
            break;
        case KND_ATTR_PROC:
            if (item->proc) {
                err = proc_item_export_GSL(item, task);
                if (err) return err;
            }
            break;
        default:
            err = out->writec(out, '"');
            if (err) return err;
            err = out->write(out, item->val, item->val_size);
            if (err) return err;
            err = out->writec(out, '"');
            if (err) return err;
            break;
        }
        in_list = true;
    }
    err = out->writec(out, ']');
    if (err) return err;

    return knd_OK;
}

extern int knd_attr_vars_export_GSL(struct kndAttrVar *items,
                                    struct kndTask *task,
                                    bool is_concise,
                                    size_t depth)
{
    struct glbOutput *out = task->out;
    struct kndAttrVar *item;
    struct kndAttr *attr;
    struct kndClass *c;
    int err;

    for (item = items; item; item = item->next) {
        if (!item->attr) continue;
        attr = item->attr;

        if (is_concise && !attr->concise_level) continue;

        item->depth = items->depth;
        item->max_depth = items->max_depth;

        err = out->writec(out, ',');
        if (err) return err;

        if (attr->is_a_set) {
            err = attr_var_list_export_GSL(item, task);
            if (err) return err;
            continue;
        }

        err = out->writec(out, '"');
        if (err) return err;
        err = out->write(out, item->name, item->name_size);
        if (err) return err;
        err = out->write(out, "\":", strlen("\":"));
        if (err) return err;

        switch (item->attr->type) {
        case KND_ATTR_NUM:
            err = out->write(out, item->val, item->val_size);
            if (err) return err;
            break;
        case KND_ATTR_TEXT:
            err = out->writec(out, '"');                                          RET_ERR();
            err = knd_text_export(item->text, KND_FORMAT_GSL, task);
            err = out->writec(out, '"');                                          RET_ERR();
            if (err) return err;
            break;
        case KND_ATTR_PROC:
            if (item->proc) {
                err = proc_item_export_GSL(item, task);
                if (err) return err;
            } else {
                err = out->write(out, "\"", strlen("\""));
                if (err) return err;
                err = out->write(out, item->val, item->val_size);
                if (err) return err;
                err = out->write(out, "\"", strlen("\""));
                if (err) return err;
            }
            break;
        case KND_ATTR_INNER:
            if (!item->class) {
                err = inner_item_export_GSL(item, task);
                if (err) return err;
            } else {
                c = item->class;
                c->depth = 1;
                c->max_depth = 1;
                err = knd_class_export_GSL(c, task, depth + 1);
                if (err) return err;
            }
            break;
        default:
            err = out->write(out, "\"", strlen("\""));
            if (err) return err;
            err = out->write(out, item->val, item->val_size);
            if (err) return err;
            err = out->write(out, "\"", strlen("\""));
            if (err) return err;
        }
    }

    return knd_OK;
}

extern int knd_attr_var_export_GSL(struct kndAttrVar *item,
                                   struct kndTask *task,
                                   size_t depth)
{
    struct glbOutput *out = task->out;
    struct kndClass *c;
    int err;

    if (item->depth >= item->max_depth) return knd_OK;

    if (task->format_offset) {
        err = out->writec(out, '\n');                                             RET_ERR();
        err = knd_print_offset(out, (depth + 1) * task->format_offset);           RET_ERR();
    }

    err = out->writec(out, '"');
    if (err) return err;
    err = out->write(out, item->name, item->name_size);
    if (err) return err;
    err = out->write(out, "\":", strlen("\":"));
    if (err) return err;
    
    switch (item->attr->type) {
    case KND_ATTR_NUM:

        err = out->write(out, item->val, item->val_size);
        if (err) return err;
        
        break;
    case KND_ATTR_PROC:
        if (item->proc) {
            err = proc_item_export_GSL(item, task);
            if (err) return err;
        } else {
            err = out->write(out, "\"", strlen("\""));
            if (err) return err;
            err = out->write(out, item->val, item->val_size);
            if (err) return err;
            err = out->write(out, "\"", strlen("\""));
            if (err) return err;
        }
        break;
    case KND_ATTR_INNER:
        if (!item->class) {
            err = inner_item_export_GSL(item, task);
            if (err) return err;
        } else {
            c = item->class;
            err = knd_class_export_GSL(c, task, 0);
            if (err) return err;
        }
        break;
    default:
            err = out->write(out, "\"", strlen("\""));
            if (err) return err;
            err = out->write(out, item->val, item->val_size);
            if (err) return err;
            err = out->write(out, "\"", strlen("\""));
            if (err) return err;
    }
    return knd_OK;
}
