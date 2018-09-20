#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>
#include <glb-lib/output.h>

#include "knd_text.h"
#include "knd_task.h"
#include "knd_repo.h"
#include "knd_elem.h"
#include "knd_utils.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_attr.h"

#define DEBUG_TEXT_LEVEL_0 0
#define DEBUG_TEXT_LEVEL_1 0
#define DEBUG_TEXT_LEVEL_2 0
#define DEBUG_TEXT_LEVEL_3 0
#define DEBUG_TEXT_LEVEL_TMP 1

static void del(struct kndText *self)
{
    free(self);
}

static void str(struct kndText *self)
{
    //struct kndTranslation *tr;

    state = self->states;
    /*while (curr_state) {

        tr = curr_state->translations;
        while (tr) {

            if (tr->val_size)
                knd_log("%*s%s: %s [#%lu]", self->depth * KND_OFFSET_SIZE, "", tr->locale, tr->val,
                        tr->state, (unsigned long)tr->chunk_count);

            if (tr->seq_size)
                knd_log("%*s%s: %s [#%lu]", self->depth * KND_OFFSET_SIZE, "", tr->locale, tr->seq,
                        tr->state, (unsigned long)tr->chunk_count);

            sel = tr->selects;
            while (sel) {

                if (sel->css_name_size) {
                    knd_log("%*sCSS: \"%s\" @%lu+%lu\n", self->depth * KND_OFFSET_SIZE, "",
                            sel->css_name, (unsigned long)sel->pos, (unsigned long)sel->len);
                }

                sel = sel->next;
            }

            tr = tr->next;
        }
        curr_state = curr_state->next;
    }
    */
}

static int export_JSON(struct kndText *self   __attribute__((unused)),
                       struct glbOutput *out  __attribute__((unused)))
{
    //struct kndTranslation *tr;
    //struct kndTextSelect *sel;

    //int num_trs = 0;

    //int err = knd_FAIL;


    if (DEBUG_TEXT_LEVEL_2)
        knd_log(".. export text to JSON..");

    /*curr_state = self->states;

    if (curr_state->translations) {
        tr = curr_state->translations;

        while (tr) {
            if (num_trs) {
                err = out->write(out, ",", 1);
                if (err) return err;
            }

            err = out->write(out, "\"", 1);
            if (err) return err;

            err = out->write(out, tr->locale, tr->locale_size);
            if (err) return err;

            err = out->write(out, "\":{", strlen("\":{"));
            if (err) return err;

            if (tr->seq_size) {
                err = out->write(out,
                                 "\"t\":\"", strlen("\"t\":\""));
                if (err) return err;

                err = out->write(out, tr->seq, tr->seq_size);
                if (err) return err;

                err = out->write(out, "\"", 1);
                if (err) return err;
            }

            if (tr->selects) {
                err = out->write(out, ",\"sels\":[", strlen(",\"sels\":["));
                if (err) return err;

                sel = tr->selects;
                while (sel) {
                    err = out->write(out,  "{", 1);
                    if (err) return err;

                    if (sel->len > 1) {
                        buf_size = sprintf(buf, "\"p\":%lu,\"len\":%lu",
                                           (unsigned long)sel->pos,
                                           (unsigned long)sel->len);
                    }
                    else {
                        buf_size = sprintf(buf, "\"p\":%lu",
                                           (unsigned long)sel->pos);
                    }

                    err = out->write(out,
                                           buf, buf_size);
                    if (err) return err;


                    err = out->write(out,  "}", 1);
                    if (err) return err;

                    if (sel->next) {
                        err = out->write(out,  ",", 1);
                        if (err) return err;
                    }

                    sel = sel->next;
                }

                err = out->write(out,  "]", 1);
                if (err) return err;
            }

            err = out->write(out, "}", 1);
            if (err) return err;

            num_trs++;

            tr = tr->next;
        }

    }
*/
    return knd_OK;
}

static int export_GSP(struct kndText *self   __attribute__((unused)),
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

static int kndText_export(struct kndText *self,
                          knd_format format,
                          struct glbOutput *out)
{
    int err;

    switch (format) {
    case KND_FORMAT_JSON:
        err = export_JSON(self, out);
        if (err) return err;
        break;
    case KND_FORMAT_GSP:
        err = export_GSP(self, out);
        if (err) return err;
        break;
    default:
        break;
    }

    return knd_OK;
}

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

    memcpy(tr->val, val, val_size);
    tr->val[val_size] = '\0';
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

    memcpy(tr->curr_locale, name, name_size);
    tr->curr_locale[name_size] = '\0';
    tr->curr_locale_size = name_size;

    tr->locale = tr->curr_locale;
    tr->locale_size = tr->curr_locale_size;

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_GSL(struct kndText *self,
                     const char *rec,
                     size_t *total_size)
{
    struct kndTranslation *tr;
    gsl_err_t parser_err;

    state = self->states;
    /*if (!state) {
        state = malloc(sizeof(struct kndTextState));
        if (!state) return *total_size = 0, make_gsl_err_external(knd_NOMEM);
        memset(state, 0, sizeof(struct kndTextState));
        self->states = state;
        self->num_states++;
        }*/

    tr = malloc(sizeof(struct kndTranslation));
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

    /* assign translation */
    //tr->next = state->translations;
    //state->translations = tr;

    return make_gsl_err(gsl_OK);
}

extern int
kndText_new(struct kndText **text)
{
    struct kndText *self;

    self = malloc(sizeof(struct kndText));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndText));

    self->del = del;
    self->str = str;
    self->export = kndText_export;
    self->parse = parse_GSL;

    *text = self;

    return knd_OK;
}
