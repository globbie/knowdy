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
    struct kndTextState *curr_state;
    struct kndTranslation *tr;
    struct kndTextSelect *sel;

    if (self->elem) {
        knd_log("%*s%s:", self->depth * KND_OFFSET_SIZE, "",
                self->elem->attr->name);
    }

    curr_state = self->states;
    while (curr_state) {
        /*if (curr_state->text_size) {
            knd_log("%*s%s: %s [#%lu]\n", offset,
                    curr_state->locale, curr_state->text,
                    (unsigned long)curr_state->state);
                    }*/

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

}



static int export_JSON(struct kndText *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;

    struct kndTextState *curr_state;
    struct kndTranslation *tr;
    struct kndTextSelect *sel;
    struct glbOutput *out;

    int num_trs = 0;

    int err = knd_FAIL;

    out = self->out;

    if (DEBUG_TEXT_LEVEL_2)
        knd_log(".. export text to JSON..");

    curr_state = self->states;

    if (curr_state->translations) {
        tr = curr_state->translations;

        while (tr) {
            /* check language */
            /*if (obj->cache->repo->locale_size) {
                if (DEBUG_TEXT_LEVEL_3)
                    knd_log("  .. text LANG: %s curr user lang: %s\n",
                            tr->locale, obj->cache->repo->locale);

                if (strncmp(tr->locale, obj->cache->repo->locale, tr->locale_size))
                    goto next_tr;
            }
            */
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

                    /* selection POS */
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

    return knd_OK;
}


static int export_HTML(struct kndText *self)
{
    //char buf[KND_NAME_SIZE];
    //size_t buf_size;

    struct kndClassInst *obj;
    struct kndTextState *curr_state;
    struct kndTranslation *tr;
    //struct kndTextSelect *sel;

    //size_t curr_size;
    //char *c;

    int err = knd_FAIL;

    obj = self->elem->obj;

    curr_state = self->states;

    if (curr_state->translations) {
        tr = curr_state->translations;

        while (tr) {
            /* check language */
            /*if (obj->cache->repo->locale_size) {

                if (DEBUG_TEXT_LEVEL_3)
                    knd_log("  .. text LANG: %s curr user lang: %s\n",
                            tr->locale, obj->cache->repo->locale);

                if (strcmp(tr->locale, obj->cache->repo->locale))
                    goto next_tr;
                    }*/

            /*buf_size = sprintf(buf, "\"l\":\"%s\"",
                               tr->locale);
            err = self->out->write(self->out,  buf, buf_size);
            if (err) return err;
            */

            if (tr->seq) {
                /*err = self->out->write(self->out,
                                       "<P>", strlen("<P>"));
                if (err) return err;
                */

                err = self->out->write(self->out, tr->seq, tr->seq_size);
                if (err) return err;

                /*err = self->out->write(self->out, "</P>", strlen("</P>"));
                if (err) return err;
                */

            }

            /*if (tr->selects) {
                err = self->out->write(self->out, "<UL>\n", strlen("<UL>\n"));
                if (err) return err;

                sel = tr->selects;
                while (sel) {
                    err = self->out->write(self->out,  "<LI>", strlen("<LI>"));
                    if (err) return err;

                    buf_size = sprintf(buf, "\"p\":%lu",
                                       (unsigned long)sel->pos);
                    err = self->out->write(self->out,
                                           buf, buf_size);
                    if (err) return err;

                    if (sel->ref) {
                        err = self->out->write(self->out,  ",\"ref\":\"", strlen(",\"ref\":\""));
                        if (err) return err;

                        err = self->out->write(self->out, sel->ref->name, sel->ref->name_size);
                        if (err) return err;

                        err = self->out->write(self->out,  "\"", 1);
                        if (err) return err;
                    }

                    err = self->out->write(self->out,  "</LI>", strlen("</LI>"));
                    if (err) return err;

                    if (sel->next) {
                        err = self->out->write(self->out,  "\n", 1);
                        if (err) return err;
                    }

                    sel = sel->next;
                }

                err = self->out->write(self->out,  "</UL>\n", strlen("</UL>\n"));
                if (err) return err;
                }*/

            /*if (tr->next) {
              err = self->out->write(self->out,  ",", 1);
              if (err) goto final;
              }*/

            tr = tr->next;
        }


        return knd_OK;
    }

    return knd_OK;
}

static int export_GSP(struct kndText *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;

    struct kndTextState *curr_state;
    struct kndTranslation *tr;

    struct kndTextSelect *sel;
    size_t curr_size;

    int err = knd_FAIL;

    // NB: expects self->states != NULL
    curr_state = self->states;
    if (!curr_state->translations) {
        knd_log("-- no translations found :(\n");
        return knd_FAIL;
    }

    tr = curr_state->translations;

    err = self->out->write(self->out,  "[tr ", strlen("[tr "));
    if (err) return err;

    while (tr) {
        err = self->out->write(self->out,  "{", 1);
        if (err) return err;

        err = self->out->write(self->out, "{l ", strlen("{l "));
        if (err) return err;
        err = self->out->write(self->out,  tr->locale, tr->locale_size);
        if (err) return err;
        err = self->out->write(self->out,  "}", 1);
        if (err) return err;

        if (tr->seq) {
            err = self->out->write(self->out, "{t ", strlen("{t "));
            if (err) return err;

            err = self->out->write(self->out,  tr->seq, tr->seq_size);
            if (err) return err;

            err = self->out->write(self->out,  "}", 1);
            if (err) return err;
        }

        if (tr->selects) {
            err = self->out->write(self->out, "[", 1);
            if (err) return err;

            sel = tr->selects;
            curr_size = 0;

            while (sel) {
                err = self->out->write(self->out,  "{", 1);
                if (err) return err;

                /* selection POS */
                if (sel->len > 1) {
                    buf_size = sprintf(buf, "{p %lu+%lu}",
                                       (unsigned long)sel->pos,
                                       (unsigned long)sel->len);
                }
                else
                    buf_size = sprintf(buf, "{p %lu}",
                                       (unsigned long)sel->pos);

                err = self->out->write(self->out, buf, buf_size);
                if (err) return err;

                /* HILITE */
                if (sel->css_name_size) {
                    err = self->out->write(self->out,
                                           "{hi ", strlen("{hi "));

                    err = self->out->write(self->out, sel->css_name, sel->css_name_size);
                    if (err) return err;

                    err = self->out->write(self->out,  "}", 1);
                    if (err) return err;
                }


                err = self->out->write(self->out,  "}", 1);
                if (err) return err;

                sel = sel->next;
            }
            err = self->out->write(self->out, "]", 1);
            if (err) return err;
        }

        err = self->out->write(self->out,  "}", 1);
        if (err) return err;

        tr = tr->next;
    }

    err = self->out->write(self->out,  "]", 1);
    if (err) return err;

    if (DEBUG_TEXT_LEVEL_2)
        knd_log("++ text export OK!");

    return knd_OK;
}

static int export(struct kndText *self)
{
    int err;

    switch(self->format) {
    case KND_FORMAT_JSON:
        err = export_JSON(self);
        if (err) return err;
        break;
    case KND_FORMAT_HTML:
        err = export_HTML(self);
        if (err) return err;
        break;
    case KND_FORMAT_GSP:
        err = export_GSP(self);
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
    struct kndTextState *state;
    struct kndTranslation *tr;
    gsl_err_t parser_err;

    state = self->states;
    if (!state) {
        state = malloc(sizeof(struct kndTextState));
        if (!state) return *total_size = 0, make_gsl_err_external(knd_NOMEM);
        memset(state, 0, sizeof(struct kndTextState));
        self->states = state;
        self->num_states++;
    }

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
    tr->next = state->translations;
    state->translations = tr;

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
    self->export = export;
    self->parse = parse_GSL;

    *text = self;

    return knd_OK;
}
