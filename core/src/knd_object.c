#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_concept.h"
#include "knd_attr.h"
#include "knd_elem.h"
#include "knd_repo.h"
#include "knd_object.h"
#include "knd_text.h"
#include "knd_refset.h"
#include "knd_sorttag.h"
#include "knd_parser.h"

#include "knd_output.h"
#include "knd_user.h"

#define DEBUG_OBJ_LEVEL_1 0
#define DEBUG_OBJ_LEVEL_2 0
#define DEBUG_OBJ_LEVEL_3 0
#define DEBUG_OBJ_LEVEL_4 0
#define DEBUG_OBJ_LEVEL_TMP 1

static int
knd_compare_obj_by_match_descend(const void *a,
                                 const void *b)
{
    struct kndObject **obj1, **obj2;

    obj1 = (struct kndObject**)a;
    obj2 = (struct kndObject**)b;

    if ((*obj1)->average_score == (*obj2)->average_score) return 0;

    if ((*obj1)->average_score < (*obj2)->average_score) return 1;

    return -1;
}


static int del(struct kndObject *self)
{
    knd_log("  .. free obj: \"%s\".. \n", self->name);

    free(self);

    return knd_OK;
}


static void str(struct kndObject *self,
                size_t depth)
{
    size_t offset_size = sizeof(char) * KND_OFFSET_SIZE * depth;
    char *offset = malloc(offset_size + 1);
    struct kndElem *elem;
    
    memset(offset, ' ', offset_size);
    offset[offset_size] = '\0';

    if (self->type == KND_OBJ_ADDR) {
        knd_log("\n%sOBJ %.*s::%.*s [%.*s]\n",
                offset, self->conc->name_size, self->conc->name,
                self->name_size, self->name,
                KND_ID_SIZE, self->id);
    }

    elem = self->elems;
    while (elem) {
        elem->str(elem, depth + 1);
        elem = elem->next;
    }
}

static int 
kndObject_export_aggr_JSON(struct kndObject *self)
{
    struct kndElem *elem;
    int err;

    /* anonymous obj */
    err = self->out->write(self->out, "{", 1);
    if (err) return err;

    elem = self->elems;
    while (elem) {

        elem->out = self->out;
        err = elem->export(elem, KND_FORMAT_JSON, 1);
        if (err) return err;
        
        if (elem->next) {
            err = self->out->write(self->out, ",", 1);
            if (err) return err;
        }

        elem = elem->next;
    }

    err = self->out->write(self->out, "}", 1);
    if (err) return err;

    return knd_OK;
}

static int
kndObject_export_JSON(struct kndObject *self)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    //char pathbuf[KND_TEMP_BUF_SIZE];
    //size_t pathbuf_size;

    //struct ooDict *idx;
    //struct kndConcept *conc;
    struct kndElem *elem;
    //struct kndRefSet *refset;

    struct kndRelClass *relc;
    struct kndRepoCache *cache = NULL;
    struct kndRelType *reltype;
    struct kndObjRef *r;
    struct kndObject *obj;
    bool is_concise = true;
    bool need_separ;
    int err;

    if (DEBUG_OBJ_LEVEL_2)
        knd_log("   .. export OBJ \"%s\"  is_concise: %d\n",
            self->name, is_concise);

    if (self->type == KND_OBJ_AGGR) {
        err = kndObject_export_aggr_JSON(self);
        return err;
    }

    buf_size = sprintf(buf, "{\"n\":\"%s\"", self->name);
    err = self->out->write(self->out, buf, buf_size);
    if (err) goto final;

    /*buf_size = sprintf(buf, ",\"id\":%.*s", self->id);
    err = self->out->write(self->out, buf, buf_size);
    if (err) goto final;
    */
    
    /* ELEMS */
    /*if (self->elems) {
        err = self->out->write(self->out, ",\"elems\":{", strlen(",\"elems\":{"));
        if (err) goto final;
        }*/

    need_separ = false;
    elem = self->elems;
    while (elem) {
        /* filter out detailed presentation */
        if (is_concise) {
            /* aggr obj? */
            if (elem->aggr) {
                obj = elem->aggr;
                obj->out = self->out;

                /*if (need_separ) {*/
                err = self->out->write(self->out, ",", 1);
                if (err) return err;

                err = self->out->write(self->out, "\"", 1);
                if (err) return err;
                err = self->out->write(self->out, elem->attr->name, elem->attr->name_size);
                if (err) return err;
                err = self->out->write(self->out, "\":", 2);
                if (err) return err;
                
                err = obj->export(obj);
                if (err) return err;

                need_separ = true;
                goto next_elem;
            }
            
            if (elem->attr) 
                if (elem->attr->concise_level)
                    goto export_elem;

            if (DEBUG_OBJ_LEVEL_2)
                knd_log("  .. skip JSON elem: %s..\n", elem->attr->name);

            goto next_elem;
        }

    export_elem:

        /*if (need_separ) {*/
        err = self->out->write(self->out, ",", 1);
        if (err) goto final;

        /* default export */
        elem->out = self->out;
        err = elem->export(elem, KND_FORMAT_JSON, 0);
        if (err) {
            knd_log("-- elem not exported: %s", elem->attr->name);
            goto final;
        }
        
        need_separ = true;
        
    next_elem:
        elem = elem->next;
    }

    /*if (self->elems) {
        err = self->out->write(self->out, "}", 1);
        if (err) goto final;
        }*/

    if (is_concise) goto closing;

    /* skip relations */
    if (self->depth) goto closing;
    
    /*  relations */
    if (self->rel_classes) {
        err = self->out->write(self->out, ",\"rels\":[", strlen(",\"rels\":["));
        if (err) return err;
    }

    /*relc = self->rel_classes;
    while (relc) {

        err = self->cache->repo->get_cache(self->cache->repo, relc->conc, &cache);
        if (err) return err;
        
        err = self->out->write(self->out, "{\"c\":\"",  strlen("{\"c\":\""));
        if (err) return err;

        err = self->out->write(self->out, relc->conc->name, relc->conc->name_size);
        if (err) return err;
        
        err = self->out->write(self->out, "\",\"attrs\":[", strlen("\",\"attrs\":["));
        if (err) return err;

        reltype = relc->rel_types;
        while (reltype) {
            err = self->out->write(self->out, "{\"attr\":\"", strlen("{\"attr\":\""));
            if (err) return err;

            err = self->out->write(self->out, reltype->attr_name, reltype->attr_name_size);
            if (err) return err;

            err = self->out->write(self->out, "\",\"refs\":[", strlen("\",\"refs\":["));
            if (err) return err;

            r = reltype->refs;
            while (r) {
                if (r->obj) {
                    err = self->out->write(self->out, "{\"obj\":", strlen("{\"obj\":"));
                    if (err) return err;

                    r->obj->out = self->out;
                    r->obj->depth = self->depth + 1;
                    err = r->obj->export(r->obj);
                    if (err) return err;

                    err = self->out->write(self->out, "}", 1);
                    if (err) return err;
                }
                else {
                    err = self->out->write(self->out, "{\"ref\":\"", strlen("{\"ref\":\""));
                    if (err) return err;

                    err = self->out->write(self->out, r->obj_id, KND_ID_SIZE);
                    if (err) return err;

                    err = self->out->write(self->out, "\"}", 2);
                    if (err) return err;
                }
                
                if (r->next) {
                    err = self->out->write(self->out, ",", 1);
                    if (err) return err;
                }
                
                r = r->next;
            }
            
            err = self->out->write(self->out, "]}", 2);
            if (err) return err;

            if (reltype->next) {
                err = self->out->write(self->out, ",", 1);
                if (err) return err;
            }

            reltype = reltype->next;
        }
        err = self->out->write(self->out, "]}", 2);
        if (err) return err;

        if (relc->next) {
            err = self->out->write(self->out, ",", 1);
            if (err) return err;
        }

        relc = relc->next;
    }
    */
    
    if (self->rel_classes) {
        err = self->out->write(self->out, "]", 1);
        if (err) return err;
    }
    
 closing:

    err = self->out->write(self->out, "}", 1);
    if (err) goto final;

    
 final:
    return err;
}




static int 
kndObject_export_GSC(struct kndObject *self)
{
    bool got_elem = false;
    struct kndElem *elem;
    bool is_concise = true;

    //struct kndRelClass *relc;
    //struct kndRelType *reltype;
    //struct kndTermIdx *idx, *term_idx;

    //size_t i, j, ri;
    int err;
    
    if (DEBUG_OBJ_LEVEL_2)
        knd_log("  .. export GSC obj \"%s\" [id: %s]..\n",
                self->name, self->id);

    err = self->out->write(self->out, "{", 1);
    if (err) return err;

    /*
    if (!self->parent) {
        err = self->out->write(self->out, self->name, self->name_size);
        if (err) return err;

        err = self->out->write(self->out, " ", 1);
        if (err) return err;
    }
    else {
        if (!self->parent->is_list) {
            err = self->out->write(self->out, self->parent->name, self->parent->name_size);
            if (err) return err;
        }

    }
    */
    
    /*if (self->filename_size) {
        err = knd_make_id_path(buf, "repos",
                               self->cache->repo->id,
                               self->cache->baseclass->name);
        if (err) return err;

        if (DEBUG_OBJ_LEVEL_3)
            knd_log("  == relative Repo path: \"%s\"\n",
                    buf);

        err = knd_make_id_path(pathbuf, buf, self->id, self->filename);
        if (err) return err;

        if (DEBUG_OBJ_LEVEL_3)
            knd_log("   == relative filename: %s\n",
                    pathbuf);
        
        buf_size = sprintf(buf, "{_fn %s}",
                           pathbuf);
        err = self->out->write(self->out, buf, buf_size);
        if (err) return err;
        
        self->filepath = strdup(pathbuf);
        if (!self->filepath) {
            err = knd_NOMEM;
            return err;
        }
        
    }

    if (self->filesize) {
        buf_size = sprintf(buf, "{_fsize %lu}",
                           (unsigned long)self->filesize);
        err = self->out->write(self->out, buf, buf_size);
        if (err) return err;
        } */
    

    /* ELEMS */
    got_elem = false;
    elem = self->elems;
    while (elem) {
        /* filter out detailed presentation */
        /*if (is_concise) {
            if (elem->attr->concise_level)
                goto export_elem;
            goto next_elem;
            }*/

        //export_elem:

        /* default export */
        elem->out = self->out;
        err = elem->export(elem, KND_FORMAT_GSC, is_concise);
        if (err) {
            knd_log("-- export of \"%s\" elem failed: %d :(", elem->attr->name, err);
            return err;
        }
        
        got_elem = true;
        
        //next_elem:
        elem = elem->next;
    }

    /*relc = self->cache->rel_classes;
    while (relc) {
        reltype = relc->rel_types;
        while (reltype) {
            refset = reltype->idx->get(reltype->idx, (const char*)self->name);
            if (!refset) goto next_reltype;

            err = self->out->write(self->out, "{_cls{", strlen("{_cls{"));
            if (err) return err;

            err = self->out->write(self->out, relc->conc->name, relc->conc->name_size);
            if (err) return err;

            err = self->out->write(self->out, "{_rel{", strlen("{_rel{"));
            if (err) return err;

            err = self->out->write(self->out,
                                   reltype->attr->name,
                                   reltype->attr->name_size);
            

            err = self->out->write(self->out, "[", 1);
            if (err) return err;

            for (i = 0; i < KND_ID_BASE; i++) {
                idx = refset->idx[i];
                if (!idx) continue;

                for (j = 0; j < KND_ID_BASE; j++) {
                    term_idx = idx->idx[j];
                    if (!term_idx) continue;

                    for (ri = 0; ri < KND_ID_BASE; ri++) {
                        ref = term_idx->refs[ri];
                        if (!ref) continue;

                        err = self->out->write(self->out, "{", 1);
                        if (err) return err;

                        err = self->out->write(self->out, ref->obj_id, KND_ID_SIZE);
                        if (err) return err;
                        
                        err = self->out->write(self->out, "}", 1);
                        if (err) return err;
                    }
                    
                }
            }
            err = self->out->write(self->out, "]", 1);
            if (err) return err;
            

            err = self->out->write(self->out, "}}}}", 4);
            if (err) return err;
            
        next_reltype:
            reltype = reltype->next;
        }
        
        relc = relc->next;
    }
    */
    
    err = self->out->write(self->out, "}", 1);
    if (err) return err;
    
    return knd_OK;
}


/* export object */
static int 
kndObject_export(struct kndObject *self)
{
    int err;
    switch(self->format) {
    case KND_FORMAT_JSON:
        err = kndObject_export_JSON(self);
        if (err) return err;
        break;
        /*case KND_FORMAT_HTML:
        err = kndObject_export_HTML(self, is_concise);
        if (err) return err;
        break;*/
    case KND_FORMAT_GSC:
        err = kndObject_export_GSC(self);
        if (err) return err;
        break;
    default:
        break;
    }
    
    return knd_OK;
}


static int run_set_name(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndObject *self = (struct kndObject*)obj;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;
    int err;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "_impl", strlen("_impl"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }
    if (!name_size) return knd_FAIL;
    if (name_size >= KND_NAME_SIZE)
        return knd_LIMIT;

    memcpy(self->name, name, name_size);
    self->name_size = name_size;
    self->name[name_size] = '\0';

    if (DEBUG_OBJ_LEVEL_2)
        knd_log("++ OBJ NAME: \"%.*s\"", self->name_size, self->name);

    return knd_OK;
}

static int
kndObject_validate_attr(struct kndObject *self,
                        const char *name,
                        size_t name_size,
                        struct kndAttr **result)
{
    struct kndConcept *conc;
    struct kndAttr *attr = NULL;
    int err, e;

    if (DEBUG_OBJ_LEVEL_2)
        knd_log(".. validating \"%s\" obj elem: \"%.*s\"", self->name, name_size, name);

    conc = self->conc;

    err = conc->get_attr(conc, name, name_size, &attr);
    if (err) {
        knd_log("  -- ELEM \"%.*s\" not approved :(\n", name_size, name);
        self->log->reset(self->log);
        e = self->log->write(self->log, name, name_size);
        if (e) return e;
        e = self->log->write(self->log, " elem not confirmed",
                               strlen(" elem not confirmed"));
        if (e) return e;
        return err;
    }

    if (DEBUG_OBJ_LEVEL_2) {
        const char *type_name = knd_attr_names[attr->type];
        knd_log("++ \"%.*s\" ELEM \"%s\" attr type: \"%s\"",
                name_size, name, attr->name, type_name);
    }
    
    *result = attr;
    return knd_OK;
}

static int parse_elem(void *data,
                      const char *name, size_t name_size,
                      const char *rec, size_t *total_size)
{
    struct kndObject *self = (struct kndObject*)data;
    struct kndObject *obj;
    struct kndElem *elem = NULL;
    struct kndAttr *attr = NULL;
    struct kndRef *ref = NULL;
    struct kndText *text = NULL;
    int err;

    if (DEBUG_OBJ_LEVEL_2) {
        knd_log("..  validation of \"%s\" elem REC: \"%.*s\"\n",
                name, 16, rec);
    }
    err = kndObject_validate_attr(self, name, name_size, &attr);
    if (err) return err;

    err = kndElem_new(&elem);
    if (err) return err;
    elem->obj = self;
    elem->attr = attr;
    elem->root = self;
    elem->out = self->out;
    
    if (!self->tail) {
        self->tail = elem;
        self->elems = elem;
    }
    else {
        self->tail->next = elem;
        self->tail = elem;
    }
    self->num_elems++;

    if (DEBUG_OBJ_LEVEL_2)
        knd_log("   == basic elem type: %s\n",
                knd_attr_names[attr->type]);

    switch (attr->type) {
    case KND_ATTR_AGGR:
     
        err = kndConcept_alloc_obj(self->conc->root_class, &obj);
        if (err) return err;
        
        obj->type = KND_OBJ_AGGR;
        obj->conc = attr->conc;
        obj->out = self->out;
        obj->log = self->log;
        err = obj->parse(obj, rec, total_size);
        if (err) return err;

        elem->aggr = obj;
        obj->parent = elem;
        return knd_OK;
    case KND_ATTR_ATOM:
    case KND_ATTR_NUM:
    case KND_ATTR_FILE:
    case KND_ATTR_CALC:
        break;
    case KND_ATTR_REF:
        err = kndRef_new(&ref);
        if (err) return err;
        ref->elem = elem;
        ref->out = self->out;
        err = ref->parse(ref, rec, total_size);
        if (err) goto final;

        elem->ref = ref;
        return knd_OK;
    case KND_ATTR_TEXT:
        err = kndText_new(&text);
        if (err) return err;

        text->elem = elem;
        text->out = self->out;

        //chunk_size = 0;
        /*err = text->parse(text, c, &chunk_size);
        if (err) return err;
        */
        elem->text = text;
        
        //*total_size = chunk_size;
        return knd_OK;
    default:
        break;
    }

    err = elem->parse(elem, rec, total_size);
    if (err) goto final;

    return knd_OK;

 final:
    elem->del(elem);
    return err;
}

/* parse object */
static int 
kndObject_parse_GSL(struct kndObject *self,
                    const char *rec,
                    size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = self
        },
        { .type = KND_CHANGE_STATE,
          .name = "elem",
          .name_size = strlen("elem"),
          .is_validator = true,
          .buf = buf,
          .buf_size = &buf_size,
          .max_buf_size = KND_NAME_SIZE,
          .validate = parse_elem,
          .obj = self
        }
    };
    int err;

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;
    
    return knd_OK;
}


static int 
kndObject_contribute(struct kndObject *self,
                     size_t  matchpoint_num,
                     size_t orig_pos)
{
    struct kndMatchPoint *mp;
    //struct kndMatchResult *res;
    //float score;
    int idx_pos, err;

    if (!self->num_matchpoints) return knd_OK;

    if ((matchpoint_num) > self->num_matchpoints) return knd_OK;
    
    /*if (self->cache->repo->match_state > self->match_state) {
        self->match_state = self->cache->repo->match_state;
        memset(self->matchpoints, 0, sizeof(struct kndMatchPoint) * self->num_matchpoints);
        self->match_score = 0;
        self->match_idx_pos = -1;
    }
    */
    
    mp = &self->matchpoints[matchpoint_num];

    if (mp->orig_pos) {
        knd_log("  .. this matchpoint was already covered by another unit?\n");
        err = knd_FAIL;
        goto final;
    }
    
    mp->score = KND_MATCH_MAX_SCORE;
    self->match_score += mp->score;
    mp->orig_pos = orig_pos;
    
    self->average_score = (float)self->match_score / (float)self->max_score;

    /*knd_log("   == \"%s\": matched in %lu!    SCORE: %.2f [%lu:%lu]\n",
            self->name,
            (unsigned long)matchpoint_num,
            self->average_score,
            (unsigned long)self->match_score,
            (unsigned long)self->max_score); */

    if (self->average_score >= KND_MATCH_SCORE_THRESHOLD) {

        knd_log("   ++ \"%s\": matching threshold reached: %.2f!\n",
                self->name, self->average_score);

        if (self->match_idx_pos >= 0)
            idx_pos = self->match_idx_pos;
        else {

            /*if (self->cache->num_matches > KND_MAX_MATCHES) {
                knd_log("  -- results buffer limit reached :(\n");
                return knd_FAIL;
            }
                
            idx_pos = self->cache->num_matches;
            self->match_idx_pos = idx_pos;
            self->cache->matches[idx_pos] = self;
            self->cache->num_matches++; */
        }

    }
    
    err = knd_OK;
 final:
    
    return err;
}

static int 
kndObject_match(struct kndObject *self,
                const char *rec,
                size_t     rec_size)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    
    struct kndObject *obj;
    struct kndElem *elem;
    struct kndOutput *out;
    int err;

    /*    self->cache->num_matches = 0; */
    
    /* elem with linear seq */
    err = kndElem_new(&elem);
    if (err) goto final;

    elem->obj = self;
    
    err = elem->match(elem, rec, rec_size);
    if (err) goto final;

    /*qsort(self->cache->matches,
          self->cache->num_matches,
          sizeof(struct kndObject*),
          knd_compare_obj_by_match_descend);
    */
    
    knd_log("== MATCH RESULTS:");

    out = self->out;

    err = out->write(out, "[", 1);
    if (err) goto final;

    /*for (size_t i = 0; i < self->cache->num_matches; i++) {
        obj = self->cache->matches[i];

        buf_size = sprintf(buf, "{%s {score %.2f}}",
                           obj->name, obj->average_score);

        knd_log("%s\n", buf);

        err = out->write(out, buf, buf_size);
        if (err) goto final;

        if (i >= KND_MATCH_MAX_RESULTS) break;
    }
    */
    err = out->write(out, "]", 1);
    if (err) goto final;
    
    err = knd_OK;

 final:
    return err;
}



static int 
kndObject_resolve(struct kndObject *self)
{
    struct kndElem *elem;
    int err;

    if (DEBUG_OBJ_LEVEL_3) {
        if (self->type == KND_OBJ_ADDR) {
            knd_log(".. resolve OBJ %.*s::%.*s [%.*s]",
                    self->conc->name_size, self->conc->name,
                    self->name_size, self->name,
                    KND_ID_SIZE, self->id);
        } else {
            knd_log(".. resolve aggr elem \"%.*s\" %.*s..",
                    self->parent->attr->name_size, self->parent->attr->name,
                    self->conc->name_size, self->conc->name);
        }
    }
    
    for (elem = self->elems; elem; elem = elem->next) {
        elem->log = self->log;
        err = elem->resolve(elem);
        if (err) return err;
    } 
    
    return knd_OK;
}

static int 
kndObject_sync(struct kndObject *self)
{
    char idbuf[KND_ID_SIZE];
    struct kndElem *elem;
    struct kndConcept *conc;
    struct kndRepoCache *cache;
    struct kndObject *obj;
    struct kndRefSet *refset;
    struct kndSortTag *tag;
    struct kndSortAttr *attr;
    struct kndSortAttr *a;
    struct kndObjRef *ref;
    int err;

    /*if (DEBUG_OBJ_LEVEL_TMP) {
        if (!self->root) {
            knd_log("\n    !! syncing primary OBJ %s::%s\n",
                    self->id, self->name);
        }
        else {
            knd_log("    .. syncing aggr obj %s..\n",
                    self->parent->name);
        }
        }
    */
    
    elem = self->elems;
    while (elem) {
        /* resolve refs of aggr obj */
        if (elem->aggr) {
            obj = elem->aggr;
            
            knd_log("    .. syncing aggr obj in %s..\n",
                    elem->attr->name);

            while (obj) {
                err = obj->sync(obj);
                if (err) return err;
                obj = obj->next;
            }
            
            goto next_elem;
        }
        
        if (!elem->attr) goto next_elem;
        if (elem->attr->type != KND_ATTR_REF)
            goto next_elem;
        
        conc = elem->attr->conc;
        if (!conc) goto next_elem;

        /*
        if (elem->ref_class)
            conc = elem->ref_class;
        
        if (DEBUG_OBJ_LEVEL_TMP)
            knd_log("\n    .. sync expanding ELEM REF: %s::%s..\n",
                    conc->name,
                    elem->states->val);

        obj = elem->states->refobj;
        if (!obj) {
            err = self->cache->repo->get_cache(self->cache->repo, conc, &cache);
            if (err) return knd_FAIL;

            err = self->cache->name_idx->lookup_name(self->cache->name_idx,
                                                     self->name, self->name_size,
                                                     self->name, self->name_size, idbuf);
            if (err) {
                if (DEBUG_OBJ_LEVEL_TMP)
                    knd_log("  -- failed to sync expand ELEM REF: %s::%s :(\n",
                        conc->name,
                        elem->states->val);
                goto next_elem;
            }
            elem->states->refobj = obj;
        }
        */
        
        next_elem:
        elem = elem->next;
    }

    if (!self->tag) {
        if (DEBUG_OBJ_LEVEL_3)
            knd_log("    -- obj %s:%s is not meant for browsing\n",
                    self->id, self->name);
        return knd_OK;
    }

  
    for (size_t i = 0; i < self->tag->num_attrs; i++) {
        attr = self->tag->attrs[i];
        
        /*err = kndObject_get_idx(self,
                                (const char*)attr->name,
                                attr->name_size, &refset);
        if (err) {
            knd_log("  -- no refset %s :(\n", attr->name);
            return err;
        }
        */
        
        err = kndObjRef_new(&ref);
        if (err) return err;
        
        ref->obj = self;
        if (self->root)
            ref->obj = self->root;
        
        memcpy(ref->obj_id, ref->obj->id, KND_ID_SIZE);
        ref->obj_id_size = KND_ID_SIZE;
    
        memcpy(ref->name, ref->obj->name, ref->obj->name_size);
        ref->name_size = ref->obj->name_size;

        err = kndSortTag_new(&tag);
        if (err) return err;

        ref->sorttag = tag;

        a = malloc(sizeof(struct kndSortAttr));
        if (!a) return knd_NOMEM;
        memset(a, 0, sizeof(struct kndSortAttr));
        a->type = attr->type;
    
        memcpy(a->name, attr->name, attr->name_size);
        a->name_size = attr->name_size;
            
        memcpy(a->val, attr->val, attr->val_size);
        a->val_size = attr->val_size;

        tag->attrs[tag->num_attrs] = a;
        tag->num_attrs++;

        /* subordinate objs not included to AZ */
        if (self->is_subord) {
            if (!strcmp(attr->name, "AZ")) {
                if (DEBUG_OBJ_LEVEL_3)
                    knd_log("  -- %s:%s excluded from AZ\n", self->id, self->name);
                continue;
            }
        }
        
        if (DEBUG_OBJ_LEVEL_TMP)
            knd_log("  .. add ref: %s %p", ref->obj_id, refset);

        err = refset->add_ref(refset, ref);
        if (DEBUG_OBJ_LEVEL_TMP)
            knd_log("  .. result: %d", err);
        if (err) {
            if (DEBUG_OBJ_LEVEL_TMP) {
                ref->str(ref, 1);
                knd_log("  -- ref \"%s\" not added to refset :(\n", ref->obj_id);
            }
            return err;
        }
        
    }
    
    
    return knd_OK;
}


extern void
kndObject_init(struct kndObject *self)
{
    self->del = del;
    self->str = str;

    //self->flatten = kndObject_flatten;
    self->match = kndObject_match;
    self->contribute = kndObject_contribute;

    self->parse = kndObject_parse_GSL;
    self->resolve = kndObject_resolve;
    self->export = kndObject_export;
    self->sync = kndObject_sync;
}

extern int
kndObject_new(struct kndObject **obj)
{
    struct kndObject *self;

    self = malloc(sizeof(struct kndObject));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndObject));
    
    kndObject_init(self);
    *obj = self;

    return knd_OK;
}
