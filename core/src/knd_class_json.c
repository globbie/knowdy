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

#define DEBUG_JSON_LEVEL_1 0
#define DEBUG_JSON_LEVEL_2 0
#define DEBUG_JSON_LEVEL_3 0
#define DEBUG_JSON_LEVEL_4 0
#define DEBUG_JSON_LEVEL_5 0
#define DEBUG_JSON_LEVEL_TMP 1

static int export_class_state_JSON(struct kndClass *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    struct glbOutput *out = self->entry->repo->out;
    struct kndUpdate *update;
    struct tm tm_info;
    size_t latest_state = self->init_state + self->num_states;
    int err;

    update = self->states->update;
    err = out->write(out, "\"_state\":", strlen("\"_state\":"));                  RET_ERR();
    err = out->writef(out, "%zu", latest_state);                                  RET_ERR();
    time(&update->timestamp);
    localtime_r(&update->timestamp, &tm_info);
    buf_size = strftime(buf, KND_NAME_SIZE,
                        ",\"_modif\":\"%Y-%m-%d %H:%M:%S\"", &tm_info);
    err = out->write(out, buf, buf_size);                                         RET_ERR();

    return knd_OK;
}

static int export_class_inst_state_JSON(struct kndClass *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    struct glbOutput *out = self->entry->repo->out;
    struct kndUpdate *update;
    struct tm tm_info;
    size_t latest_state = self->init_inst_state + self->num_inst_states;
    int err;

    /* latest inst update */
    update = self->inst_states->update;
    err = out->write(out, "\"_state\":", strlen("\"_state\":"));                  RET_ERR();
    err = out->writef(out, "%zu", latest_state);                                  RET_ERR();

    time(&update->timestamp);
    localtime_r(&update->timestamp, &tm_info);
    buf_size = strftime(buf, KND_NAME_SIZE,
                        ",\"_modif\":\"%Y-%m-%d %H:%M:%S\"", &tm_info);
    err = out->write(out, buf, buf_size);                                     RET_ERR();
    return knd_OK;
}


static int export_conc_elem_JSON(void *obj,
                                 const char *elem_id,
                                 size_t elem_id_size,
                                 size_t count,
                                 void *elem)
{
    struct kndClass *self = obj;
    struct kndTask *task = self->entry->repo->task;
    if (count < task->start_from) return knd_OK;
    if (task->batch_size >= task->batch_max) return knd_RANGE;

    struct glbOutput *out = self->entry->repo->out;
    struct kndClassEntry *entry = elem;
    struct kndClass *c = entry->class;
    int err;

    if (DEBUG_JSON_LEVEL_2)
        knd_log("..export elem: %.*s  conc:%p entry:%p",
                elem_id_size, elem_id, c, entry);

    if (!c) {
        //err = unfreeze_class(self, entry, &c);                                      RET_ERR();
    }

    /* separator */
    if (task->batch_size) {
        err = out->writec(out, ',');                                              RET_ERR();
    }

    c->depth = 0;
    c->max_depth = 0;
    if (self->max_depth) {
        c->max_depth = self->max_depth;
    }

    err = c->export(c);
    if (err) return err;

    task->batch_size++;

    return knd_OK;
}


static int aggr_item_export_JSON(struct kndClass *self,
                                 struct kndAttrVar *parent_item)
{
    struct glbOutput *out = self->entry->repo->out;
    struct kndAttrVar *item;
    struct kndAttr *attr;
    struct kndClass *c;
    bool in_list = false;
    int err;

    c = parent_item->attr->parent_class;

    if (DEBUG_JSON_LEVEL_2) {
        knd_log(".. JSON export aggr item: %.*s",
                parent_item->name_size, parent_item->name);
        //c = parent_item->attr->parent_class;
        //c->str(c);
        c->str(c);
        knd_log("== comp attrs:%zu",
                c->num_computed_attrs);
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

        c = parent_item->attr->conc;
        if (c->num_computed_attrs) {

            if (DEBUG_JSON_LEVEL_2)
                knd_log("\n..present computed attrs in %.*s (val:%.*s)",
                        parent_item->name_size, parent_item->name,
                        parent_item->val_size, parent_item->val);

            // TODO
            //err = present_computed_aggr_attrs(self, parent_item);
            //if (err) return err;
        }
   }
    
    /* export a class ref */
    if (parent_item->class) {
        attr = parent_item->attr;
        c = parent_item->attr->conc;

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

        c->depth = self->depth + 1;
        c->max_depth = self->max_depth;

        //knd_log(".. export %.*s class, depth:%zu max depth:%zu\n\n",
        //        c->name_size, c->name, c->depth, c->max_depth);

        err = c->export(c);
        if (err) return err;
        in_list = true;
    }

    if (!parent_item->class) {
        /* terminal string value */
        if (parent_item->val_size) {
            c = parent_item->attr->conc;
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
            c = item->class;
            c->depth = self->depth + 1;
            c->max_depth = self->max_depth;

            //knd_log(".. export %.*s class, depth:%zu max depth:%zu\n\n",
            //    c->name_size, c->name, c->depth, c->max_depth);
            
            err = c->export(c);
            if (err) return err;
            break;
        case KND_ATTR_AGGR:
            err = aggr_item_export_JSON(self, item);
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

static int ref_item_export_JSON(struct kndClass *self,
                                struct kndAttrVar *item)
{
    struct kndClass *c;
    int err;

    assert(item->class != NULL);

    c = item->class;
    c->depth = self->depth + 1;
    c->max_depth = self->max_depth;
    err = c->export(c);
    if (err) return err;

    return knd_OK;
}

static int proc_item_export_JSON(struct kndClass *self,
                                 struct kndAttrVar *item)
{
    struct kndProc *proc;
    int err;

    assert(item->proc != NULL);

    proc = item->proc;
    proc->depth = self->depth + 1;
    proc->max_depth = self->max_depth;
    err = proc->export(proc);
    if (err) return err;

    return knd_OK;
}

static int attr_var_list_export_JSON(struct kndClass *self,
                                     struct kndAttrVar *parent_item)
{
    struct glbOutput *out = self->entry->repo->out;
    struct kndAttrVar *item;
    bool in_list = false;
    size_t count = 0;
    int err;

    if (DEBUG_JSON_LEVEL_2) {
        knd_log(".. export JSON list: %.*s\n\n",
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
        case KND_ATTR_AGGR:

            parent_item->id_size = sprintf(parent_item->id, "%lu",
                                           (unsigned long)count);
            count++;

            err = aggr_item_export_JSON(self, parent_item);
            if (err) return err;
            break;
        case KND_ATTR_REF:
            err = ref_item_export_JSON(self, parent_item);
            if (err) return err;
            break;
        case KND_ATTR_PROC:
            if (parent_item->proc) {
                err = proc_item_export_JSON(self, parent_item);
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
        case KND_ATTR_AGGR:
            item->id_size = sprintf(item->id, "%lu",
                                    (unsigned long)count);
            count++;

            err = aggr_item_export_JSON(self, item);
            if (err) return err;
            break;
        case KND_ATTR_REF:
            err = ref_item_export_JSON(self, item);
            if (err) return err;
            break;
        case KND_ATTR_PROC:
            if (item->proc) {
                err = proc_item_export_JSON(self, item);
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

static int attr_vars_export_JSON(struct kndClass *self,
                                 struct kndAttrVar *items,
                                 size_t depth __attribute__((unused)))
{
    struct kndAttrVar *item;
    struct glbOutput *out;
    struct kndClass *c;
    int err;

    out = self->entry->repo->out;

    for (item = items; item; item = item->next) {
        err = out->writec(out, ',');
        if (err) return err;

        if (item->attr && item->attr->is_a_set) {
            err = attr_var_list_export_JSON(self, item);
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
        case KND_ATTR_PROC:
            if (item->proc) {
                err = proc_item_export_JSON(self, item);
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
        case KND_ATTR_AGGR:

            if (!item->class) {
                err = aggr_item_export_JSON(self, item);
                if (err) return err;
            } else {
                c = item->class;
                c->depth = self->depth;
                err = c->export(c);
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

static int present_computed_class_attrs(struct kndClass *self,
                                        struct kndClassVar *cvar)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    struct glbOutput *out = self->entry->repo->out;
    struct ooDict *attr_name_idx = self->attr_name_idx;
    struct kndAttr *attr;
    struct kndAttrVar *attr_var;
    struct kndAttrEntry *entry;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    long numval;
    int err;

    for (size_t i = 0; i < self->num_computed_attrs; i++) {
        attr = self->computed_attrs[i];
        entry = attr_name_idx->get(attr_name_idx,
                                   attr->name, attr->name_size);
        if (!entry) {
            knd_log("-- attr %.*s not indexed?",
                    attr->name_size, attr->name);
            return knd_FAIL;
        }

        attr_var = entry->attr_var;
        if (!attr_var) {
            err = mempool->new_attr_var(mempool, &attr_var);
            if (err) {
                knd_log("-- attr item mempool exhausted");
                return knd_NOMEM;
            }
            attr_var->attr = attr;
            attr_var->class_var = cvar;
            memcpy(attr_var->name, attr->name, attr->name_size);
            attr_var->name_size = attr->name_size;
            entry->attr_var = attr_var;
        }

        switch (attr->type) {
        case KND_ATTR_NUM:
            numval = attr_var->numval;
            if (!attr_var->is_cached) {

                // TODO
                //err = compute_class_attr_num_value(self, cvar, attr_var);
                //if (err) continue;

                numval = attr_var->numval;
                //attr_var->numval = numval;
                //attr_var->is_cached = true;
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
            
            buf_size = snprintf(buf, KND_NAME_SIZE, "%lu", numval);
            err = out->write(out, buf, buf_size);                                     RET_ERR();
            break;
        default:
            break;
        }
    }

    return knd_OK;
}

static int export_gloss_JSON(struct kndClass *self)
{
    struct kndTranslation *tr;
    struct glbOutput *out = self->entry->repo->out;
    struct kndTask *task = self->entry->repo->task;
    int err;

    for (tr = self->tr; tr; tr = tr->next) {
        if (memcmp(task->locale, tr->locale, tr->locale_size)) {
            continue;
        }

        err = out->write(out, ",\"_gloss\":\"", strlen(",\"_gloss\":\""));        RET_ERR();
        err = out->write_escaped(out, tr->val,  tr->val_size);                    RET_ERR();
        err = out->write(out, "\"", 1);                                           RET_ERR();
        break;
    }

    for (tr = self->summary; tr; tr = tr->next) {
        if (memcmp(task->locale, tr->locale, tr->locale_size)) {
            continue;
        }
        err = out->write(out, ",\"_summary\":\"", strlen(",\"_summary\":\""));    RET_ERR();
        err = out->write_escaped(out, tr->val,  tr->val_size);                    RET_ERR();
        err = out->write(out, "\"", 1);                                           RET_ERR();
        break;
    }

    return knd_OK;
}

static int export_concise_JSON(struct kndClass *self)
{
    struct kndClassVar *item;
    struct kndAttrVar *attr_var;
    struct kndAttr *attr;
    struct kndAttrEntry *attr_entry;
    struct kndClass *c;
    const char *key;
    void *val;
    struct glbOutput *out = self->entry->repo->out;
    int err;

    if (DEBUG_JSON_LEVEL_2)
        knd_log(".. export concise JSON for %.*s..",
                self->entry->name_size, self->entry->name, self->entry->repo->out);

    for (item = self->baseclass_vars; item; item = item->next) {
        for (attr_var = item->attrs; attr_var; attr_var = attr_var->next) {

            /* TODO assert */
            if (!attr_var->attr) continue;

            attr = attr_var->attr;

            if (!attr->concise_level) continue;

            if (attr->is_a_set) {
                err = out->writec(out, ',');                                      RET_ERR();
                err = attr_var_list_export_JSON(self, attr_var);
                if (err) return err;
                continue;
            }

            /* single elem concise representation */
            err = out->writec(out, ',');                                          RET_ERR();
            err = out->writec(out, '"');                                          RET_ERR();

            err = out->write(out, attr_var->name, attr_var->name_size);
            if (err) return err;
            err = out->write(out, "\":", strlen("\":"));
            if (err) return err;

            switch (attr->type) {
            case KND_ATTR_NUM:
                err = out->write(out, attr_var->val, attr_var->val_size);
                if (err) return err;
                break;
            case KND_ATTR_REF:
                c = attr_var->class;
                if (c) {
                    c->depth = self->depth;
                    c->max_depth = self->max_depth;
                    err = c->export(c);
                    if (err) return err;
                } else {
                    err = out->write(out, "\"", strlen("\""));
                    if (err) return err;
                    err = out->write(out, attr_var->val, attr_var->val_size);
                    if (err) return err;
                    err = out->write(out, "\"", strlen("\""));
                    if (err) return err;
                }
                break;
            case KND_ATTR_AGGR:
                err = aggr_item_export_JSON(self, attr_var);
                if (err) return err;
                break;
            default:
                err = out->write(out, "\"", strlen("\""));
                if (err) return err;
                err = out->write(out, attr_var->val, attr_var->val_size);
                if (err) return err;
                err = out->write(out, "\"", strlen("\""));
                if (err) return err;
            }
        }
    }

    /* inherited attrs */
    if (self->attr_name_idx) {
        key = NULL;
        self->attr_name_idx->rewind(self->attr_name_idx);
        do {
            self->attr_name_idx->next_item(self->attr_name_idx, &key, &val);
            if (!key) break;

            attr_entry = val;
            attr = attr_entry->attr;

            /* skip over immediate attrs */
            if (attr->parent_class == self) continue;

            /* display only concise fields */
            if (!attr->concise_level) continue;

            attr_var = attr_entry->attr_var;
            if (!attr_var) {
                err = knd_get_attr_var(self, attr->name, attr->name_size, &attr_var);
                if (err) continue;
            }
            
            /* NB: only plain text fields */
            switch (attr->type) {
            case KND_ATTR_STR:
                err = out->writec(out, ',');                                          RET_ERR();
                err = out->writec(out, '"');                                          RET_ERR();
                err = out->write(out, attr_var->name, attr_var->name_size);           RET_ERR();
                err = out->write(out, "\":", strlen("\":"));                          RET_ERR();
                err = out->write(out, "\"", strlen("\""));                            RET_ERR();
                err = out->write(out, attr_var->val, attr_var->val_size);             RET_ERR();
                err = out->write(out, "\"", strlen("\""));                            RET_ERR();
                break;
            default:
                break;
            }
            
        } while (key);
    }

    return knd_OK;
}


static int export_inherited_attrs_JSON(struct kndClass *self)
{
    struct kndAttrVar *attr_var;
    struct kndAttr *attr;
    struct kndAttrEntry *attr_entry;
    struct kndClass *c;
    const char *key = NULL;
    void *val;
    struct glbOutput *out = self->entry->repo->out;
    int err;

    self->attr_name_idx->rewind(self->attr_name_idx);
    do {
        self->attr_name_idx->next_item(self->attr_name_idx, &key, &val);
        if (!key) break;
        
        attr_entry = val;
        attr = attr_entry->attr;

        /* skip over immediate attrs */
        if (attr->parent_class == self) continue;

        if (DEBUG_JSON_LEVEL_2)
            knd_log(".. %.*s class to export inherited attr: %.*s",
                    self->name_size, self->name,
                    attr->name_size, attr->name);

        attr_var = attr_entry->attr_var;
        if (!attr_var) {
            err = knd_get_attr_var(self, attr->name, attr->name_size, &attr_var);
            if (err) continue;
        }
        
        /* NB: only plain text fields */
        switch (attr->type) {
        case KND_ATTR_STR:
            err = out->writec(out, ',');                                          RET_ERR();
            err = out->writec(out, '"');                                          RET_ERR();
            err = out->write(out, attr_var->name, attr_var->name_size);           RET_ERR();
            err = out->write(out, "\":", strlen("\":"));                          RET_ERR();
            err = out->write(out, "\"", strlen("\""));                            RET_ERR();
            err = out->write(out, attr_var->val, attr_var->val_size);             RET_ERR();
            err = out->write(out, "\"", strlen("\""));                            RET_ERR();
            break;
        default:
            break;
        }
    } while (key);

    return knd_OK;
}

extern int knd_class_export_set_JSON(struct kndClass *self,
                                     struct glbOutput *out,
                                     struct kndSet *set)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndTask *task = self->entry->repo->task;
    int err;

    err = out->write(out, "{\"_set\":{",
                     strlen("{\"_set\":{"));                                      RET_ERR();

    /* TODO: present child clauses */

    if (set->base) {
        err = out->write(out, "\"_base\":\"",
                         strlen("\"_base\":\""));                                 RET_ERR();
        err = out->write(out, set->base->name,  set->base->name_size);            RET_ERR();
        err = out->writec(out, '"');                                              RET_ERR();
    }
    err = out->writec(out, '}');                                                  RET_ERR();

    buf_size = sprintf(buf, ",\"total\":%lu",
                       (unsigned long)set->num_elems);
    err = out->write(out, buf, buf_size);                                         RET_ERR();


    err = out->write(out, ",\"batch\":[",
                     strlen(",\"batch\":["));                                     RET_ERR();

    err = set->map(set, export_conc_elem_JSON, (void*)self);
    if (err && err != knd_RANGE) return err;

    err = out->writec(out, ']');                                                  RET_ERR();

    buf_size = sprintf(buf, ",\"batch_max\":%lu",
                       (unsigned long)task->batch_max);
    err = out->write(out, buf, buf_size);                                        RET_ERR();
    buf_size = sprintf(buf, ",\"batch_size\":%lu",
                       (unsigned long)task->batch_size);
    err = out->write(out, buf, buf_size);                                     RET_ERR();
    err = out->write(out,
                     ",\"batch_from\":", strlen(",\"batch_from\":"));         RET_ERR();
    buf_size = sprintf(buf, "%lu",
                       (unsigned long)task->batch_from);
    err = out->write(out, buf, buf_size);                                     RET_ERR();

    err = out->writec(out, '}');                                                  RET_ERR();
    return knd_OK;
}

extern int knd_class_export_JSON(struct kndClass *self,
                                 struct glbOutput *out)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndAttr *attr;

    struct kndClass *c;
    struct kndClassVar *item;
    struct kndClassEntry *entry;

    size_t item_count;
    int i, err;

    if (DEBUG_JSON_LEVEL_TMP)
        knd_log(".. JSON export: \"%.*s\"  ",
                self->entry->name_size, self->entry->name);

    err = out->write(out, "{", 1);                                                RET_ERR();
    err = out->write(out, "\"_name\":\"", strlen("\"_name\":\""));                RET_ERR();
    err = out->write_escaped(out, self->entry->name, self->entry->name_size);     RET_ERR();
    err = out->write(out, "\"", 1);                                               RET_ERR();

    err = out->write(out, ",\"_id\":", strlen(",\"_id\":"));                      RET_ERR();
    err = out->writef(out, "%zu", self->entry->numid);                            RET_ERR();

    err = export_gloss_JSON(self);                                                RET_ERR();

    if (self->depth >= self->max_depth) {
        /* any concise fields? */
        err = export_concise_JSON(self);                                          RET_ERR();
        goto final;
    }

    /* state info */
    if (self->num_states) {
        err = out->writec(out, ',');
        if (err) return err;
        err = export_class_state_JSON(self);                                      RET_ERR();
    }
    if (self->num_inst_states) {
        err = out->writec(out, ',');
        if (err) return err;
        err = export_class_inst_state_JSON(self);                                      RET_ERR();
    }

    /* display base classes only once */
    if (self->num_baseclass_vars) {
        err = out->write(out, ",\"_base\":[", strlen(",\"_base\":["));            RET_ERR();

        item_count = 0;
        for (item = self->baseclass_vars; item; item = item->next) {
            if (item_count) {
                err = out->write(out, ",", 1);                                    RET_ERR();
            }

            err = out->write(out, "{\"_name\":\"", strlen("{\"_name\":\""));      RET_ERR();
            err = out->write(out, item->entry->name, item->entry->name_size);
            if (err) return err;
            err = out->write(out, "\"", 1);
            if (err) return err;

            err = out->write(out, ",\"_id\":", strlen(",\"_id\":"));
            if (err) return err;
            buf_size = snprintf(buf, KND_NAME_SIZE, "%zu", item->numid);
            err = out->write(out, buf, buf_size);
            if (err) return err;

            if (item->attrs) {
                err = attr_vars_export_JSON(self, item->attrs, 0);
                if (err) return err;
            }

            if (self->num_computed_attrs) {
                if (DEBUG_JSON_LEVEL_2)
                    knd_log("\n.. export computed attrs of class %.*s",
                            self->name_size, self->name);
                err = present_computed_class_attrs(self, item);
                if (err) return err;
            }

            if (self->attr_name_idx) {
                err = export_inherited_attrs_JSON(self);                          RET_ERR();
            }

            err = out->write(out, "}", 1);
            if (err) return err;
            item_count++;
        }
        err = out->write(out, "]", 1);
        if (err) return err;
    }

    if (self->attrs) {
        err = out->write(out, ",\"_attrs\":{",
                         strlen(",\"_attrs\":{"));
        if (err) return err;

        i = 0;
        for (attr = self->attrs; attr; attr = attr->next) {
            if (i) {
                err = out->write(out, ",", 1);
                if (err) return err;
            }
            err = attr->export(attr, KND_FORMAT_JSON, out);
            if (err) {
                if (DEBUG_JSON_LEVEL_TMP)
                    knd_log("-- failed to export %.*s attr",
                            attr->name_size, attr->name);
                return err;
            }
            i++;
        }
        err = out->writec(out, '}');
        if (err) return err;
    }

    
    /* non-terminal classes */
    if (self->entry->num_children) {
        err = out->write(out, ",\"_num_subclasses\":",
                         strlen(",\"_num_subclasses\":"));
        if (err) return err;

        buf_size = sprintf(buf, "%zu", self->entry->num_children);
        err = out->write(out, buf, buf_size);
        if (err) return err;

        if (self->entry->num_terminals) {
            err = out->write(out, ",\"_num_terminals\":",
                             strlen(",\"_num_terminals\":"));
            if (err) return err;
            buf_size = sprintf(buf, "%zu", self->entry->num_terminals);
            err = out->write(out, buf, buf_size);
            if (err) return err;
        }

        if (self->entry->num_children) {
            err = out->write(out, ",\"_subclasses\":[",
                             strlen(",\"_subclasses\":["));
            if (err) return err;

            for (size_t i = 0; i < self->entry->num_children; i++) {
                entry = self->entry->children[i];
                if (i) {
                    err = out->write(out, ",", 1);
                    if (err) return err;
                }
                err = out->write(out, "{\"_name\":\"", strlen("{\"_name\":\""));
                if (err) return err;
                err = out->write(out, entry->name, entry->name_size);
                if (err) return err;
                err = out->write(out, "\"", 1);
                if (err) return err;

                err = out->write(out, ",\"_id\":", strlen(",\"_id\":"));
                if (err) return err;
                buf_size = sprintf(buf, "%zu", entry->numid);
                err = out->write(out, buf, buf_size);
                if (err) return err;

                if (entry->num_terminals) {
                    err = out->write(out, ",\"_num_terminals\":",
                                     strlen(",\"_num_terminals\":"));
                    if (err) return err;
                    buf_size = sprintf(buf, "%zu", entry->num_terminals);
                    err = out->write(out, buf, buf_size);
                    if (err) return err;
                }

                /* localized glosses */
                c = entry->class;
                if (!c) {
                    //err = unfreeze_class(self, entry, &c);                          RET_ERR();
                }

                err = export_gloss_JSON(c);                                       RET_ERR();

                err = export_concise_JSON(c);                                     RET_ERR();

                err = out->write(out, "}", 1);
                if (err) return err;
            }
            err = out->write(out, "]", 1);
            if (err) return err;
        }

        err = out->write(out, "}", 1);
        if (err) return err;
        return knd_OK;
    }

    /* instances */
    if (self->entry->num_objs) {
        err = out->write(out, ",\"_instances\":{",
                         strlen(",\"_instances\":{"));
        if (err) return err;

        err = out->write(out, "\"_tot\":", strlen("\"_tot\":"));
        buf_size = sprintf(buf, "%zu", self->entry->num_objs);
        err = out->write(out, buf, buf_size);
        if (err) return err;

        // TODO navigation facets?

        err = out->writec(out, '}');
        if (err) return err;
    }

 final:
    err = out->write(out, "}", 1);
    if (err) return err;

    return knd_OK;
}

