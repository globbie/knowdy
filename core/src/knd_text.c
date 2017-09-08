#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_text.h"
#include "knd_task.h"
#include "knd_repo.h"
#include "knd_output.h"
#include "knd_elem.h"
#include "knd_object.h"
#include "knd_objref.h"
#include "knd_utils.h"
#include "knd_concept.h"
#include "knd_attr.h"
#include "knd_parser.h"

#define DEBUG_TEXT_LEVEL_0 0
#define DEBUG_TEXT_LEVEL_1 0
#define DEBUG_TEXT_LEVEL_2 0
#define DEBUG_TEXT_LEVEL_3 0
#define DEBUG_TEXT_LEVEL_TMP 1

static int 
kndText_del(struct kndText *self)
{

    free(self);

    return knd_OK;
}

static int 
kndText_str(struct kndText *self, size_t depth)
{
    size_t offset_size = sizeof(char) * KND_OFFSET_SIZE * depth;
    char *offset = malloc(offset_size + 1);

    struct kndTextState *curr_state;
    struct kndTranslation *tr;
    struct kndTextSelect *sel;
  
    memset(offset, ' ', offset_size);
    offset[offset_size] = '\0';

    knd_log("%s%s:", offset,
            self->elem->attr->name);

    curr_state = self->states;
    while (curr_state) {
        /*if (curr_state->text_size) {
            knd_log("%s%s: %s [#%lu]\n", offset,
                    curr_state->locale, curr_state->text,
                    (unsigned long)curr_state->state);
                    }*/
        
        tr = curr_state->translations;
        while (tr) {

            if (tr->val_size) 
                knd_log("%s%s: %s [#%lu]", offset, tr->locale, tr->val,
                        tr->state, (unsigned long)tr->chunk_count);

            if (tr->seq_size)
                knd_log("%s%s: %s [#%lu]", offset, tr->locale, tr->seq,
                        tr->state, (unsigned long)tr->chunk_count);

            sel = tr->selects;
            while (sel) {

                if (sel->css_name_size) {
                    knd_log("%sCSS: \"%s\" @%lu+%lu\n", offset,
                            sel->css_name, (unsigned long)sel->pos, (unsigned long)sel->len);
                }
                
                if (sel->ref) {
                    knd_log("%sREF: \"%s\" @%lu+%lu\n", offset,
                            sel->ref->name, (unsigned long)sel->pos, (unsigned long)sel->len);
                }

                sel = sel->next;
            }

            tr = tr->next;
        }
        curr_state = curr_state->next;
    }
    
    return knd_OK;
}

static int 
kndText_index(struct kndText *self)
{
    char buf[KND_LARGE_BUF_SIZE];
    size_t buf_size;

    struct kndObject *obj;
    struct kndTextState *curr_state;
    struct kndTranslation *tr;
    int err = knd_FAIL;

    obj = self->elem->obj;
    curr_state = self->states;
    
    tr = curr_state->translations;
    while (tr) {
        if (tr->seq) {
            
            buf_size = sprintf(buf, ",\"t\": \"%s\"",
                               tr->seq);

            if (DEBUG_TEXT_LEVEL_3)
                knd_log("  .. indexing TEXT \"%s\"..\n",
                        tr->seq);

            /* TODO */
        }
            
        
        tr = tr->next;
    }
        
    return knd_OK;

    return err;
}


static int 
kndText_export_JSON(struct kndText *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;

    struct kndObject *obj;
    struct kndTextState *curr_state;
    struct kndTranslation *tr;
    struct kndTextSelect *sel;
    struct kndOutput *out;
    
    int num_trs = 0;

    int err = knd_FAIL;

    obj = self->elem->obj;
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

                    if (sel->ref) {
                        err = out->write(out,  ",\"ref\":\"", strlen(",\"ref\":\""));
                        if (err) return err;

                        /* TODO: expand */
                        /* get GUID */
                        /*err = obj->cache->repo->get_guid(obj->cache->repo,
                                                         obj->cache->baseclass,
                                                         sel->ref->name, sel->ref->name_size,
                                                         buf); */
                        err = knd_FAIL;
                        
                        if (err) {
                            err = out->write(out, "000", KND_ID_SIZE);
                            if (err) return err;
                        } else {
                            err = out->write(out, buf, KND_ID_SIZE);
                            if (err) return err;
                        }

                        err = out->write(out, ":", 1);
                        if (err) return err;
                        
                        err = out->write(out, sel->ref->name, sel->ref->name_size);
                        if (err) return err;
                        
                        err = out->write(out,  "\"", 1);
                        if (err) return err;
                    }
                    
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
            
        next_tr:
            
            tr = tr->next;
        }

    }

    return knd_OK;
}


static int 
kndText_export_HTML(struct kndText *self)
{
    //char buf[KND_NAME_SIZE];
    //size_t buf_size;

    struct kndObject *obj;
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

        next_tr:
            
            tr = tr->next;
        }


        return knd_OK;
    }

    return knd_OK;
}




static int 
kndText_export_GSC(struct kndText *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;

    struct kndObject *obj;
    struct kndTextState *curr_state;
    struct kndTranslation *tr;
    
    struct kndTextSelect *sel;
    size_t curr_size;
    
    int err = knd_FAIL;

    if (DEBUG_TEXT_LEVEL_2)
        knd_log(".. export text obj: %p  states: %p..", self->elem->obj, self->states);
  
    obj = self->elem->obj;
    
    // NB: expects self->states != NULL
    curr_state = self->states;
    if (!curr_state->translations) {
        knd_log("-- no translations found :(\n");
        return knd_FAIL;
    }

    tr = curr_state->translations;
            
    buf_size = sprintf(buf, "[tr ");
    err = self->out->write(self->out,  buf, buf_size);
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

                /* REF */
                if (sel->ref) {
                    err = self->out->write(self->out,
                                           "{ref ", strlen("{ref "));

                    err = self->out->write(self->out, sel->ref->name, sel->ref->name_size);
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



static int 
kndText_export(struct kndText *self,
               knd_format format)
{
    int err;
    
    switch(format) {
    case KND_FORMAT_JSON:
        err = kndText_export_JSON(self);
        if (err) return err;
        break;
    case KND_FORMAT_HTML:
        err = kndText_export_HTML(self);
        if (err) return err;
        break;
        /*case KND_FORMAT_GSL:
        err = kndText_export_GSL(self);
        if (err) return err;
        break;*/
    case KND_FORMAT_GSC:
        err = kndText_export_GSC(self);
        if (err) return err;
        break;
    default:
        break;
    }
    
    return knd_OK;
}

static int run_set_translation_text(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndTranslation *tr = (struct kndTranslation*)obj;
    struct kndTaskArg *arg;
    const char *val = NULL;
    size_t val_size = 0;
    int err;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "_impl", strlen("_impl"))) {
            val = arg->val;
            val_size = arg->val_size;
        }
    }

    if (!val_size) return knd_FAIL;
    if (val_size >= KND_NAME_SIZE) {
        /* alloc memory */
        if (val_size + 1 >= KND_MAX_TEXT_CHUNK_SIZE) {
            knd_log("-- max text limit reached: %lu :(", (unsigned long)val_size);
            return knd_LIMIT;
        }

        tr->seq = malloc(val_size + 1);
        if (!tr->seq) return knd_NOMEM;

        memcpy(tr->seq, val, val_size);
        tr->seq[val_size] = '\0';
        tr->seq_size = val_size;

        if (DEBUG_TEXT_LEVEL_2)
            knd_log("== TEXT CHUNK val: \"%s\"", tr->seq);
        return knd_OK;
    }

    if (DEBUG_TEXT_LEVEL_2)
        knd_log(".. set translation text \"%s\" => \"%s\"", tr->locale, val);

    memcpy(tr->val, val, val_size);
    tr->val[val_size] = '\0';
    tr->val_size = val_size;

    return knd_OK;
}



static int parse_translation_GSL(void *obj,
                                 const char *name, size_t name_size,
                                 const char *rec, size_t *total_size)
{
    struct kndTranslation *tr = (struct kndTranslation*)obj;
    int err;

    if (DEBUG_TEXT_LEVEL_3) {
        knd_log("..  translation in \"%s\" REC: \"%s\"\n",
                name, rec); }

    struct kndTaskSpec specs[] = {
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

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    return knd_OK;
}

static int parse_GSL(struct kndText *self,
                     const char *rec,
                     size_t *total_size)
{
    struct kndTextState *state;
    struct kndTranslation *tr;
    int err;
    
    state = self->states;
    if (!state) {
        state = malloc(sizeof(struct kndTextState));
        if (!state) return knd_NOMEM;
        memset(state, 0, sizeof(struct kndTextState));
        self->states = state;
        self->num_states++;
    }
    
    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return knd_NOMEM;
    memset(tr, 0, sizeof(struct kndTranslation));

    struct kndTaskSpec specs[] = {
        { .is_validator = true,
          .buf = tr->curr_locale,
          .buf_size = &tr->curr_locale_size,
          .max_buf_size = KND_LOCALE_SIZE,
          .validate = parse_translation_GSL,
          .obj = tr
        }
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    /* assign translation */
    tr->next = state->translations;
    state->translations = tr;

    return knd_OK;
}

extern int 
kndText_new(struct kndText **text)
{
    struct kndText *self;
    
    self = malloc(sizeof(struct kndText));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndText));

    self->del = kndText_del;
    self->str = kndText_str;
    self->export = kndText_export;
    self->parse = parse_GSL;
    self->index = kndText_index;

    *text = self;

    return knd_OK;
}
