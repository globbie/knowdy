#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_text.h"
#include "knd_repo.h"
#include "knd_output.h"
#include "knd_conc.h"
#include "knd_elem.h"
#include "knd_object.h"
#include "knd_objref.h"
#include "knd_utils.h"
#include "knd_dataclass.h"
#include "knd_attr.h"

#include "../../src/data_reader/knd_data_reader.h"

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
    //char buf[KND_TEMP_BUF_SIZE];
    //size_t buf_size;
    //size_t curr_size;
    //char *c;
    size_t offset_size = sizeof(char) * KND_OFFSET_SIZE * depth;
    char *offset = malloc(offset_size + 1);

    struct kndTextState *curr_state;
    struct kndTranslation *tr;
    struct kndTextSelect *sel;
  
    memset(offset, ' ', offset_size);
    offset[offset_size] = '\0';

    curr_state = self->states;
    while (curr_state) {
        /*if (curr_state->text_size) {
            knd_log("%s%s: %s [#%lu]\n", offset,
                    curr_state->lang_code, curr_state->text,
                    (unsigned long)curr_state->state);
                    }*/
        
        tr = curr_state->translations;
        while (tr) {
            
            knd_log("%s%s: %s [#%lu]      num chunks: %lu\n", offset, tr->lang_code, tr->seq,
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
    
    //struct kndTextSelect *sel;
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
    
    curr_state = self->states;

    if (curr_state->translations) {
        tr = curr_state->translations;
            
        while (tr) {
            /* check language */
            if (obj->cache->repo->lang_code_size) {
                if (DEBUG_TEXT_LEVEL_3)
                    knd_log("  .. text LANG: %s curr user lang: %s\n",
                            tr->lang_code, obj->cache->repo->lang_code);
                
                if (strcmp(tr->lang_code, obj->cache->repo->lang_code))
                    goto next_tr;
            }
            
            if (num_trs) {
                err = out->write(out, ",", 1);
                if (err) return err;
            }
            
            err = out->write(out, "\"", 1);
            if (err) return err;

            err = out->write(out, tr->lang_code, tr->lang_code_size);
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
            if (obj->cache->repo->lang_code_size) {

                if (DEBUG_TEXT_LEVEL_3)
                    knd_log("  .. text LANG: %s curr user lang: %s\n",
                            tr->lang_code, obj->cache->repo->lang_code);
                
                if (strcmp(tr->lang_code, obj->cache->repo->lang_code))
                    goto next_tr;
            }

            /*buf_size = sprintf(buf, "\"l\":\"%s\"",
                               tr->lang_code);
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

    obj = self->elem->obj;

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
        
        buf_size = sprintf(buf, "{l %s}",
                           tr->lang_code);
        err = self->out->write(self->out,  buf, buf_size);
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


static int
kndText_parse_synt_graph(struct kndText *self __attribute__((unused)),
                         const char *rec,
                         size_t *total_size)
{
    //char buf[KND_LARGE_BUF_SIZE];
    //size_t buf_size;
    
    const char *c;
    const char *b;

    //size_t chunk_size;
    int err = knd_FAIL;
    
    c = rec;
    b = c;

    while (*c) {
        switch (*c) {
            /* non-whitespace char */
        default:

            break;
        case '>':

            knd_log("  ++ got SYNT GRAPH!\n");
            
            *total_size = c - rec;
            return knd_OK;
        }
        c++;
    }

    return err;
}


static int
kndText_read_selections(struct kndText *self __attribute__((unused)),
                        struct kndTranslation *tr,
                        const char *rec,
                        size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;

    char refbuf[KND_NAME_SIZE];
    size_t refbuf_size;

    struct kndTextSelect *sel = NULL;
    struct kndObjRef *ref;

    const char *c;
    const char *b;
    char *s;
    
    long numval = -1;
    long sel_len = 0;

    bool in_sel = false;
    bool in_pos = false;
    bool in_ref = false;
    bool in_hilite = false;
    bool in_hilite_end = false;
    
    //size_t chunk_size;
    int err = knd_FAIL;
    
    c = rec;
    b = c;

    while (*c) {
        switch (*c) {
            /* non-whitespace char */
        default:

            break;
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            if (!in_sel) break;
            
            /* whitespace */
            buf_size = c - b;
            if (buf_size >= KND_NAME_SIZE) break;
            
            memcpy(buf, b, buf_size);
            buf[buf_size] = '\0';
            
            if (!strcmp(buf, "p")) {
                in_pos = true;
                b = c + 1;
                break;
            }
            
            if (!strcmp(buf, "ref")) {
                in_ref = true;
                b = c + 1;
                break;
            }

            if (!strcmp(buf, "hi")) {
                in_hilite = true;
                b = c + 1;
                break;
            }
            
            break;
        case '{':
            if (!in_sel) { 
                in_sel = true;
            }
            
            b = c + 1;
            break;
        case '}':

            /* non-matching bracket */
            if (!in_sel) return knd_FAIL;
            
            if (in_pos) {
                buf_size = c - b;

                memcpy(buf, b, buf_size);
                buf[buf_size] = '\0';

                s = strchr(buf, '+');
                if (s) {
                    *s = '\0';
                    s++;
                    err = knd_parse_num((const char*)s, &sel_len);
                    if (err) return err;
                }
                
                err = knd_parse_num((const char*)buf, &numval);
                if (err) goto final;

                /*knd_log("   ++ POS: %lu\n", (unsigned long)numval);*/

                in_pos = false;
                break;
            }

            if (in_hilite) {
                refbuf_size = c - b;
                if (refbuf_size >= KND_NAME_SIZE) return knd_LIMIT;

                if (numval < 0) break;

                sel = malloc(sizeof(struct kndTextSelect));
                if (!sel) return knd_NOMEM;
                
                memset(sel, 0, sizeof(struct kndTextSelect));

                memcpy(sel->css_name, b, refbuf_size);
                sel->css_name[refbuf_size] = '\0';
                sel->css_name_size = refbuf_size;
                
                sel->pos = numval;
                sel->len = 1;
                if (sel_len) sel->len = sel_len;
            
                if (!tr->tail) {
                    tr->tail = sel;
                    tr->selects = sel;
                }
                else {
                    tr->tail->next = sel;
                    tr->tail = sel;
                }
                
                tr->num_selects++;

                numval = -1;
                refbuf_size = 0;
                in_hilite = false;
                in_hilite_end = true;
                break;
            }

            if (in_ref) {
                refbuf_size = c - b;
                memcpy(refbuf, b, refbuf_size);
                refbuf[refbuf_size] = '\0';
                in_ref = false;

                /*knd_log("   ++ REF: %s\n", refbuf);*/
                
                break;
            }

            if (in_hilite_end) {
                in_hilite_end = false;
                in_sel = false;
                break;
            }
            
            if (!refbuf_size) break;
            if (numval < 0) break;
            
            /* end of selection */
            err = kndObjRef_new(&ref);
            if (err) return err;
                
            memcpy(ref->name, refbuf, refbuf_size);
            ref->name[refbuf_size] = '\0';
            ref->name_size = refbuf_size;
                
            /*knd_log("  .. reading TEXT SEL: %s (pos: %lu)\n",
                    ref->name, (unsigned long)numval);
            */
            
            sel = malloc(sizeof(struct kndTextSelect));
            if (!sel) return knd_NOMEM;
                
            memset(sel, 0, sizeof(struct kndTextSelect));
                
            sel->ref = ref;
            sel->pos = numval;
            sel->len = 1;
            if (sel_len) sel->len = sel_len;
            
            if (!tr->tail) {
                tr->tail = sel;
                tr->selects = sel;
            }
            else {
                tr->tail->next = sel;
                tr->tail = sel;
            }
            
            tr->num_selects++;

            numval = -1;
            refbuf_size = 0;
            in_sel = false;
            
            break;
        case ']':
            if (in_sel) return knd_FAIL;
            
            *total_size = c - rec;
            return knd_OK;
        }
        c++;
    }


 final:
    return err;
}



static int
kndText_parse_selection(struct kndText *self,
                        struct kndTranslation *tr,
                        const char *rec,
                        size_t *total_size,
                        size_t *total_chunks)
{
    //char buf[KND_NAME_SIZE];
    size_t buf_size;

    char refbuf[KND_NAME_SIZE];
    size_t refbuf_size;

    //size_t curr_size;
    struct kndTextSelect *sel;
    struct kndObjRef *ref;
    
    const char *c;
    const char *b;

    bool in_field = false;
    //bool in_field_name = false;

    //bool in_text = false;
    //bool in_text_val = false;

    size_t start_pos;
    size_t chunk_size;
    int err = knd_FAIL;
    size_t num_chunks = 0;
    
    c = rec;
    b = c;
    
    start_pos = self->out->buf_size;
    chunk_size = 0;
    
    while (*c) {
        switch (*c) {
            /* non-whitespace char */
        default:
            
            break;
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            /* whitespace */
            if (in_field) {

                refbuf_size = c - b;
                if (refbuf_size >= KND_NAME_SIZE) return knd_LIMIT;

                memcpy(refbuf, b, refbuf_size);
                refbuf[refbuf_size] = '\0';
                
                b = c + 1;
                break;
            }

            num_chunks++;
            
            break;
        case '{':
            /* in field */
            if (!in_field) {
                in_field = true;
            
                /* write text string */
                /*if (num_chunks) {
                    err = self->out->write(self->out,
                                           KND_TEXT_CHUNK_SEPAR, strlen(KND_TEXT_CHUNK_SEPAR));
                    if (err) return err;
                }
                */
                
                chunk_size = c - b;
                err = self->out->write(self->out, b, chunk_size);
                if (err) return err;
                
                b = c + 1;
            }
            break;
        case '}':

            if (in_field) {
                buf_size = c - b;
                if (buf_size >= KND_NAME_SIZE) return knd_LIMIT;

                if (!buf_size) {
                    knd_log("  -- empty chunk :(\n");
                    return knd_FAIL;
                }
                
                if (!strcmp(refbuf, "hi")) {
                    sel = malloc(sizeof(struct kndTextSelect));
                    if (!sel) return knd_NOMEM;
                
                    memset(sel, 0, sizeof(struct kndTextSelect));
                    num_chunks++;
                
                    sel->pos = tr->chunk_count;
                    sel->len = num_chunks;
                
                    memcpy(sel->css_name, b, buf_size);
                    sel->css_name[buf_size] = '\0';
                    sel->css_name_size = buf_size;
                    
                    if (!tr->tail) {
                        tr->tail = sel;
                        tr->selects = sel;
                    }
                    else {
                        tr->tail->next = sel;
                        tr->tail = sel;
                    }

                    tr->num_selects++;
                    in_field = false;
                    break;
                }


                err = kndObjRef_new(&ref);
                if (err) return err;
                
                memcpy(ref->name, b, buf_size);
                ref->name[buf_size] = '\0';
                ref->name_size = buf_size;
                
                sel = malloc(sizeof(struct kndTextSelect));
                if (!sel) return knd_NOMEM;
                
                memset(sel, 0, sizeof(struct kndTextSelect));
                
                num_chunks++;
                
                sel->ref = ref;
                sel->pos = tr->chunk_count;
                sel->len = num_chunks;
                
                knd_log("\n    == TEXT REF: \"%s\" (pos: %lu, len: %lu)\n\n",
                        ref->name,
                        (unsigned long)sel->pos, (unsigned long)sel->len);
                
                if (!tr->tail) {
                    tr->tail = sel;
                    tr->selects = sel;
                }
                else {
                    tr->tail->next = sel;
                    tr->tail = sel;
                }

                tr->num_selects++;
                in_field = false;
                break;
            }
            
            *total_size = c - rec;
            *total_chunks = num_chunks;
            
            return knd_OK;
        }
        
        c++;
    }

    return err;
}


static int
kndText_parse_translation(struct kndText *self,
                          struct kndTranslation *tr,
                          const char *rec,
                          size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;

    //char wchunk[KND_NAME_SIZE];
    size_t wchunk_size;

    //size_t char_num_val;
    //size_t curr_size;

    const char *c;
    const char *b;
    const char *w;

    bool in_field = false;
    bool in_field_name = false;

    bool in_lang_code = false;
    bool in_lang_val = false;

    bool in_text = false;
    bool in_text_val = false;
    //bool in_ref = false;

    bool in_synt_graph = false;

    bool is_alpha_num = false;

    bool space_needed = false;
    bool got_selection = false;
    bool got_space = false;
    
    size_t chunk_size;
    size_t num_chunks = 0;
    
    int err = knd_FAIL;
    
    c = rec;
    b = c;
    w = rec;

    
    self->out->reset(self->out);

    while (*c) {
        switch (*c) {
            /* non-whitespace char */
        default:

            if (in_lang_code) {
                if (!in_lang_val) {
                    b = c;
                    in_lang_val = true;
                    break;
                }
            }

            if (in_text) {

                if (!in_text_val) {
                    b = c;
                    w = c;
                    in_text_val = true;
                    is_alpha_num = true;
                    tr->chunk_count = 0;
                    break;
                }

                /* previous char was whitespace */
                if (!is_alpha_num) w = c;
                
                is_alpha_num = true;
                got_space = false;
            }
            
            break;
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            /* whitespace */

            if (in_text) {

                /* word chunk */
                if (is_alpha_num) {
                    wchunk_size = c - w;
                    
                    if (wchunk_size) {
                        if (wchunk_size >= KND_NAME_SIZE) {
                            knd_log("  chunk limit? :(\n");
                        }
                        else {

                            /* add whitespace if needed */
                            if (tr->chunk_count) 
                                space_needed = true;

                            if (got_selection) {
                                space_needed = false;
                                got_selection = false;
                            }
                            
                            if (space_needed) {
                                err = self->out->write(self->out,
                                                       KND_TEXT_CHUNK_SEPAR, strlen(KND_TEXT_CHUNK_SEPAR));
                                if (err) return err;
                            }
                            
                            err = self->out->write(self->out, w, wchunk_size);
                            if (err) return err;

                            /*memcpy(wchunk, w, wchunk_size);
                            wchunk[wchunk_size] = '\0';
                            knd_log("  .. write out CHUNK \"%s\"..\n", wchunk); */
                            
                            tr->chunk_count++;
                        }
                    }
                }

                got_selection = false;
                is_alpha_num = false;
                got_space = true;
                break;
            }
            
            if (!in_field) break;

            if (!in_field_name) {
                buf_size = c - b;

                if (buf_size >= KND_NAME_SIZE) return knd_LIMIT;
                
                memcpy(buf, b, buf_size);
                buf[buf_size] = '\0';
                
                in_field_name = true;
                
                /*knd_log("TRANSLATE FIELD name: \"%s\"\n", buf);*/
            
                if (!strcmp(buf, "l")) {
                    in_lang_code = true;
                    break;
                }
                
                if (!strcmp(buf, "t")) {
                    in_text = true;
                    break;
                }

                if (!strcmp(buf, "sg")) {
                    in_synt_graph = true;
                    break;
                }
            }
            break;
        case '<':
            if (!in_synt_graph) {
                b = c + 1;
                break;
            }

            c++;
            err = kndText_parse_synt_graph(self, c, &chunk_size);
            if (err) goto final;
            c += chunk_size;
            
            break;
        case '[':
            c++;
            
            err = kndText_read_selections(self, tr, c, &chunk_size);
            if (err) goto final;
            
            c += chunk_size;
            b = c + 1;
            break;
        case '{':
            if (!in_field) {
                in_field = true;
                b = c + 1;
                break;
            }

            if (in_text) {

                /* in selection area */
                if (is_alpha_num) {
                    wchunk_size = c - w;
                    if (wchunk_size) {
                        if (wchunk_size >= KND_NAME_SIZE) {
                            knd_log("  chunk limit? :(\n");
                        }
                        else {
                            if (tr->chunk_count)
                                space_needed = true;

                            if (got_selection) {
                                space_needed = false;
                                got_selection = false;
                            }

                            if (space_needed) {
                                err = self->out->write(self->out,
                                                       KND_TEXT_CHUNK_SEPAR, strlen(KND_TEXT_CHUNK_SEPAR));
                                if (err) return err;
                            }

                            err = self->out->write(self->out, w, wchunk_size);
                            if (err) return err;
                        }
                    }
                }
                
                c++;

                /* space before selection itself */
                if (got_space) {
                    err = self->out->write(self->out,
                                           KND_TEXT_CHUNK_SEPAR, strlen(KND_TEXT_CHUNK_SEPAR));
                    if (err) return err;
                    got_space = false;
                }


                err = kndText_parse_selection(self, tr, c, &chunk_size, &num_chunks);
                if (err) goto final;

                c += chunk_size;
                tr->chunk_count += num_chunks;

                if (DEBUG_TEXT_LEVEL_3)
                    knd_log("    == CURR TEXT: \"%s\"\n\n", self->out->buf);

                b = c + 1;
                w = c + 1;
                is_alpha_num = false;
                got_selection = true;
                break;
            }
            
            break;
        case '}':

            if (in_field) {

                if (in_lang_val) {
                    buf_size = c - b;

                    if (buf_size >= KND_LANG_CODE_SIZE) {
                        err = knd_LIMIT;
                        goto final;
                    }

                    memcpy(tr->lang_code, b, buf_size);
                    tr->lang_code[buf_size] = '\0';
                    tr->lang_code_size = buf_size;
                    
                    /* TODO: convert shortcuts to full lang codes */
                    if (!strcmp(tr->lang_code, "RU")) {
                        buf_size = strlen("ru_RU");
                        memcpy(tr->lang_code, "ru_RU", buf_size);
                        tr->lang_code[buf_size] = '\0';
                        tr->lang_code_size = buf_size;
                    }

                    if (DEBUG_TEXT_LEVEL_3)
                        knd_log("  == LANG CODE: \"%s\"\n", tr->lang_code);
                    
                    in_lang_code = false;
                    in_lang_val = false;
                }

                if (in_text_val) {
                    /*buf_size = c - b;
                    err = self->out->write(self->out, b, buf_size);
                    if (err) return err;
                    */

                    /* word chunk */
                    if (is_alpha_num) {
                        wchunk_size = c - w;
                        if (wchunk_size) {
                            if (wchunk_size >= KND_NAME_SIZE) {
                                knd_log("   -- chunk limit? :(\n");
                            }
                            else {
                                space_needed = true;
                                if (got_selection) {
                                    space_needed = false;
                                    got_selection = false;
                                }
                            
                                if (space_needed) {
                                    err = self->out->write(self->out,
                                                           KND_TEXT_CHUNK_SEPAR,
                                                           strlen(KND_TEXT_CHUNK_SEPAR));
                                    if (err) return err;
                                }
                                
                                self->out->write(self->out, w, wchunk_size);
                                if (err) return err;
                            }
                        }
                    }

                    if (self->out->buf_size) {
                        tr->seq = malloc(self->out->buf_size + 1);
                        if (!tr->seq) {
                            err = knd_NOMEM;
                            goto final;
                        }
                        
                        memcpy(tr->seq, self->out->buf, self->out->buf_size);
                        tr->seq[self->out->buf_size] = '\0';
                        tr->seq_size = self->out->buf_size;

                        knd_remove_nonprintables(tr->seq);

                        if (DEBUG_TEXT_LEVEL_3)
                            knd_log("\n   == TEXT val: \"%s\"\n\n",
                                    tr->seq);
                    }
                    
                    in_text = false;
                    in_text_val = false;
                }
                
                in_field = false;
                in_field_name = false;
                break;
            }


            if (in_synt_graph) {
                in_synt_graph = false;
            }
            
            /*knd_log("  -- end of TRANSLATE elem!\n");*/
            
            *total_size = c - rec;

            return knd_OK;
        }
        
        c++;
    }

 final:
    return err;
}


static int
kndText_parse_translations(struct kndText *self,
                           struct kndTextState *curr_state,
                           const char *rec,
                           size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;

    struct kndTranslation *tr;
    
    const char *c;
    const char *b;

    bool in_list = true;
    bool in_list_class = false;
    //bool in_name = false;

    size_t chunk_size;
    int err = knd_FAIL;
    
    c = rec;
    b = c;
    
    while (*c) {
        switch (*c) {
            /* non-whitespace char */
        default:
            if (!in_list) break;
            
            break;
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            /* whitespace */
            if (!in_list) break;

            if (!in_list_class) {
                buf_size = c - b;

                if (buf_size >= KND_NAME_SIZE) return knd_LIMIT;

                memcpy(buf, b, buf_size);
                buf[buf_size] = '\0';

                /*knd_log("LIST class: \"%s\"\n", buf);*/

                in_list_class = true;
            }
            
            break;
        case '{':

            tr = malloc(sizeof(struct kndTranslation));
            if (!tr) return knd_NOMEM;

            memset(tr, 0, sizeof(struct kndTranslation));

            c++;
            err = kndText_parse_translation(self, tr, c, &chunk_size);
            if (err) goto final;
            c += chunk_size;

            tr->next = curr_state->translations;

            curr_state->translations = tr;
            curr_state->num_translations++;
            
            break;
        case ']':
            /*knd_log("   .. end of translations: %s\n", c);*/

            *total_size = c - rec;
            
            return knd_OK;
        }
        
        c++;
    }

 final:
    return err;
}


static int
kndText_parse(struct kndText *self,
              const char *rec,
              size_t *total_size)
{
    //char buf[KND_NAME_SIZE];
    //size_t buf_size;

    struct kndTextState *curr_state = NULL;
    const char *c;
    const char *b;

    bool in_body = true;
    //bool in_name = false;
    size_t chunk_size;
    int err = knd_FAIL;
    
    c = rec;
    b = c;

    if (DEBUG_TEXT_LEVEL_3)
        knd_log("   .. parse TEXT field from \"%s\"\n\n OUTPUT: %p\n", c, self->out);
    
    while (*c) {
        switch (*c) {
            /* non-whitespace char */
        default:
            if (!in_body) break;
            break;
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            /* whitespace */
            if (!in_body) break;

            break;
        case '{':
            /*knd_log("   .. parse text field from \"%s\"\n", c);*/
            
            break;
        case '}':

            *total_size = c - rec;

            return knd_OK;
           
            break;
        case '[':
            c++;
            
            curr_state = malloc(sizeof(struct kndTextState));
            if (!curr_state) {
                err = knd_NOMEM;
                goto final;
            }
            
            memset(curr_state, 0, sizeof(struct kndTextState));
            
            err = kndText_parse_translations(self, curr_state, c, &chunk_size);
            if (err) goto final;

            c += chunk_size;
            
            self->states = curr_state;
            self->num_states++;
            break;
        }
        
        c++;
    }

 final:

    if (curr_state) free(curr_state);
    
    return err;
}




static int
kndText_update_translation(struct kndText *self,
                           struct kndTranslation *tr,
                           const char *rec,
                           size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;

    //size_t char_num_val;
    //size_t curr_size;
    
    const char *c;
    const char *b;

    bool in_body = true;
    bool in_field = false;
    bool in_field_name = false;

    bool in_lang_code = false;
    bool in_lang_val = false;

    bool in_text = false;
    bool in_text_val = false;

    bool in_synt_graph = false;

    size_t chunk_size;
    int err = knd_FAIL;
    
    c = rec;
    b = c;

    while (*c) {
        switch (*c) {
            /* non-whitespace char */
        default:
            if (!in_body) break;

            if (in_lang_code) {
                if (!in_lang_val) {
                    b = c;
                    in_lang_val = true;
                    break;
                }
            }

            if (in_text) {
                if (!in_text_val) {
                    b = c;
                    in_text_val = true;
                    break;
                }
            }
            
            break;
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            /* whitespace */
            if (!in_body) break;

            if (!in_field) break;

            if (!in_field_name) {
                buf_size = c - b;

                memcpy(buf, b, buf_size);
                buf[buf_size] = '\0';
                
                in_field_name = true;
                
                /*knd_log("TRANSLATE FIELD name: \"%s\"\n", buf);*/
            
                if (!strcmp(buf, "l")) {
                    in_lang_code = true;
                    break;
                }
                
                if (!strcmp(buf, "t")) {
                    in_text = true;
                    break;
                }

                if (!strcmp(buf, "sg")) {
                    in_synt_graph = true;
                    break;
                }


            }
            
            break;
        case '<':
            if (!in_synt_graph) {
                b = c + 1;
                break;
            }

            c++;
            err = kndText_parse_synt_graph(self, c, &chunk_size);
            if (err) goto final;
            c += chunk_size;
            
            break;
        case '[':
            c++;
            if (!(*c)) return knd_FAIL;
            
            err = kndText_read_selections(self, tr, c, &chunk_size);
            if (err) goto final;
            c += chunk_size;

            break;
        case '{':
            if (!in_field) {
                in_field = true;
                b = c + 1;
                break;
            }
            
            break;
        case '}':

            if (in_field) {

                if (in_lang_val) {
                    buf_size = c - b;

                    if (buf_size >= KND_LANG_CODE_SIZE) {
                        err = knd_LIMIT;
                        goto final;
                    }
                    
                    memcpy(tr->lang_code, b, buf_size);
                    tr->lang_code[buf_size] = '\0';
                    
                    knd_log("  == LANG val: \"%s\"\n",
                            tr->lang_code);
                    
                    in_lang_code = false;
                    in_lang_val = false;
                }

                if (in_text_val) {
                    buf_size = c - b;

                    tr->seq = malloc(buf_size + 1);
                    if (!tr->seq) {
                        err = knd_NOMEM;
                        goto final;
                    }

                    memcpy(tr->seq, b, buf_size);
                    tr->seq[buf_size] = '\0';
                    
                    tr->seq_size = buf_size;

                    knd_log("  == updated TEXT val: \"%s\"\n",
                            tr->seq);
                    
                    in_text = false;
                    in_text_val = false;
                }
                
                in_field = false;
                in_field_name = false;
                break;
            }


            if (in_synt_graph) {
                in_synt_graph = false;
            }
            
            /*knd_log("  -- end of TRANSLATE elem!\n");*/
            
            *total_size = c - rec;

            return knd_OK;
        }
        
        c++;
    }

 final:
    return err;
}



static int
kndText_update_translations(struct kndText *self,
                           struct kndTextState *curr_state,
                           const char *rec,
                           size_t *total_size)
{
    char buf[KND_LARGE_BUF_SIZE];
    size_t buf_size;

    struct kndTranslation *tr;
    
    const char *c;
    const char *b;

    bool in_list = true;
    bool in_list_class = false;
    //bool in_name = false;

    size_t chunk_size;
    int err = knd_FAIL;
    
    c = rec;
    b = c;

    while (*c) {
        switch (*c) {
            /* non-whitespace char */
        default:
            if (!in_list) break;
            
            break;
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            /* whitespace */
            if (!in_list) break;

            if (!in_list_class) {
                buf_size = c - b;
                memcpy(buf, b, buf_size);
                buf[buf_size] = '\0';

                /*knd_log("LIST class: \"%s\"\n", buf);*/

                in_list_class = true;
            }
            
            break;
        case '{':

            tr = malloc(sizeof(struct kndTranslation));
            if (!tr) return knd_NOMEM;

            memset(tr, 0, sizeof(struct kndTranslation));

            c++;
            err = kndText_update_translation(self, tr, c, &chunk_size);
            if (err) goto final;
            c += chunk_size;

            tr->state = curr_state->state;
            tr->next = curr_state->translations;
            curr_state->translations = tr;
            curr_state->num_translations++;
            
            break;
        case ']':
            /*knd_log("   .. end of translations!\n");*/
            *total_size = c - rec;
            return knd_OK;
        }
        
        c++;
    }

 final:
    return err;
}


static int
kndText_update(struct kndText *self,
               const char *rec,
               size_t *total_size)
{
    char buf[KND_LARGE_BUF_SIZE];
    size_t buf_size;

    struct kndTextState *state = NULL;
    const char *c;
    const char *b;

    bool in_body = true;
    bool in_state = false;
    size_t chunk_size;
    long numval;
    int err = knd_FAIL;
    
    c = rec;
    b = c;

    knd_log("  .. TEXT update: \"%s\"\n\n", rec);
    
    while (*c) {
        switch (*c) {
            /* non-whitespace char */
        default:
            if (!in_body) break;
            break;
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            /* whitespace */
            if (!in_body) break;

            break;
        case '#':
            if (!in_state) {
                in_state = true;
                b = c + 1;
                break;
            }
            break;
        case '{':

            b = c + 1;
            break;
        case '}':

            if (in_state) {
                buf_size = c - b;
                memcpy(buf, b, buf_size);
                buf[buf_size] = '\0';

                err = knd_parse_num((const char*)buf, &numval);
                if (err) goto final;
                
                knd_log("   == CURR TEXT STATE to confirm: %lu\n",
                        (unsigned long)numval);

                /* is the recent state greater? */
                if (self->states->state > (unsigned long)numval) {
                    knd_log("  -- current state is greater than this, please check the updates!\n");
                    err = knd_FAIL;
                    goto final;
                }

                /* alloc new state */
                state = malloc(sizeof(struct kndTextState));
                if (!state) {
                    err = knd_NOMEM;
                    goto final;
                }
                memset(state, 0, sizeof(struct kndTextState));
                state->state = self->elem->obj->cache->repo->state;
                
                in_state = false;
                break;
            }

            *total_size = c - rec;
            return knd_OK;
        case '[':
            c++;
            if (!(*c)) return knd_FAIL;
            
            err = kndText_update_translations(self, state, c, &chunk_size);
            if (err) goto final;

            state->next = self->states;
            self->states = state;
            self->num_states++;
            
            c += chunk_size;
            break;
        }
        
        c++;
    }

 final:
    return err;
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
    self->parse = kndText_parse;
    self->update = kndText_update;
    self->index = kndText_index;

    *text = self;

    return knd_OK;
}
