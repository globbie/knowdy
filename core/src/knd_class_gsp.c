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
#include "knd_task.h"
#include "knd_user.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_http_codes.h"

#include <gsl-parser.h>
#include <glb-lib/output.h>

#define DEBUG_CLASS_GSP_LEVEL_1 0
#define DEBUG_CLASS_GSP_LEVEL_2 0
#define DEBUG_CLASS_GSP_LEVEL_3 0
#define DEBUG_CLASS_GSP_LEVEL_4 0
#define DEBUG_CLASS_GSP_LEVEL_5 0
#define DEBUG_CLASS_GSP_LEVEL_TMP 1

static int attr_vars_export_GSP(struct kndClass *self,
                                 struct kndAttrVar *items,
                                 size_t depth);

/*static gsl_err_t read_GSP(struct kndClass *self,
                          const char *rec,
                          size_t *total_size)
{
    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log(".. reading GSP: \"%.*s\"..", 256, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_name,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_g",
          .name_size = strlen("_g"),
          .parse = parse_gloss_array,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_summary",
          .name_size = strlen("_summary"),
          .parse = parse_summary_array,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_ci",
          .name_size = strlen("_ci"),
          .parse = parse_class_var_array,
          .obj = self
        },
        { .name = "aggr",
          .name_size = strlen("aggr"),
          .parse = parse_aggr,
          .obj = self
        },
        { .name = "str",
          .name_size = strlen("str"),
          .parse = parse_str,
          .obj = self
        },
        { .name = "bin",
          .name_size = strlen("bin"),
          .parse = parse_bin,
          .obj = self
        },
        { .name = "num",
          .name_size = strlen("num"),
          .parse = parse_num,
          .obj = self
        },
        { .name = "ref",
          .name_size = strlen("ref"),
          .parse = parse_ref,
          .obj = self
        },
        { .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc,
          .obj = self
        },
        { .name = "text",
          .name_size = strlen("text"),
          .parse = parse_text,
          .obj = self
        },
        { .name = "_desc",
          .name_size = strlen("_desc"),
          .parse = parse_descendants,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

*/

static int export_conc_id_GSP(void *obj,
                              const char *elem_id  __attribute__((unused)),
                              size_t elem_id_size  __attribute__((unused)),
                              size_t count __attribute__((unused)),
                              void *elem)
{
    struct glbOutput *out = obj;
    struct kndClassEntry *entry = elem;
    int err;
    err = out->writec(out, ' ');                                                  RET_ERR();
    err = out->write(out, entry->id, entry->id_size);                                 RET_ERR();
    return knd_OK;
}

static int export_facets_GSP(struct kndClass *self, struct kndSet *set)
{
    struct glbOutput *out = self->entry->repo->out;
    struct kndSet *subset;
    struct kndFacet *facet;
    struct ooDict *set_name_idx;
    const char *key;
    void *val;
    int err;

    err = out->write(out,  "[fc ", strlen("[fc "));                               RET_ERR();
    for (size_t i = 0; i < set->num_facets; i++) {
        facet = set->facets[i];
        err = out->write(out,  "{", 1);                                           RET_ERR();
        err = out->write(out, facet->attr->name, facet->attr->name_size);
        err = out->write(out,  " ", 1);                                           RET_ERR();

        if (facet->set_name_idx) {
            err = out->write(out,  "[set", strlen("[set"));                       RET_ERR();
            set_name_idx = facet->set_name_idx;
            key = NULL;
            set_name_idx->rewind(set_name_idx);
            do {
                set_name_idx->next_item(set_name_idx, &key, &val);
                if (!key) break;
                subset = (struct kndSet*)val;

                err = out->writec(out, '{');                                      RET_ERR();
                err = out->write(out, subset->base->id,
                                 subset->base->id_size);                          RET_ERR();

                err = out->write(out, "[c", strlen("[c"));                        RET_ERR();
                err = subset->map(subset, export_conc_id_GSP, (void*)out);
                if (err) return err;
                err = out->writec(out, ']');                                      RET_ERR();
                err = out->writec(out, '}');                                      RET_ERR();
            } while (key);
            err = out->write(out,  "]", 1);                                       RET_ERR();
        }
        err = out->write(out,  "}", 1);                                           RET_ERR();
    }
    err = out->write(out,  "]", 1);                                               RET_ERR();

    return knd_OK;
}

static int export_descendants_GSP(struct kndClass *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct glbOutput *out = self->entry->repo->out;
    struct kndSet *set;
    int err;

    set = self->entry->descendants;

    err = out->write(out, "{_desc", strlen("{_desc"));                            RET_ERR();
    buf_size = sprintf(buf, "{tot %zu}", set->num_elems);
    err = out->write(out, buf, buf_size);                                         RET_ERR();

    err = out->write(out, "[c", strlen("[c"));                                    RET_ERR();
    err = set->map(set, export_conc_id_GSP, (void*)out);
    if (err) return err;
    err = out->writec(out, ']');                                                  RET_ERR();

    if (set->num_facets) {
        err = export_facets_GSP(self, set);                                       RET_ERR();
    }

    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}

static int ref_list_export_GSP(struct kndClass *self,
                               struct kndAttrVar *parent_item)
{
    struct kndAttrVar *item;
    struct glbOutput *out;
    struct kndClass *c;
    int err;

    out = self->entry->repo->out;

    err = out->writec(out, '{');
    if (err) return err;
    err = out->write(out, parent_item->name, parent_item->name_size);
    if (err) return err;

    err = out->write(out, "[r", strlen("[r"));
    if (err) return err;

    /* first elem */
    if (parent_item->val_size) {
        c = parent_item->class;
        if (c) {
            err = out->writec(out, ' ');
            if (err) return err;
            err = out->write(out, c->entry->id, c->entry->id_size);
            if (err) return err;
        }
    }

    for (item = parent_item->list; item; item = item->next) {
        c = item->class;
        if (!c) continue;

        err = out->writec(out, ' ');
        if (err) return err;
        err = out->write(out, c->entry->id, c->entry->id_size);
        if (err) return err;
    }
    err = out->writec(out, ']');
    if (err) return err;

    err = out->writec(out, '}');
    if (err) return err;

    return knd_OK;
}

static int aggr_item_export_GSP(struct kndClass *self,
                                struct kndAttrVar *parent_item)
{
    struct glbOutput *out = self->entry->repo->out;
    struct kndClass *c = parent_item->class;
    struct kndAttrVar *item;
    struct kndAttr *attr;
    int err;

    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log("== GSP export of aggr item: %.*s val:%.*s",
                parent_item->name_size, parent_item->name,
                parent_item->val_size, parent_item->val);

    err = out->writec(out, '{');
    if (err) return err;

    if (c) {
        err = out->write(out, c->entry->id, c->entry->id_size);
        if (err) return err;
    } else {

        /* terminal value */
        if (parent_item->val_size) {
            c = parent_item->attr->conc;
            if (c && c->implied_attr) {
                attr = c->implied_attr;
                err = out->writec(out, ' ');
                if (err) return err;
                err = out->write(out,
                                 parent_item->val,
                                 parent_item->val_size);
                if (err) return err;
            } else {
                err = out->writec(out, ' ');
                if (err) return err;
                err = out->write(out,
                                 parent_item->val,
                                 parent_item->val_size);
                if (err) return err;
            }
        } else {
            err = out->write(out,
                             parent_item->id,
                             parent_item->id_size);
            if (err) return err;
        }
    }

    c = parent_item->attr->conc;

    for (item = parent_item->children; item; item = item->next) {
        err = out->writec(out, '{');
        if (err) return err;

        err = out->write(out, item->name, item->name_size);
        if (err) return err;

        switch (item->attr->type) {
        case KND_ATTR_REF:
            //knd_log("ref item: %.*s", item->name_size, item->name);
            break;
        case KND_ATTR_AGGR:
            err = aggr_item_export_GSP(self, item);
            if (err) return err;
            break;
        default:
            err = out->writec(out, ' ');
            if (err) return err;
            err = out->write(out, item->val, item->val_size);
            if (err) return err;
            break;
        }

        err = out->writec(out, '}');
        if (err) return err;
    }

    //if (parent_item->class) {
    err = out->writec(out, '}');
    if (err) return err;

    return knd_OK;
}

static int aggr_list_export_GSP(struct kndClass *self,
                                struct kndAttrVar *parent_item)
{
    struct kndAttrVar *item;
    struct glbOutput *out = self->entry->repo->out;
    struct kndClass *c;
    int err;

    if (DEBUG_CLASS_GSP_LEVEL_1) {
        knd_log(".. export aggr list: %.*s  val:%.*s",
                parent_item->name_size, parent_item->name,
                parent_item->val_size, parent_item->val);
    }

    err = out->writec(out, '[');
    if (err) return err;
    err = out->write(out, parent_item->name, parent_item->name_size);
    if (err) return err;

    /* first elem */
    /*if (parent_item->class) {
        c = parent_item->class;
        if (c) { */
    if (parent_item->class) {
        err = aggr_item_export_GSP(self, parent_item);
        if (err) return err;
    }

    for (item = parent_item->list; item; item = item->next) {
        c = item->class;

        err = aggr_item_export_GSP(self, item);
        if (err) return err;
    }

    err = out->writec(out, ']');
    if (err) return err;

    return knd_OK;
}

static int attr_vars_export_GSP(struct kndClass *self,
                                 struct kndAttrVar *items,
                                 size_t depth  __attribute__((unused)))
{
    struct kndAttrVar *item;
    struct glbOutput *out;
    int err;

    out = self->entry->repo->out;

    for (item = items; item; item = item->next) {

        if (item->attr && item->attr->is_a_set) {
            switch (item->attr->type) {
            case KND_ATTR_AGGR:
                err = aggr_list_export_GSP(self, item);
                if (err) return err;
                break;
            case KND_ATTR_REF:
                err = ref_list_export_GSP(self, item);
                if (err) return err;
                break;
            default:
                break;
            }
            continue;
        }

        err = out->write(out, "{", 1);
        if (err) return err;
        err = out->write(out, item->name, item->name_size);
        if (err) return err;

        if (item->val_size) {
            err = out->write(out, " ", 1);
            if (err) return err;
            err = out->write(out, item->val, item->val_size);
            if (err) return err;
        }

        if (item->children) {
            err = attr_vars_export_GSP(self, item->children, 0);
            if (err) return err;
        }
        err = out->write(out, "}", 1);
        if (err) return err;
    }
    return knd_OK;
}

static int export_class_body_updates(struct kndClass *self,
                                     struct kndUpdate *update,
                                     struct glbOutput *out)
{
    int err;
    return knd_OK;
}

static int export_class_inst_updates(struct kndClass *self,
                                     struct kndUpdate *update,
                                     struct glbOutput *out)
{
    int err;
    return knd_OK;
}

extern int knd_class_export_updates_GSP(struct kndClass *self,
                                        struct kndUpdate *update,
                                        struct glbOutput *out)
{
    struct kndState *state;
    int err;

    err = out->writec(out, '{');                                                  RET_ERR();
    err = out->write(out, self->entry->id, self->entry->id_size);                 RET_ERR();
    err = out->write(out, "{_n ", strlen("{_n "));                                RET_ERR();
    err = out->write(out, self->name, self->name_size);                           RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();

    if (self->states) {
        state = self->states;
        /* any updates of the class body? */
        if (state->update == update) {
            err = export_class_body_updates(self, update, out);                   RET_ERR();
        }
    }

    if (self->inst_states) {
        state = self->inst_states;
        /* any updates of the class insts? */
        if (state->update == update) {
            err = export_class_inst_updates(self, update, out);                   RET_ERR();
        }
    }
    err = out->writec(out, '}');                                                  RET_ERR();
    return knd_OK;
}

extern int knd_class_export_GSP(struct kndClass *self,
                                struct glbOutput *out)
{
    struct kndAttr *attr;
    struct kndClassVar *item;
    struct kndTranslation *tr;
    int err;

    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log(".. GSP export of \"%.*s\" [%.*s]",
                self->entry->name_size, self->entry->name,
                self->entry->id_size, self->entry->id);

    err = out->writec(out, '{');
    if (err) return err;
    err = out->write(out, self->entry->name, self->entry->name_size);
    if (err) return err;

    err = out->write(out, "{id ", strlen("{id "));
    if (err) return err;
    err = out->write(out, self->entry->id, self->entry->id_size);
    if (err) return err;
    err = out->writec(out, '}');
    if (err) return err;

    if (self->tr) {
        err = out->write(out, "[_g", strlen("[_g"));
        if (err) return err;

        for (tr = self->tr; tr; tr = tr->next) {
            err = out->write(out, "{", 1);
            if (err) return err;
            err = out->write(out, tr->locale, tr->locale_size);
            if (err) return err;
            err = out->write(out, "{t ", 3);
            if (err) return err;
            err = out->write(out, tr->val, tr->val_size);
            if (err) return err;
            err = out->write(out, "}}", 2);
            if (err) return err;
        }
        err = out->write(out, "]", 1);
        if (err) return err;
    }

    if (self->summary) {
        err = out->write(out, "[_summary", strlen("[_summary"));
        if (err) return err;

        for (tr = self->tr; tr; tr = tr->next) {
            err = out->write(out, "{", 1);
            if (err) return err;
            err = out->write(out, tr->locale, tr->locale_size);
            if (err) return err;
            err = out->write(out, "{t ", 3);
            if (err) return err;
            err = out->write(out, tr->val, tr->val_size);
            if (err) return err;
            err = out->write(out, "}}", 2);
            if (err) return err;
        }
        err = out->write(out, "]", 1);
        if (err) return err;
    }

    if (self->baseclass_vars) {
        err = out->write(out, "[_ci", strlen("[_ci"));
        if (err) return err;

        for (item = self->baseclass_vars; item; item = item->next) {
            err = out->writec(out, '{');
            if (err) return err;
            err = out->write(out, item->entry->id, item->entry->id_size);
            if (err) return err;

            if (item->attrs) {
              err = attr_vars_export_GSP(self, item->attrs, 0);
              if (err) return err;
            }
            err = out->writec(out, '}');
            if (err) return err;

        }
        err = out->writec(out, ']');
        if (err) return err;
    }

    if (self->attrs) {
        for (attr = self->attrs; attr; attr = attr->next) {
            err = attr->export(attr, KND_FORMAT_GSP, self->entry->repo->out);
            if (err) return err;
        }
    }

    if (self->entry->descendants) {
        err = export_descendants_GSP(self);                                      RET_ERR();
    }

    err = out->writec(out, '}');
    if (err) return err;

    return knd_OK;
}

