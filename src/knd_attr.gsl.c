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
                                    struct kndTask *task,
                                    size_t depth);

static int inner_item_export_GSL(struct kndAttrVar *parent_item,
                                 struct kndTask *task,
                                 size_t depth)
{
    struct glbOutput *out = task->out;
    struct kndAttrVar *item;
    struct kndAttr *attr;
    struct kndClass *c;
    int err;

    c = parent_item->attr->parent_class;

    if (DEBUG_ATTR_GSL_LEVEL_2) {
        knd_log(".. GSL export inner item: %.*s",
                parent_item->name_size, parent_item->name);
    }

    if (parent_item->name_size) {
        //err = out->write(out, "{_id", strlen("{_id")); RET_ERR();
        //err = out->writec(out, ' '); RET_ERR();

        //err = out->write(out, parent_item->name, parent_item->name_size);          RET_ERR();

        //err = out->writec(out, '}'); RET_ERR();
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

        if (task->format_offset) {
            err = out->writec(out, '\n');                                             RET_ERR();
            err = knd_print_offset(out, (depth + 1) * task->format_offset);           RET_ERR();
        }

        err = out->writec(out, '{');
        if (err) return err;
        err = out->write(out, attr->name, attr->name_size); RET_ERR();

        //c->depth = 1;
        //c->max_depth = 1;

        err = knd_class_export_GSL(c, task, depth + 1);
        if (err) return err;
    }

    if (!parent_item->class) {
        /* terminal string value */
        if (parent_item->val_size) {
            c = parent_item->attr->ref_class;
            attr = parent_item->attr;

            if (c->implied_attr) {
                attr = c->implied_attr;
                err = out->writec(out, '{'); RET_ERR();
                err = out->write(out, attr->name, attr->name_size); RET_ERR();
            }
            err = out->writec(out, ' '); RET_ERR();

            err = out->write(out, parent_item->val, parent_item->val_size); RET_ERR();
        }
    }

    for (item = parent_item->children; item; item = item->next) {
        if (task->format_offset) {
            err = out->writec(out, '\n');                                         RET_ERR();
            err = knd_print_offset(out, (depth + 1) * task->format_offset);       RET_ERR();
        }

        err = out->writec(out, '{');                                              RET_ERR();
        err = out->write(out, item->name, item->name_size);                       RET_ERR();

        switch (item->attr->type) {
        case KND_ATTR_REF:
            assert(item->class != NULL);
            c = item->class;
            //c->depth = 1;
            //c->max_depth = 1;
            err = knd_class_export_GSL(c, task, depth + 1);                       RET_ERR();
            break;
        case KND_ATTR_INNER:
            err = inner_item_export_GSL(item, task, depth + 1);
            if (err) return err;
            break;
        default:
            err = out->writec(out, ' ');                                          RET_ERR();
            err = out->write(out, item->val, item->val_size);                     RET_ERR();
            break;
        }
        err = out->writec(out, '}');                                              RET_ERR();
    }

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
    struct kndMemPool *mempool = task->mempool;
    size_t numval = 0;
    size_t depth = 1;
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

    attr_var->depth = task->depth;
    attr_var->max_depth = task->max_depth;

    if (attr->is_a_set) {
        return attr_var_list_export_GSL(attr_var, task, depth);
    }

    err = out->writec(out, '{');                                          RET_ERR();
    err = out->write(out, attr_var->name, attr_var->name_size);           RET_ERR();

    switch (attr->type) {
    case KND_ATTR_NUM:
        err = out->writec(out, ' ');                            RET_ERR();
        err = out->write(out, attr_var->val, attr_var->val_size);             RET_ERR();
        break;
    case KND_ATTR_INNER:
        err = inner_item_export_GSL(attr_var, task, depth + 1);
        if (err) return err;
        break;
    case KND_ATTR_STR:
        err = out->writec(out, ' ');                            RET_ERR();
        err = out->write(out, attr_var->val, attr_var->val_size);             RET_ERR();
        break;
    default:
        break;
    }
    err = out->writec(out, '}');                                          RET_ERR();
    
    return knd_OK;
}

static int ref_item_export_GSL(struct kndAttrVar *item,
                               struct kndTask *task,
                               size_t depth)
{
    struct kndClass *c;
    size_t curr_depth = task->depth;
    int err;

    // TODO
    assert(item->class != NULL);
    c = item->class;

    knd_log(".. expand ref %.*s: depth:%zu max_depth:%zu",
            c->name_size, c->name, task->depth, task->max_depth);

    err = knd_class_export_GSL(c, task, depth);                               RET_ERR();

    task->depth = curr_depth;

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
    err = knd_proc_export(proc, KND_FORMAT_GSL, out);  RET_ERR();
    return knd_OK;
}

static int attr_var_list_export_GSL(struct kndAttrVar *parent_item,
                                    struct kndTask *task,
                                    size_t depth)
{
    struct glbOutput *out = task->out;
    struct kndAttrVar *item;
    size_t count = 0;
    int err;

    if (DEBUG_ATTR_GSL_LEVEL_2) {
        knd_log(".. export GSL list: %.*s\n\n",
                parent_item->name_size, parent_item->name);
    }

    err = out->writec(out, '[');  RET_ERR();
    err = out->write(out, parent_item->name, parent_item->name_size);  RET_ERR();

    /* first elem: TODO */
    /*if (parent_item->class) {
        switch (parent_item->attr->type) {
        case KND_ATTR_INNER:
            parent_item->id_size = sprintf(parent_item->id, "%lu",
                                           (unsigned long)count);
            count++;

            err = inner_item_export_GSL(parent_item, task, depth + 1);       RET_ERR();
            break;
        case KND_ATTR_REF:
            err = ref_item_export_GSL(parent_item, task);                    RET_ERR();
            break;
        case KND_ATTR_PROC:
            if (parent_item->proc) {
                err = proc_item_export_GSL(parent_item, task);   RET_ERR();
            }
            break;
        default:
            err = out->writec(out, ' ');  RET_ERR();
            err = out->write(out, parent_item->val, parent_item->val_size);  RET_ERR();
            break;
        }
    }
    */

    for (item = parent_item->list; item; item = item->next) {
        /* TODO */
        if (!item->attr) {
            knd_log("-- no attr: %.*s (%p)",
                    item->name_size, item->name, item);
            continue;
        }

        if (task->format_offset) {
            err = out->writec(out, '\n');                                         RET_ERR();
            err = knd_print_offset(out, (depth + 1) * task->format_offset);       RET_ERR();
        }

        err = out->writec(out, '{');  RET_ERR();

        switch (parent_item->attr->type) {
        case KND_ATTR_INNER:
            item->id_size = sprintf(item->id, "%lu",
                                    (unsigned long)count);
            count++;
            err = inner_item_export_GSL(item, task, depth + 1);
            if (err) return err;
            break;
        case KND_ATTR_REF:
            err = ref_item_export_GSL(item, task, depth + 1);
            if (err) return err;
            break;
        case KND_ATTR_PROC:
            if (item->proc) {
                err = proc_item_export_GSL(item, task);
                if (err) return err;
            }
            break;
        default:
            err = out->writec(out, ' ');  RET_ERR();
            err = out->write(out, item->val, item->val_size);  RET_ERR();
            break;
        }
        err = out->writec(out, '}');  RET_ERR();
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
    //bool in_list = false;
    int err;

    for (item = items; item; item = item->next) {
        if (!item->attr) continue;
        attr = item->attr;

        if (is_concise && !attr->concise_level) continue;

        item->depth = items->depth;
        item->max_depth = items->max_depth;

        if (task->format_offset) {
            err = out->writec(out, '\n');                                     RET_ERR();
            err = knd_print_offset(out, (depth + 1) * task->format_offset);   RET_ERR();
        }

        if (attr->is_a_set) {
            err = attr_var_list_export_GSL(item, task, depth + 1);
            if (err) return err;
            continue;
        }

        err = out->writec(out, '{');                                              RET_ERR();
        err = out->write(out, item->name, item->name_size);                       RET_ERR();

        switch (item->attr->type) {
        case KND_ATTR_NUM:
            err = out->writec(out, ' ');  RET_ERR();
            err = out->write(out, item->val, item->val_size); RET_ERR();
            break;
        case KND_ATTR_TEXT:
            err = out->writec(out, ' ');                                          RET_ERR();
            err = knd_text_export(item->text, KND_FORMAT_GSL, task);
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
                err = inner_item_export_GSL(item, task, depth + 1);
                if (err) return err;
            } else {
                c = item->class;
                //c->depth = 1;
                //c->max_depth = 1;
                err = knd_class_export_GSL(c, task, depth + 1);
                if (err) return err;
            }
            break;
        default:
            err = out->writec(out, ' ');                                          RET_ERR();
            err = out->write(out, item->val, item->val_size);                     RET_ERR();
            break;
        }
        err = out->writec(out, '}');                                              RET_ERR();
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

    err = out->writec(out, '{');
    if (err) return err;
    err = out->write(out, item->name, item->name_size);
    if (err) return err;
    err = out->writec(out, ' ');
    if (err) return err;
    
    switch (item->attr->type) {
    case KND_ATTR_NUM:
        err = out->write(out, item->val, item->val_size); RET_ERR();
        break;
    case KND_ATTR_PROC:
        if (item->proc) {
            err = proc_item_export_GSL(item, task);  RET_ERR();
        } else {
            err = out->write(out, item->val, item->val_size);  RET_ERR();
        }
        break;
    case KND_ATTR_INNER:
        if (!item->class) {
            err = inner_item_export_GSL(item, task, depth + 1);  RET_ERR();
        } else {
            c = item->class;
            err = knd_class_export_GSL(c, task, depth + 1);  RET_ERR();
        }
        break;
    default:
        err = out->write(out, item->val, item->val_size);  RET_ERR();
        break;
    }
    return knd_OK;
}


extern int knd_attr_export_GSL(struct kndAttr *self, struct kndTask *task, size_t depth)
{
    char buf[KND_NAME_SIZE] = {0};
    size_t buf_size = 0;
    struct kndTranslation *tr;
    struct glbOutput *out = task->out;
    const char *type_name = knd_attr_names[self->type];
    size_t type_name_size = strlen(knd_attr_names[self->type]);
    int err;

    err = out->write(out, "{", 1);  RET_ERR();
    err = out->write(out, type_name, type_name_size);
    if (err) return err;
    err = out->write(out, " ", 1);
    if (err) return err;
    err = out->write(out, self->name, self->name_size);
    if (err) return err;

    if (self->is_a_set) {
        err = out->write(out, " {t set}", strlen(" {t set}"));
        if (err) return err;
    }

    if (self->is_implied) {
        err = out->write(out, " {impl}", strlen(" {impl}"));
        if (err) return err;
    }

    if (self->is_indexed) {
        err = out->write(out, " {idx}", strlen(" {idx}"));
        if (err) return err;
    }

    if (self->concise_level) {
        buf_size = sprintf(buf, "%zu", self->concise_level);
        err = out->write(out, " {concise ", strlen(" {concise "));
        if (err) return err;
        err = out->write(out, buf, buf_size);
        if (err) return err;
        err = out->writec(out, '}');
        if (err) return err;
    }

    if (self->ref_classname_size) {
        err = out->write(out, " {c ", strlen(" {c "));
        if (err) return err;
        err = out->write(out, self->ref_classname, self->ref_classname_size);
        if (err) return err;
        err = out->write(out, "}", 1);
        if (err) return err;
    }

    if (self->ref_procname_size) {
        err = out->write(out, " {p ", strlen(" {p "));
        if (err) return err;
        err = out->write(out, self->ref_procname, self->ref_procname_size);
        if (err) return err;
        err = out->write(out, "}", 1);
        if (err) return err;
    }

    /* choose gloss */
    if (self->tr) {
        if (task->format_offset) {
            err = out->writec(out, '\n');                                             RET_ERR();
            err = knd_print_offset(out, (depth + 1) * task->format_offset);           RET_ERR();
        }
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

