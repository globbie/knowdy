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

extern int knd_export_class_inst_state_JSON(struct kndClass *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    struct glbOutput *out = self->entry->repo->out;
    struct kndUpdate *update;
    struct tm tm_info;
    size_t latest_state = self->init_inst_state + self->num_inst_states;
    struct kndState *state;
    int err;

    /* latest inst update */
    err = out->write(out, "\"_num_states\":",
                     strlen("\"_num_states\":"));                                 RET_ERR();
    err = out->writef(out, "%zu", latest_state);                                  RET_ERR();

    if (self->inst_states) {
        state = self->inst_states;
        if (state->update) {
            update = state->update;
            time(&update->timestamp);
            localtime_r(&update->timestamp, &tm_info);
            buf_size = strftime(buf, KND_NAME_SIZE,
                                ",\"_modif\":\"%Y-%m-%d %H:%M:%S\"", &tm_info);
            err = out->write(out, buf, buf_size);                                     RET_ERR();
        }
    }

    err = out->write(out, ",\"_tot\":", strlen(",\"_tot\":"));                  RET_ERR();
    err = out->writef(out, "%zu", self->entry->inst_idx->num_valid_elems);      RET_ERR();

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
        knd_log("..export elem: %.*s",
                elem_id_size, elem_id);

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

    err = c->export(c, KND_FORMAT_JSON, out);
    if (err) return err;

    task->batch_size++;
    return knd_OK;
}

static int present_computed_class_attrs(struct kndClass *self,
                                        struct kndClassVar *cvar)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    struct glbOutput *out = self->entry->repo->out;
    struct ooDict *attr_name_idx = self->entry->repo->attr_name_idx;
    struct kndAttr *attr;
    struct kndAttrVar *attr_var;
    struct kndAttrRef *entry;
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
            err = knd_attr_var_new(mempool, &attr_var);                  RET_ERR();
            attr_var->attr = attr;
            attr_var->class_var = cvar;
            //memcpy(attr_var->name, attr->name, attr->name_size);
            attr_var->name = attr->name;
            attr_var->name_size = attr->name_size;
            entry->attr_var = attr_var;
        }

        switch (attr->type) {
        case KND_ATTR_NUM:
            numval = attr_var->numval;
            if (!attr_var->is_cached) {

                err = knd_compute_class_attr_num_value(self, cvar, attr_var);
                if (err) continue;

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
    struct glbOutput *out = self->entry->repo->out;
    int err;

    if (DEBUG_JSON_LEVEL_2)
        knd_log(".. export concise JSON for %.*s..",
                self->entry->name_size, self->entry->name, self->entry->repo->out);

    for (item = self->baseclass_vars; item; item = item->next) {
        if (!item->attrs) continue;
        err = knd_attr_vars_export_JSON(item->attrs, out, true);               RET_ERR();
    }

    /* inherited attrs */
    err = self->attr_idx->map(self->attr_idx,
                              knd_export_inherited_attr, (void*)self); 
    if (err && err != knd_RANGE) return err;

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
        err = out->write(out, "\"_is\":\"",
                         strlen("\"_is\":\""));                                 RET_ERR();
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


static int present_subclass(struct kndClassRef *ref,
                            struct glbOutput *out)
{
    struct kndClassEntry *entry = ref->entry;
    struct kndClass *c;
    int err;

    err = out->write(out, "{\"_name\":\"", strlen("{\"_name\":\""));          RET_ERR();
    err = out->write(out, entry->name, entry->name_size);                     RET_ERR();
    err = out->write(out, "\"", 1);                                           RET_ERR();

    err = out->write(out, ",\"_id\":", strlen(",\"_id\":"));                  RET_ERR();
    err = out->writef(out, "%zu", entry->numid);                              RET_ERR();

    if (ref->entry->num_terminals) {
        err = out->write(out, ",\"_num_terminals\":",
                         strlen(",\"_num_terminals\":"));                     RET_ERR();
        err = out->writef(out, "%zu", entry->num_terminals);                  RET_ERR();
    }

    /* localized glosses */
    c = entry->class;
    if (!c) {
        //err = unfreeze_class(self, entry, &c);                          RET_ERR();
    }
    err = export_gloss_JSON(c);                                               RET_ERR();
    err = export_concise_JSON(c);                                             RET_ERR();
    err = out->writec(out, '}');                                              RET_ERR();
    return knd_OK;
}

static int present_subclasses(struct kndClass *self,
                              size_t num_children,
                              struct glbOutput *out)
{
    struct kndClassRef *ref;
    struct kndClassEntry *entry = self->entry;
    struct kndClassEntry *orig_entry = entry->orig;
    bool in_list = false;
    int err;

    err = out->write(out, ",\"_num_subclasses\":",
                     strlen(",\"_num_subclasses\":"));                  RET_ERR();

    err = out->writef(out, "%zu", num_children);                        RET_ERR();

    if (entry->num_terminals) {
        err = out->write(out, ",\"_num_terminals\":",
                         strlen(",\"_num_terminals\":"));             RET_ERR();
        err = out->writef(out, "%zu", entry->num_terminals);    RET_ERR();
    }

    err = out->write(out, ",\"_subclasses\":[",
                     strlen(",\"_subclasses\":["));               RET_ERR();
    
    for (ref = entry->children; ref; ref = ref->next) {
        if (in_list) {
            err = out->write(out, ",", 1);  RET_ERR();
        }
        err = present_subclass(ref, out);                         RET_ERR();
        in_list = true;
    }

    if (orig_entry) {
        for (ref = orig_entry->children; ref; ref = ref->next) {
            if (in_list) {
                err = out->write(out, ",", 1);  RET_ERR();
            }
            err = present_subclass(ref, out);                         RET_ERR();
            in_list = true;
        }
    }
    
    err = out->write(out, "]", 1);                                                RET_ERR();
    
    return knd_OK;
}

extern int knd_class_export_JSON(struct kndClass *self,
                                 struct glbOutput *out)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndAttr *attr;
    struct kndClassVar *item;
    struct kndSet *attr_idx;
    struct kndClassEntry *entry = self->entry;
    struct kndClassEntry *orig_entry = entry->orig;
    size_t item_count;
    size_t num_children = 0;
    int i, err;

    if (DEBUG_JSON_LEVEL_2)
        knd_log(".. JSON export: \"%.*s\"   depth:%zu max depth:%zu",
                entry->name_size, entry->name,
                self->depth, self->max_depth);

    err = out->write(out, "{", 1);                                                RET_ERR();
    err = out->write(out, "\"_name\":\"", strlen("\"_name\":\""));                RET_ERR();
    err = out->write_escaped(out, entry->name, entry->name_size);     RET_ERR();
    err = out->writec(out, '"');                                                  RET_ERR();

    err = out->write(out, ",\"_repo\":\"", strlen(",\"_repo\":\""));              RET_ERR();
    err = out->write(out, entry->repo->name,
                     entry->repo->name_size);                               RET_ERR();
    err = out->writec(out, '"');                                                  RET_ERR();

    err = out->write(out, ",\"_id\":", strlen(",\"_id\":"));                      RET_ERR();
    err = out->writef(out, "%zu", entry->numid);                            RET_ERR();

    err = export_gloss_JSON(self);                                                RET_ERR();

    if (self->depth >= self->max_depth) {
        //knd_log("== max depth reached: %zu", self->max_depth);
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

    /*if (self->num_inst_states) {
        err = out->writec(out, ',');
        if (err) return err;
        err = export_class_inst_state_JSON(self);                                      RET_ERR();
        }*/

    /* display base classes only once */
    if (self->num_baseclass_vars) {
        err = out->write(out, ",\"_is\":[", strlen(",\"_is\":["));            RET_ERR();

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
                item->attrs->depth = self->depth;
                item->attrs->max_depth = self->max_depth;
                err = knd_attr_vars_export_JSON(item->attrs, out, false);      RET_ERR();
            }

            if (self->num_computed_attrs) {
                if (DEBUG_JSON_LEVEL_2)
                    knd_log("\n.. export computed attrs of class %.*s",
                            self->name_size, self->name);
                err = present_computed_class_attrs(self, item);
                if (err) return err;
            }


            err = out->write(out, "}", 1);      RET_ERR();
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
            err = knd_attr_export(attr, KND_FORMAT_JSON, out);
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

    /* inherited attrs */
    attr_idx = self->attr_idx;
    err = attr_idx->map(attr_idx,
                        knd_export_inherited_attr, (void*)self); 
    if (err && err != knd_RANGE) return err;

    /* facets */
    if (entry->descendants) {
        //knd_log("++ desc facets: %zu", entry->descendants->num_facets);
        //entry->descendants->num_facets) {
        //err = export_facets_JSON(self, entry->descendants);
        //if (err) return err;
    }

    num_children = entry->num_children;
    if (orig_entry)
        num_children += orig_entry->num_children;
    if (num_children) {
        err = present_subclasses(self, num_children, out);                        RET_ERR();
    }

    /* instances */
    if (entry->inst_idx) {
        err = out->write(out, ",\"_instances\":{",
                         strlen(",\"_instances\":{"));                            RET_ERR();

        if (self->inst_states) {
            err = knd_export_class_inst_state_JSON(self);                                      RET_ERR();
        }

        // TODO navigation facets?

        err = out->writec(out, '}');
        if (err) return err;
    }

 final:
    err = out->write(out, "}", 1);
    if (err) return err;

    return knd_OK;
}

