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

#define DEBUG_GSL_LEVEL_1 0
#define DEBUG_GSL_LEVEL_2 0
#define DEBUG_GSL_LEVEL_3 0
#define DEBUG_GSL_LEVEL_4 0
#define DEBUG_GSL_LEVEL_5 0
#define DEBUG_GSL_LEVEL_TMP 1

static gsl_err_t alloc_gloss_item(void *obj,
                                  const char *name,
                                  size_t name_size,
                                  size_t count,
                                  void **item)
{
    struct kndClass *self = obj;
    struct kndTranslation *tr;

    assert(name == NULL && name_size == 0);

    if (DEBUG_GSL_LEVEL_2)
        knd_log(".. %.*s: allocate gloss translation,  count: %zu",
                self->entry->name_size, self->entry->name, count);

    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return make_gsl_err_external(knd_NOMEM);

    memset(tr, 0, sizeof(struct kndTranslation));
    *item = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t append_gloss_item(void *accu,
                                   void *item)
{
    struct kndClass *self =   accu;
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

    if (DEBUG_GSL_LEVEL_2)
        knd_log(".. read gloss translation: \"%.*s\",  text: \"%.*s\"",
                tr->locale_size, tr->locale, tr->val_size, tr->val);

    return make_gsl_err(gsl_OK);
}

extern gsl_err_t knd_parse_gloss_array(void *obj,
                                       const char *rec,
                                       size_t *total_size)
{
    struct kndClass *self = obj;

    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .alloc = alloc_gloss_item,
        .append = append_gloss_item,
        .accu = self,
        .parse = parse_gloss_item
    };

    if (DEBUG_GSL_LEVEL_2)
        knd_log(".. %.*s: reading gloss",
                self->entry->name_size, self->entry->name);

    return gsl_parse_array(&item_spec, rec, total_size);
}

static gsl_err_t alloc_summary_item(void *obj,
                                    const char *name,
                                    size_t name_size,
                                    size_t count,
                                    void **item)
{
    struct kndClass *self = obj;
    struct kndTranslation *tr;

    assert(name == NULL && name_size == 0);

    if (DEBUG_GSL_LEVEL_2)
        knd_log(".. %.*s: allocate summary translation,  count: %zu",
                self->entry->name_size, self->entry->name, count);

    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return make_gsl_err_external(knd_NOMEM);

    memset(tr, 0, sizeof(struct kndTranslation));
    *item = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t append_summary_item(void *accu,
                                   void *item)
{
    struct kndClass *self =   accu;
    struct kndTranslation *tr = item;

    tr->next = self->summary;
    self->summary = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_summary_item(void *obj,
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

    if (DEBUG_GSL_LEVEL_2)
        knd_log(".. read summary translation: \"%.*s\",  text: \"%.*s\"",
                tr->locale_size, tr->locale, tr->val_size, tr->val);

    return make_gsl_err(gsl_OK);
}

extern gsl_err_t knd_parse_summary_array(void *obj,
                                         const char *rec,
                                         size_t *total_size)
{
    struct kndClass *self = obj;

    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .alloc = alloc_summary_item,
        .append = append_summary_item,
        .accu = self,
        .parse = parse_summary_item
    };

    if (DEBUG_GSL_LEVEL_2)
        knd_log(".. %.*s: reading summary",
                self->entry->name_size, self->entry->name);

    return gsl_parse_array(&item_spec, rec, total_size);
}

extern int knd_class_export_updates_GSL(struct kndClass *self,
                                        struct kndUpdate *update,
                                        struct glbOutput *out)
{
    struct kndState *state;
    int err;

    err = out->writec(out, '{');                                                  RET_ERR();
    err = out->write(out, self->name, self->name_size);                           RET_ERR();
    err = out->write(out, "{id ", strlen("{id "));                                RET_ERR();
    err = out->write(out, self->entry->id, self->entry->id_size);                 RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();
    
    if (self->states) {
        state = self->states;
        /*if (state->update == update) {
            err = knd_class_export_GSL(c, out);                          RET_ERR();
            }*/
    }
    
    err = out->writec(out, '}');                                          RET_ERR();
    return knd_OK;
}
