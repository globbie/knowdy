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
#include "knd_commit.h"
#include "knd_user.h"
#include "knd_repo.h"
#include "knd_mempool.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_shared_set.h"
#include "knd_set.h"
#include "knd_output.h"
#include "knd_utils.h"
#include "knd_http_codes.h"

#define DEBUG_JSON_LEVEL_1 0
#define DEBUG_JSON_LEVEL_2 0
#define DEBUG_JSON_LEVEL_3 0
#define DEBUG_JSON_LEVEL_4 0
#define DEBUG_JSON_LEVEL_5 0
#define DEBUG_JSON_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepo *repo;
    struct kndClass *class;
    struct kndClassVar *class_var;
};

int knd_export_class_state_JSON(struct kndClassEntry *self, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndState *state = self->class->states;
    struct kndClass *c = self->class;
    size_t latest_state_numid = self->class->init_state + self->class->num_states;
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

    state = c->desc_states;
    if (state) {
        latest_state_numid = c->init_desc_state + c->num_desc_states;
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

    state = c->inst_states;
    if (state) {
        latest_state_numid = c->init_inst_state + c->num_inst_states;
        total = 0;
        if (state->val)
            total = state->val->val_size;

        err = out->write(out, ",\"insts\":{",
                         strlen(",\"insts\":{"));                             RET_ERR();
        err = out->write(out, "\"_state\":", strlen("\"_state\":"));              RET_ERR();
        err = out->writef(out, "%zu", latest_state_numid);                        RET_ERR();
        err = out->write(out, ",\"total\":", strlen(",\"total\":"));              RET_ERR();
        err = out->writef(out, "%zu", total);                                     RET_ERR();
        err = out->writec(out, '}');                                              RET_ERR();
    }
    
    // TODO
    /*time(&commit->timestamp);
    localtime_r(&commit->timestamp, &tm_info);
    buf_size = strftime(buf, KND_NAME_SIZE,
                        ",\"_modif\":\"%Y-%m-%d %H:%M:%S\"", &tm_info);
    err = out->write(out, buf, buf_size);                                         RET_ERR();
    */

    return knd_OK;
}

int knd_export_class_inst_state_JSON(struct kndClass *self, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    size_t latest_state_id = 0;
    int err;

    if (self->inst_states)
        latest_state_id = self->inst_states->numid;

    err = out->write(out, "\"_state\":",
                     strlen("\"_state\":"));                                      RET_ERR();
    err = out->writef(out, "%zu", latest_state_id);                               RET_ERR();

    if (self->inst_idx) {
        err = out->write(out, ",\"_tot\":", strlen(",\"_tot\":"));                  RET_ERR();
        err = out->writef(out, "%zu", self->inst_idx->num_valid_elems);      RET_ERR();
    } else {
        err = out->write(out, ",\"_tot\":0", strlen(",\"_tot\":0"));                  RET_ERR();
    }
    return knd_OK;
}

static int export_class_ref(void *obj, const char *unused_var(elem_id), size_t unused_var(elem_id_size),
                            size_t unused_var(count), void *elem)
{
    struct kndTask *task = obj;
    size_t indent_size = task->ctx->format_indent;
    size_t depth = task->depth;
    // if (count < task->start_from) return knd_OK;
    // if (task->batch_size >= task->batch_max) return knd_RANGE;

    struct kndOutput *out = task->out;
    struct kndClassRef *ref = elem;
    struct kndClassEntry *entry = ref->entry;
    struct kndClassInstRef *inst_ref;
    size_t inst_ref_count = 0;
    int err;

    if (task->batch_size) {
        OUT(",", 1);
    }
    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, (depth) * indent_size);
        RET_ERR();
    }
    OUT("{", 1);
    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, (depth + 1) * indent_size);
        RET_ERR();
    }
    OUT("\"class\":", strlen("\"class\":"));
    if (indent_size) {
        OUT(" ", 1);
    }
    OUT("\"", 1);
    OUT(entry->name, entry->name_size);
    OUT("\"", 1);

    if (ref->insts) {
        OUT(",", 1);
        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth + 1) * indent_size);
            RET_ERR();
        }
        OUT("\"insts\":", strlen("\"insts\":"));
        if (indent_size) {
            OUT(" ", 1);
        }
        OUT("[", 1);
        
        FOREACH (inst_ref, ref->insts) {
            if (inst_ref_count) {
                OUT(",", 1);
            }
            OUT("{", 1);
            OUT("\"name\":", strlen("\"name\":"));
            if (indent_size) {
                OUT(" ", 1);
            }
            OUT("\"", 1);
            if (inst_ref->entry) {
                OUT(inst_ref->entry->name, inst_ref->entry->name_size);
            } else {
                OUT(inst_ref->name, inst_ref->name_size);
            }
            OUT("\"", 1);
            OUT("}", 1);
            inst_ref_count++;
        }
        OUT("]", 1);
    }
    
    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, (depth) * indent_size);
        RET_ERR();
    }
    OUT("}", 1);

    task->batch_size++;
    return knd_OK;
}

static int export_concise_JSON(struct kndClass *self, struct kndTask *task)
{
    struct kndClassVar *item;
    int err;

    if (DEBUG_JSON_LEVEL_2)
        knd_log(".. export concise JSON for %.*s..",
                self->entry->name_size, self->entry->name);

    FOREACH (item, self->baseclass_vars) {
        if (!item->attrs) continue;
        err = knd_attr_vars_export_JSON(item->attrs, task, true, 0);
        KND_TASK_ERR("failed to export attr vars JSON");
    }
    if (DEBUG_JSON_LEVEL_2)
        knd_log(".. export inherited attrs of %.*s..",
                self->entry->name_size, self->entry->name);

    /* inherited attrs */
    //err = self->attr_idx->map(self->attr_idx,
    //                          knd_export_inherited_attr, (void*)task); 
    //if (err && err != knd_RANGE) return err;

    return knd_OK;
}

extern int knd_empty_set_export_JSON(struct kndClass *unused_var(self),
                                     struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;
    out->reset(out);
    err = out->write(out, "{}", strlen("{}"));                                    RET_ERR();
    return knd_OK;
}

static int export_facet(struct kndClassFacet *parent_facet, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndClassFacet *facet;
    struct kndClassRef *ref;
    bool in_list = false;
    int err;

    err = out->write(out, "{\"_name\":",
                     strlen("{\"_name\":"));                                       RET_ERR();
    err = out->writec(out, '"');                                                  RET_ERR();
    err = out->write(out, parent_facet->base->name,
                     parent_facet->base->name_size);                               RET_ERR();
    err = out->writec(out, '"');                                                  RET_ERR();

    // err = export_gloss_JSON(parent_facet->base->class, task);                     RET_ERR();

    err = export_concise_JSON(parent_facet->base->class, task);                   RET_ERR();
    
    err = out->writef(out, ",\"_total\":%zu",
                      parent_facet->num_elems);                RET_ERR();

    if (parent_facet->children || parent_facet->elems) {
        in_list = false;
        err = out->write(out, ",\"_subclasses\":[",
                         strlen(",\"_subclasses\":["));                            RET_ERR();

        for (facet = parent_facet->children; facet; facet = facet->next) {
            if (in_list) {
                err = out->writec(out, ',');                                      RET_ERR();
            }
            err = export_facet(facet, task);                                      RET_ERR();
            in_list = true;
        }

        for (ref = parent_facet->elems; ref; ref = ref->next) {
            task->depth = 0;
            task->ctx->max_depth = 0;
            if (in_list) {
                err = out->writec(out, ',');                                      RET_ERR();
            }
            err = knd_class_export_JSON(ref->entry->class, task, false, 0);
            RET_ERR();
            in_list = true;
        }
        err = out->writec(out, ']');                                              RET_ERR();
    } 

    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}

int knd_class_facets_export_JSON(struct kndTask *task)
{
    struct kndClassFacet *facet;
    struct kndClassFacet *facets = NULL;
    struct kndOutput *out = task->out;
    bool in_list = false;
    int err;

    err = out->write(out, "{\"_facets\":[",
                     strlen("{\"_facets\":["));                                   RET_ERR();

    FOREACH (facet, facets) {
        if (in_list) {
            err = out->writec(out, ',');                                          RET_ERR();
        }
        err = export_facet(facet, task);                                          RET_ERR();
        in_list = true;
    }

    err = out->writec(out, ']');                                                  RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}

extern int knd_class_set_export_JSON(struct kndSet *set, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    size_t curr_depth = 0;
    int err;
    err = out->write(out, "{\"_set\":{",
                     strlen("{\"_set\":{"));                                      RET_ERR();

    /* TODO: present child clauses */
    if (set->base) {
        err = out->write(out, "\"is\":\"",
                         strlen("\"is\":\""));                                   RET_ERR();
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

    curr_depth = task->ctx->max_depth;
    task->ctx->max_depth = 1;

    // err = set->map(set, export_class_set_elem, (void*)task);
    // if (err && err != knd_RANGE) return err;
    task->ctx->max_depth = curr_depth;

    err = out->writec(out, ']');                                                  RET_ERR();

    /*if (set->facets) {
        err = export_facets(set, task);                                           RET_ERR();
        }*/
    
    err = out->writef(out, ",\"batch_max\":%lu",
                      (unsigned long)task->batch_max);                            RET_ERR();
    err = out->writef(out, ",\"batch_size\":%lu",
                       (unsigned long)task->batch_size);                          RET_ERR();

    if (task->batch_from) {
        err = out->write(out,
                         ",\"from_batch\":", strlen(",\"from_batch\":"));         RET_ERR();
        err = out->writef(out,"%lu",
                          (unsigned long)task->batch_from);                       RET_ERR();
    } else {
        err = out->write(out,
                         ",\"from\":", strlen(",\"from\":"));             RET_ERR();

        err = out->writef(out,"%lu",
                          (unsigned long)task->start_from);                           RET_ERR();
    }
    err = out->writec(out, '}');                                                  RET_ERR();
    return knd_OK;
}

static int present_subclass(struct kndClassRef *ref, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndClassEntry *entry = ref->entry;
    struct kndClass *c;
    size_t indent_size = task->ctx->format_indent;
    int err;

    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, depth * indent_size);
        RET_ERR();
    }

    OUT("{", 1);
    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, (depth + 1) * indent_size);
        RET_ERR();
    }
    OUT("\"name\":", strlen("\"name\":"));
    if (indent_size) {
        OUT(" ", 1);
    }
    OUT("\"", 1);
    OUT(entry->name, entry->name_size);
    OUT("\"", 1);

    /*if (ref->entry->class->num_terminals) {
        err = out->write(out, ",\"_num_terminals\":",
                         strlen(",\"_num_terminals\":"));                         RET_ERR();
        err = out->writef(out, "%zu", ref->entry->class->num_terminals);                      RET_ERR();
        }*/

    /* get localized gloss */
    err = knd_class_acquire(entry, &c, task);
    KND_TASK_ERR("failed to acquire a subclass %.*s", entry->name_size, entry->name);
    if (c->tr) {
        err = knd_text_gloss_export_JSON(c->tr, task, depth + 1);
        KND_TASK_ERR("failed to export subclass gloss JSON");
    }
    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, depth * indent_size);
        RET_ERR();
    }
    OUT("}", 1);
    return knd_OK;
}

static int present_subclasses(struct kndClass *self, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndClassRef *ref;
    bool in_list = false;
    size_t indent_size = task->ctx->format_indent;
    int err;

    OUT(",", 1);
    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, depth * indent_size);
        RET_ERR();
    }
    OUT("\"num-subclasses\":", strlen("\"num-subclasses\":"));
    if (indent_size) {
        OUT(" ", 1);
    }
    OUTF("%zu", self->num_children);
    OUT(",", 1);

    /*if (self->num_terminals) {
        OUT("\"num-terminals\":", strlen("\"num-terminals\":"));
        OUT("%zu,", self->num_terminals);
        }*/

    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, depth * indent_size);
        RET_ERR();
    }
  
    OUT("\"subclasses\":", strlen("\"subclasses\":"));
    if (indent_size) {
        OUT(" ", 1);
    }
    OUT("[", 1);

    FOREACH (ref, self->children) {
        if (in_list) {
            OUT(",", 1);
        }
        err = present_subclass(ref, task, depth + 1);
        KND_TASK_ERR("failed to present subclass JSON");
        in_list = true;
    }
    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, depth * indent_size);
        RET_ERR();
    }
    OUT("]", 1);    
    return knd_OK;
}

static int export_attrs(struct kndClass *self, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndAttr *attr;
    size_t count = 0;
    int err;
    OUT(",\"attrs\":{", strlen(",\"attrs\":{"));
    FOREACH (attr, self->attrs) {
        if (count) {
            OUT(",", 1);
        }
        err = knd_attr_export(attr, KND_FORMAT_JSON, task);
        KND_TASK_ERR("failed to export %.*s attr", attr->name_size, attr->name);
        count++;
    }
    OUT("}", 1);
    return knd_OK;
}

static int export_inverse_rels(struct kndClass *self, struct kndTask *task, size_t depth)
{
    struct kndAttrHub *attr_hub;
    struct kndAttr *attr;
    struct kndOutput *out = task->out;
    bool in_list = false;
    size_t curr_depth = 0;
    size_t indent_size = task->ctx->format_indent;
    int err;

    OUT(",", 1);
    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, (depth) * indent_size);
        RET_ERR();
    }

    OUT("\"rels\":", strlen("\"rels\":"));
    if (indent_size) {
        OUT(" ", 1);
    }
    OUT("[", 1);
    
    FOREACH (attr_hub, self->attr_hubs) {
        if (in_list) {
            OUT(",", 1);
        }
        if (!attr_hub->attr) {
            err = knd_attr_hub_resolve(attr_hub, task);
            KND_TASK_ERR("failed to resolve attr hub");
        }
        attr = attr_hub->attr;
        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth + 1) * indent_size);
            RET_ERR();
        }

        OUT("{", 1);
        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth + 2) * indent_size);
            RET_ERR();
        }
        OUT("\"class\":", strlen("\"class\":"));
        if (indent_size) {
            OUT(" ", 1);
        }
        OUT("\"", 1);
        OUT(attr->parent->name, attr->parent->name_size);
        OUT("\"", 1);

        OUT(",", 1);
        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth + 2) * indent_size);
            RET_ERR();
        }
        OUT("\"attr\":", strlen("\"attr\":"));
        if (indent_size) {
            OUT(" ", 1);
        }
        OUT("\"", 1);
        OUT(attr->name, attr->name_size);
        OUT("\"", 1);
        
        if (attr_hub->topics) {
            OUT(",", 1);
            if (indent_size) {
                OUT("\n", 1);
                err = knd_print_offset(out, (depth + 2) * indent_size);
                RET_ERR();
            }
            OUT("\"total\":", strlen("\"total\":"));
            if (indent_size) {
                OUT(" ", 1);
            }
            OUTF("%zu", attr_hub->topics->num_valid_elems);
        
            curr_depth = task->ctx->max_depth;
            task->ctx->max_depth = 0;
            task->depth = depth + 3;
            task->batch_size = 0;
            OUT(",", 1);
            if (indent_size) {
                OUT("\n", 1);
                err = knd_print_offset(out, (depth + 2) * indent_size);
                RET_ERR();
            }
            OUT("\"topics\":", strlen("\"topics\":"));
            if (indent_size) {
                OUT(" ", 1);
            }
            OUT("[", 1);
            err = knd_set_map(attr_hub->topics, export_class_ref, (void*)task);
            if (err && err != knd_RANGE) return err;

            if (indent_size) {
                OUT("\n", 1);
                err = knd_print_offset(out, (depth + 2) * indent_size);
                RET_ERR();
            }
            OUT("]", 1);
            task->ctx->max_depth = curr_depth;
        }

        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth + 1) * indent_size);
            RET_ERR();
        }
        OUT("}", 1);
        in_list = true;
    }
    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, (depth) * indent_size);
        RET_ERR();
    }
    OUT("]", 1);
    return knd_OK;
}

static int export_baseclasses(struct kndClass *self, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndClassVar *cvar;
    struct kndClass *c;
    size_t count = 0;
    size_t indent_size = task->ctx->format_indent;
    int err;

    if (DEBUG_JSON_LEVEL_2)
        knd_log(".. export baseclasses of %.*s", self->name_size, self->name);

    OUT(",", 1);
    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, depth * indent_size);
        RET_ERR();
    }

    OUT("\"is\":", strlen("\"is\":"));
    if (indent_size) {
        OUT(" ", 1);
    }
    OUT("[", 1);

    FOREACH (cvar, self->baseclass_vars) {
        if (count) {
            OUT(",", 1);
        }
        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth + 1) * indent_size);
            RET_ERR();
        }
        OUT("{", 1);
        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth + 2) * indent_size);
            RET_ERR();
        }
        OUT("\"name\":", strlen("\"name\":"));
        if (indent_size) {
            OUT(" ", 1);
        }
        OUT("\"", 1);
        OUT(cvar->entry->name, cvar->entry->name_size);
        OUT("\"", 1);

        /* get localized gloss */
        err = knd_class_acquire(cvar->entry, &c, task);
        KND_TASK_ERR("failed to acquire base class %.*s", cvar->entry->name_size, cvar->entry->name);
        if (c->tr) {
            err = knd_text_gloss_export_JSON(c->tr, task, depth + 2);
            KND_TASK_ERR("failed to export baseclass gloss JSON");
        }

        /* attr vars */
        if (cvar->attrs) {
            err = knd_attr_vars_export_JSON(cvar->attrs, task, false, depth + 2);
            KND_TASK_ERR("failed to export attr vars JSON");
        }

        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth + 1) * indent_size);
            RET_ERR();
        }
        OUT("}", 1);
        count++;
    }
    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, depth * indent_size);
        RET_ERR();
    }
    OUT("]", 1);
    return knd_OK;
}
                                     
int knd_class_export_JSON(struct kndClass *self, struct kndTask *task,
                          bool unused_var(is_list_item), size_t depth)
{
    struct kndClassEntry *entry = self->entry;
    struct kndClassEntry *orig_entry = entry->base;
    struct kndOutput *out = task->out;
    struct kndState *state = self->states;
    size_t indent_size = task->ctx->format_indent;
    int err;

    if (DEBUG_JSON_LEVEL_2)
        knd_log(".. JSON export: \"%.*s\" (repo:%.*s)  depth:%zu max depth:%zu",
                entry->name_size, entry->name, entry->repo->name_size, entry->repo->name,
                task->depth, task->ctx->max_depth);

    OUT("{", 1);
    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, (depth + 1) * indent_size);
        RET_ERR();
    }
    OUT("\"name\":", strlen("\"name\":"));
    if (indent_size) {
        OUT(" ", 1);
    }
    OUT("\"", 1);
    err = out->write_escaped(out, entry->name, entry->name_size);                 RET_ERR();
    OUT("\"", 1);

    if (self->tr) {
        err = knd_text_gloss_export_JSON(self->tr, task, depth + 1);
        KND_TASK_ERR("failed to export gloss JSON");
    }

    if (state) {
        err = out->write(out, ",\"_state\":", strlen(",\"_state\":"));            RET_ERR();
        err = out->writef(out, "%zu", state->numid);                              RET_ERR();

        if (state->commit) {
            err = out->write(out, ",\"_commit\":", strlen(",\"_commit\":"));      RET_ERR();
            err = out->writef(out, "%zu", state->commit->numid);                  RET_ERR();
        }

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

    if (task->depth > task->ctx->max_depth) {
        err = export_concise_JSON(self, task);                                    RET_ERR();
        goto final;
    }

    /* state info */
    if (self->num_states) {
        err = out->writec(out, ',');
        if (err) return err;
        err = knd_export_class_state_JSON(entry, task);                            RET_ERR();
    }

    /* display base classes only once */
    if (self->num_baseclass_vars) {
        err = export_baseclasses(self, task, depth + 1);
        KND_TASK_ERR("failed to export baseclass JSON");
    }
    /*else {
        if (orig_entry && orig_entry->class->num_baseclass_vars) {
            err = export_baseclass_vars(orig_entry->class, task);         RET_ERR();
        }
        }*/

    if (self->attrs) {
        err = export_attrs(self, task);
        KND_TASK_ERR("failed to export attrs JSON");
    } else {
        if (orig_entry && orig_entry->class->num_attrs) {
            err = export_attrs(orig_entry->class, task);                   RET_ERR();
        }
    }

    /* inherited attrs */
    //set = self->attr_idx;
    //err = set->map(set, knd_export_inherited_attr, (void*)task); 
    //if (err && err != knd_RANGE) return err;

    if (self->num_children) {
        err = present_subclasses(self, task, depth + 1);
        KND_TASK_ERR("failed to export subclasses in JSON");
    }

    /* instances */
    if (self->inst_idx) {
        OUT(",\"instances\":{", strlen(",\"instances\":{"));
        if (self->inst_states) {
            err = knd_export_class_inst_state_JSON(self, task);
            RET_ERR();
        }

        // TODO navigation facets?
        err = out->writec(out, '}');
        if (err) return err;
    }

    /* inverse relations */
    if (self->attr_hubs) {
        err = export_inverse_rels(self, task, depth + 1);
        KND_TASK_ERR("failed to export JSON inverse rels");
    }
    
 final:
    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, (depth) * indent_size);
        RET_ERR();
    }
    OUT("}", 1);
    return knd_OK;
}

