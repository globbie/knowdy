#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>
#include <glb-lib/output.h>

#include "knd_text.h"
#include "knd_task.h"
#include "knd_repo.h"
#include "knd_utils.h"
#include "knd_mempool.h"

#define DEBUG_TEXT_LEVEL_0 0
#define DEBUG_TEXT_LEVEL_1 0
#define DEBUG_TEXT_LEVEL_2 0
#define DEBUG_TEXT_LEVEL_3 0
#define DEBUG_TEXT_LEVEL_TMP 1

static int export_JSON(struct kndText *self,
                       struct kndTask *task,
                       struct glbOutput *out)
{
    struct kndTranslation *tr;
    int err;

    if (DEBUG_TEXT_LEVEL_TMP)
        knd_log(".. export text to JSON..");

    for (tr = self->tr; tr; tr = tr->next) {
        if (memcmp(task->locale, tr->locale, tr->locale_size)) {
            continue;
        }
        err = out->writec(out, '"');                                              RET_ERR();
        err = out->write_escaped(out, tr->val,  tr->val_size);                    RET_ERR();
        err = out->writec(out, '"');                                              RET_ERR();
        break;
    }
    return knd_OK;
}

static int export_GSP(struct kndText *self   __attribute__((unused)),
                      struct kndTask *task,
                      struct glbOutput *out   __attribute__((unused)))
{
    //struct kndState *state;
    //struct kndTranslation *tr;

    //struct kndTextSelect *sel;

    //int err = knd_FAIL;

    // NB: expects self->states != NULL
    /*curr_state = self->states;
    if (!curr_state->translations) {
        knd_log("-- no translations found :(\n");
        return knd_FAIL;
    }

    tr = curr_state->translations;

    err = out->write(out,  "[tr ", strlen("[tr "));
    if (err) return err;

    while (tr) {
        err = out->write(out,  "{", 1);
        if (err) return err;

        err = out->write(out, "{l ", strlen("{l "));
        if (err) return err;
        err = out->write(out,  tr->locale, tr->locale_size);
        if (err) return err;
        err = out->write(out,  "}", 1);
        if (err) return err;

        if (tr->seq) {
            err = out->write(out, "{t ", strlen("{t "));
            if (err) return err;

            err = out->write(out,  tr->seq, tr->seq_size);
            if (err) return err;

            err = out->write(out,  "}", 1);
            if (err) return err;
        }

        if (tr->selects) {
            err = out->write(out, "[", 1);
            if (err) return err;

            sel = tr->selects;
            curr_size = 0;

            while (sel) {
                err = out->write(out,  "{", 1);
                if (err) return err;

                if (sel->len > 1) {
                    buf_size = sprintf(buf, "{p %lu+%lu}",
                                       (unsigned long)sel->pos,
                                       (unsigned long)sel->len);
                }
                else
                    buf_size = sprintf(buf, "{p %lu}",
                                       (unsigned long)sel->pos);

                err = out->write(out, buf, buf_size);
                if (err) return err;


                if (sel->css_name_size) {
                    err = out->write(out,
                                           "{hi ", strlen("{hi "));

                    err = out->write(out, sel->css_name, sel->css_name_size);
                    if (err) return err;

                    err = out->write(out,  "}", 1);
                    if (err) return err;
                }


                err = out->write(out,  "}", 1);
                if (err) return err;

                sel = sel->next;
            }
            err = out->write(out, "]", 1);
            if (err) return err;
        }

        err = out->write(out,  "}", 1);
        if (err) return err;

        tr = tr->next;
    }

    err = out->write(out,  "]", 1);
    if (err) return err;
*/
    if (DEBUG_TEXT_LEVEL_2)
        knd_log("++ text export OK!");

    return knd_OK;
}

extern int knd_text_export(struct kndText *self,
                           knd_format format,
                           struct kndTask *task,
                           struct glbOutput *out)
{
    int err;

    switch (format) {
    case KND_FORMAT_JSON:
        err = export_JSON(self, task, out);
        if (err) return err;
        break;
    case KND_FORMAT_GSP:
        err = export_GSP(self, task, out);
        if (err) return err;
        break;
    default:
        break;
    }

    return knd_OK;
}

#if 0
static gsl_err_t run_set_translation_text(void *obj, const char *val, size_t val_size)
{
    struct kndTranslation *tr = obj;

    if (!val_size) return make_gsl_err(gsl_FORMAT);
    if (val_size >= KND_NAME_SIZE) {
        /* alloc memory */
        if (val_size + 1 >= KND_MAX_TEXT_CHUNK_SIZE) {
            knd_log("-- max text limit reached: %lu :(", (unsigned long)val_size);
            return make_gsl_err(gsl_LIMIT);
        }

        /*tr->seq = malloc(val_size + 1);
        if (!tr->seq) return knd_NOMEM;

        memcpy(tr->seq, val, val_size);
        tr->seq[val_size] = '\0';
        tr->seq_size = val_size;
        */
        if (DEBUG_TEXT_LEVEL_2)
            knd_log("== TEXT CHUNK val: \"%s\"", tr->seq);
        return make_gsl_err(gsl_OK);
    }

    if (DEBUG_TEXT_LEVEL_2)
        knd_log(".. set translation text \"%s\" => \"%s\"", tr->locale, val);

    tr->val = val;
    tr->val_size = val_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_translation_GSL(void *obj,
                                       const char *name, size_t name_size,
                                       const char *rec, size_t *total_size)
{
    struct kndTranslation *tr = obj;

    if (DEBUG_TEXT_LEVEL_3) {
        knd_log("..  translation in \"%s\" REC: \"%s\"\n",
                name, rec); }

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_translation_text,
          .obj = tr
        }
    };

    tr->curr_locale = name;
    tr->curr_locale_size = name_size;

    tr->locale = tr->curr_locale;
    tr->locale_size = tr->curr_locale_size;

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_GSL(struct kndText *self __attribute__((unused)),
                           const char *rec,
                           size_t *total_size)
{
    struct kndTranslation *tr;
    gsl_err_t parser_err;

    /*  tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return *total_size = 0, make_gsl_err_external(knd_NOMEM);
    memset(tr, 0, sizeof(struct kndTranslation));

    struct gslTaskSpec specs[] = {
        { .is_validator = true,
          .validate = parse_translation_GSL,
          .obj = tr
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;
    */

    /* assign translation */
    //tr->next = state->translations;
    //state->translations = tr;

    return make_gsl_err(gsl_OK);
}
#endif

extern int knd_text_translation_new(struct kndMemPool *mempool,
                                    struct kndTranslation **result)
{
    void *page;
    int err;

    //knd_log(".. new text translation [size:%zu]", sizeof(struct kndTranslation));

    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                            sizeof(struct kndTranslation), &page);
    if (err) return err;
    *result = page;
    //kndTranslation_init(*result);
    return knd_OK;
}

extern int knd_text_new(struct kndMemPool *mempool,
                        struct kndText **result)
{
    void *page;
    int err;
    //knd_log(".. new text [size:%zu]", sizeof(struct kndText));

    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                            sizeof(struct kndText), &page);
    if (err) return err;
    *result = page;
    //kndText_init(*result);
    return knd_OK;
}
