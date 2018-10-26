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

extern int knd_export_class_state_JSON(struct kndClass *self,
                                       struct glbOutput *out)
{
    struct kndState *state = self->states;
    size_t latest_state_numid = self->init_state + self->num_states;
    size_t total;
    int err;

    err = out->write(out, "\"_state\":", strlen("\"_state\":"));                  RET_ERR();
    err = out->writef(out, "%zu", latest_state_numid);                            RET_ERR();

    if (state) {
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
        case KND_CREATED:
            err = out->write(out,   ",\"_phase\":\"new\"",
                             strlen(",\"_phase\":\"new\""));                      RET_ERR();
            break;
        default:
            break;
        }
    }

    state = self->desc_states;
    if (state) {
        latest_state_numid = self->init_desc_state + self->num_desc_states;
        total = 0;
        if (state->val)
            total = state->val->val_size;

        err = out->write(out, ",\"descendants\":{",
                         strlen(",\"descendants\":{"));                           RET_ERR();
        err = out->write(out, "\"_state\":", strlen("\"_state\":"));              RET_ERR();
        err = out->writef(out, "%zu", latest_state_numid);                        RET_ERR();
        err = out->write(out, ",\"total\":", strlen(",\"total\":"));              RET_ERR();
        err = out->writef(out, "%zu", total);                                     RET_ERR();
        err = out->writec(out, '}');                                              RET_ERR();
    }

    state = self->inst_states;
    if (state) {
        latest_state_numid = self->init_inst_state + self->num_inst_states;
        total = 0;
        if (state->val)
            total = state->val->val_size;

        err = out->write(out, ",\"instances\":{",
                         strlen(",\"instances\":{"));                             RET_ERR();
        err = out->write(out, "\"_state\":", strlen("\"_state\":"));              RET_ERR();
        err = out->writef(out, "%zu", latest_state_numid);                        RET_ERR();
        err = out->write(out, ",\"total\":", strlen(",\"total\":"));              RET_ERR();
        err = out->writef(out, "%zu", total);                                     RET_ERR();
        err = out->writec(out, '}');                                              RET_ERR();
    }
    
    // TODO
    /*time(&update->timestamp);
    localtime_r(&update->timestamp, &tm_info);
    buf_size = strftime(buf, KND_NAME_SIZE,
                        ",\"_modif\":\"%Y-%m-%d %H:%M:%S\"", &tm_info);
    err = out->write(out, buf, buf_size);                                         RET_ERR();
    */

    return knd_OK;
}

extern int knd_export_class_inst_state_JSON(struct kndClass *self)
{
    struct glbOutput *out = self->entry->repo->out;
    size_t latest_state_id = 0;
    int err;

    if (self->inst_states)
        latest_state_id = self->inst_states->numid;

    err = out->write(out, "\"_state\":",
                     strlen("\"_state\":"));                                      RET_ERR();
    err = out->writef(out, "%zu", latest_state_id);                               RET_ERR();

    if (self->entry->inst_idx) {
        err = out->write(out, ",\"_tot\":", strlen(",\"_tot\":"));                  RET_ERR();
        err = out->writef(out, "%zu", self->entry->inst_idx->num_valid_elems);      RET_ERR();
    } else {
        err = out->write(out, ",\"_tot\":0", strlen(",\"_tot\":"));                  RET_ERR();
    }
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
    struct kndState *state;
    int err;

    if (DEBUG_JSON_LEVEL_2)
        knd_log("..export elem: %.*s",
                elem_id_size, elem_id);

    if (!c) {
        //err = unfreeze_class(self, entry, &c);                                      RET_ERR();
        return knd_OK;
    }

    if (!task->show_removed_objs) {
        state = c->states;
        if (state && state->phase == KND_REMOVED) return knd_OK;
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

    err = knd_class_export(c, KND_FORMAT_JSON, out);
    if (err) return err;

    task->batch_size++;
    return knd_OK;
}

static int export_class_ref_JSON(void *obj,
                                 const char *unused_var(elem_id),
                                 size_t unused_var(elem_id_size),
                                 size_t count,
                                 void *elem)
{
    struct kndClass *self = obj;
    struct glbOutput *out = self->entry->repo->out;
    struct kndClassEntry *entry = elem;
    struct kndClass *c = entry->class;
    int err;

    /* separator */
    if (count) {
        err = out->writec(out, ',');                                              RET_ERR();
    }
    c->depth = 0;
    c->max_depth = 0;
    err = knd_class_export(c, KND_FORMAT_JSON, out);
    if (err) return err;
    return knd_OK;
}

static int export_gloss_JSON(struct kndClass *self,
                             struct glbOutput *out)
{
    struct kndTranslation *tr;
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

    return knd_OK;
}


static int export_concise_JSON(struct kndClass *self,
                               struct glbOutput *out)
{
    struct kndClassVar *item;
    int err;

    if (DEBUG_JSON_LEVEL_2)
        knd_log(".. export concise JSON for %.*s..",
                self->entry->name_size, self->entry->name);

    for (item = self->baseclass_vars; item; item = item->next) {
        if (!item->attrs) continue;
        err = knd_attr_vars_export_JSON(item->attrs,
                                        self->entry->repo->task, out, true);      RET_ERR();
    }

    if (DEBUG_JSON_LEVEL_2)
        knd_log(".. export inherited attrs of %.*s..",
                self->entry->name_size, self->entry->name);

    /* inherited attrs */
    err = self->attr_idx->map(self->attr_idx,
                              knd_export_inherited_attr, (void*)self); 
    if (err && err != knd_RANGE) return err;

    return knd_OK;
}

extern int knd_class_export_set_JSON(struct kndClass *self,
                                     struct kndSet *set,
                                     struct glbOutput *out)
{
    struct kndTask *task = self->entry->repo->task;
    int err;

    err = out->write(out, "{\"_set\":{",
                     strlen("{\"_set\":{"));                                      RET_ERR();

    /* TODO: present child clauses */

    if (set->base) {
        err = out->write(out, "\"_is\":\"",
                         strlen("\"_is\":\""));                                   RET_ERR();
        err = out->write(out, set->base->name,  set->base->name_size);            RET_ERR();
        err = out->writec(out, '"');                                              RET_ERR();
    }
    err = out->writec(out, '}');                                                  RET_ERR();

    if (task->show_removed_objs) {
        err = out->writef(out, ",\"total\":%lu",
                          (unsigned long)set->num_elems);                         RET_ERR();
    } else {
        err = out->writef(out, ",\"total\":%lu",
                          (unsigned long)set->num_valid_elems);                   RET_ERR();
    }

    err = out->write(out, ",\"batch\":[",
                     strlen(",\"batch\":["));                                     RET_ERR();

    err = set->map(set, export_conc_elem_JSON, (void*)self);
    if (err && err != knd_RANGE) return err;

    err = out->writec(out, ']');                                                  RET_ERR();

    err = out->writef(out, ",\"batch_max\":%lu",
                      (unsigned long)task->batch_max);                            RET_ERR();
    err = out->writef(out, ",\"batch_size\":%lu",
                       (unsigned long)task->batch_size);                          RET_ERR();

    err = out->write(out,
                     ",\"batch_from\":", strlen(",\"batch_from\":"));             RET_ERR();

    err = out->writef(out,"%lu",
                      (unsigned long)task->batch_from);                          RET_ERR();
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
    err = export_gloss_JSON(c, out);                                               RET_ERR();
    err = export_concise_JSON(c, out);                                             RET_ERR();
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
    struct kndClass *c;
    struct kndState *state;
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
        c = ref->class;
        // TODO: defreeze
        if (!c) continue;
        
        state = c->states;
        if (state && state->phase == KND_REMOVED) continue;

        if (in_list) {
            err = out->write(out, ",", 1);  RET_ERR();
        }

        err = present_subclass(ref, out);                         RET_ERR();
        in_list = true;
    }

    if (orig_entry) {
        for (ref = orig_entry->children; ref; ref = ref->next) {
            c = ref->class;
            // TODO: defreeze
            if (!c) continue;
            state = c->states;
            if (state && state->phase == KND_REMOVED) continue;

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

static int export_attrs(struct kndClass *self,
                        struct glbOutput *out)
{
    struct kndAttr *attr;
    size_t i = 0;
    int err;
    err = out->write(out, ",\"_attrs\":{",
                     strlen(",\"_attrs\":{"));   RET_ERR();
    
    for (attr = self->attrs; attr; attr = attr->next) {
        if (i) {
            err = out->write(out, ",", 1);  RET_ERR();
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
    err = out->writec(out, '}'); RET_ERR();
    return knd_OK;
}

static int export_baseclass_vars(struct kndClass *self,
                                 struct glbOutput *out)
{
    struct kndClassVar *item;
    size_t item_count = 0;
    int err;

    err = out->write(out, ",\"_is\":[", strlen(",\"_is\":["));                    RET_ERR();

    for (item = self->baseclass_vars; item; item = item->next) {
        if (item_count) {
            err = out->write(out, ",", 1);                                        RET_ERR();
        }

        err = out->write(out, "{\"_name\":\"", strlen("{\"_name\":\""));          RET_ERR();
        err = out->write(out, item->entry->name, item->entry->name_size);         RET_ERR();
        err = out->write(out, "\"", 1);                                           RET_ERR();

        err = out->write(out, ",\"_id\":", strlen(",\"_id\":"));                  RET_ERR();
        err = out->writef(out, "%zu", item->numid);                               RET_ERR();

        if (item->entry->class) {
            err = export_gloss_JSON(item->entry->class, out);                     RET_ERR();
        }

        if (item->attrs) {
            item->attrs->depth = self->depth;
            item->attrs->max_depth = self->max_depth;
            err = knd_attr_vars_export_JSON(item->attrs,
                                            self->entry->repo->task, out, false);      RET_ERR();
        }

        /*if (self->num_computed_attrs) {
            if (DEBUG_JSON_LEVEL_TMP)
                knd_log("\n.. export computed attrs of class %.*s",
                        self->name_size, self->name);
            err = present_computed_class_attrs(self, item);
            if (err) return err;
            }*/


        err = out->write(out, "}", 1);      RET_ERR();
        item_count++;
    }
    err = out->write(out, "]", 1);
    if (err) return err;

    return knd_OK;
}
                                     
extern int knd_class_export_JSON(struct kndClass *self,
                                 struct glbOutput *out)
{
    struct kndClassEntry *entry = self->entry;
    struct kndClassEntry *orig_entry = entry->orig;
    struct kndTask *task = entry->repo->task;
    struct kndSet *set;
    struct kndAttr *attr;
    struct kndClassRel *class_rel;
    struct kndState *state = self->states;
    size_t num_children;
    bool in_list = false;
    int err;

    if (DEBUG_JSON_LEVEL_2) {
        knd_log("\n.. JSON export: \"%.*s\" (repo:%.*s)  depth:%zu max depth:%zu",
                entry->name_size, entry->name,
                entry->repo->name_size, entry->repo->name,
                self->depth, self->max_depth);

        knd_log("= self:%p orig_entry:%p", self, orig_entry);
        if (orig_entry) {
            knd_log("= orig_entry_class:%p \"%.*s\" (repo:%.*s)",
                    orig_entry->class,
                    orig_entry->class->name_size,
                    orig_entry->class->name,
                    orig_entry->repo->name_size,
                    orig_entry->repo->name);

            if (orig_entry->class) {
                knd_log("= orig_entry_class class vars:%zu", 
                        orig_entry->class->num_baseclass_vars);
            }
        }
    }

    err = out->write(out, "{", 1);                                                RET_ERR();
    err = out->write(out, "\"_name\":\"", strlen("\"_name\":\""));                RET_ERR();
    err = out->write_escaped(out, entry->name, entry->name_size);                 RET_ERR();
    err = out->writec(out, '"');                                                  RET_ERR();

    err = out->write(out, ",\"_id\":", strlen(",\"_id\":"));                      RET_ERR();
    err = out->writef(out, "%zu", entry->numid);                                  RET_ERR();

    if (task->max_depth == 0) {
        goto final;
    }

    err = out->write(out, ",\"_repo\":\"", strlen(",\"_repo\":\""));              RET_ERR();
    err = out->write(out, entry->repo->name,
                     entry->repo->name_size);                                     RET_ERR();
    err = out->writec(out, '"');                                                  RET_ERR();

    if (state) {
        err = out->write(out, ",\"_state\":", strlen(",\"_state\":"));            RET_ERR();
        err = out->writef(out, "%zu", state->numid);                              RET_ERR();

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
        case KND_CREATED:
            err = out->write(out,   ",\"_phase\":\"new\"",
                             strlen(",\"_phase\":\"new\""));                      RET_ERR();
            break;
        default:
            break;
        }
    }
    
    err = export_gloss_JSON(self, out);                                                RET_ERR();

    if (self->depth >= self->max_depth) {
        /* any concise fields? */
        err = export_concise_JSON(self, out);                                          RET_ERR();
        goto final;
    }

    /* state info */
    if (self->num_states) {
        err = out->writec(out, ',');
        if (err) return err;
        err = knd_export_class_state_JSON(self, out);                                      RET_ERR();
    }

    /*if (self->num_inst_states) {
        err = out->writec(out, ',');
        if (err) return err;
        err = export_class_inst_state_JSON(self);                                      RET_ERR();
        }*/

    /* display base classes only once */
    if (self->num_baseclass_vars) {
        err = export_baseclass_vars(self, out);                          RET_ERR();
    } else {
        if (orig_entry && orig_entry->class->num_baseclass_vars) {
            err = export_baseclass_vars(orig_entry->class, out);         RET_ERR();
        }
    }

    if (self->attrs) {
        err = export_attrs(self, out); RET_ERR();
    } else {
        if (orig_entry && orig_entry->class->num_attrs) {
            err = export_attrs(orig_entry->class, out);                   RET_ERR();
        }
    }

    /* inherited attrs */
    set = self->attr_idx;
    err = set->map(set, knd_export_inherited_attr, (void*)self); 
    if (err && err != knd_RANGE) return err;

    num_children = entry->num_children;
    if (self->desc_states) {
        state = self->desc_states;
        if (state->val) 
            num_children = state->val->val_size;
    }

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
            err = knd_export_class_inst_state_JSON(self);                         RET_ERR();
        }

        // TODO navigation facets?

        err = out->writec(out, '}');
        if (err) return err;
    }


    /* relations */
    if (entry->reverse_rels) {
        err = out->write(out, ",\"_rels\":[", strlen(",\"_rels\":["));            RET_ERR();

        for (class_rel = entry->reverse_rels; class_rel;
             class_rel = class_rel->next) {
            if (in_list) {
                err = out->writec(out, ',');                                      RET_ERR();
            }
            err = out->writec(out, '{');                                          RET_ERR();
            err = out->write(out, "\"topic\":\"",
                             strlen("\"topic\":\""));                             RET_ERR();
            err = out->write(out,
                             class_rel->topic->name,
                             class_rel->topic->name_size);                        RET_ERR();
            err = out->writec(out, '"');                                          RET_ERR();

            attr = class_rel->attr;
            err = out->write(out, ",\"attr\":\"",
                             strlen(",\"attr\":\""));                             RET_ERR();
            err = out->write(out, attr->name,
                             attr->name_size);                                    RET_ERR();
            err = out->writec(out, '"');                                          RET_ERR();

            err = out->write(out, ",\"total\":",
                             strlen(",\"total\":"));                              RET_ERR();
            err = out->writef(out, "%zu", class_rel->set->num_elems);             RET_ERR();

            if (task->show_rels) {
                set = class_rel->set;
                err = out->write(out, ",\"batch\":[",
                                 strlen(",\"batch\":["));                         RET_ERR();
                task->max_depth = 0;
                err = set->map(set, export_class_ref_JSON, (void*)self);
                if (err && err != knd_RANGE) return err;
                err = out->writec(out, ']');                                      RET_ERR();
                task->max_depth = 1;
            }

            err = out->writec(out, '}');                                          RET_ERR();
            in_list = true;
        }
        err = out->writec(out, ']');                                              RET_ERR();
    }
    
 final:
    err = out->write(out, "}", 1);
    if (err) return err;

    return knd_OK;
}

