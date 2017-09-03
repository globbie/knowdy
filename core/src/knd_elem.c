#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/stat.h>

#include "knd_concept.h"
#include "knd_repo.h"
#include "knd_elem.h"
#include "knd_attr.h"
#include "knd_object.h"
#include "knd_text.h"
#include "knd_refset.h"
#include "knd_sorttag.h"

#include "knd_output.h"
#include "knd_user.h"
#include "knd_parser.h"

#define DEBUG_ELEM_LEVEL_1 0
#define DEBUG_ELEM_LEVEL_2 0
#define DEBUG_ELEM_LEVEL_3 0
#define DEBUG_ELEM_LEVEL_4 0
#define DEBUG_ELEM_LEVEL_TMP 1


static int
index_atom(struct kndElem *self,
           struct kndObject *obj);

static int
index_ref(struct kndElem *self);


static void del(struct kndElem *self)
{

    free(self);
}


static void str(struct kndElem *self, size_t depth)
{
    size_t offset_size = sizeof(char) * KND_OFFSET_SIZE * depth;
    char *offset = malloc(offset_size + 1);

    struct kndObject *obj;
    struct kndElem *elem;
    struct kndElemState *elem_state;
    struct kndText *text;
    
    memset(offset, ' ', offset_size);
    offset[offset_size] = '\0';

    if (self->states && self->states->val_size)
        knd_log("%s%s => %s", offset, self->attr->name, self->states->val);

    if (self->aggr) {
        if (self->is_list) {
            knd_log("%s   inline LIST\n",
                    offset);
            
            obj = self->aggr;
            while (obj) {
                obj->str(obj, depth + 1);
                obj = obj->next;
            }
        }
        else {
            knd_log("%s%s:",
                    offset, self->attr->name);
            self->aggr->str(self->aggr, depth + 1);
        }
    }

    switch (self->attr->type) {
    case KND_ATTR_REF:
        self->ref->str(self->ref, depth + 1);
        return;
    case KND_ATTR_TEXT:
        text = self->text;
        text->str(text, depth + 1);
        return;
        /*case KND_ATTR_STR:
        elem_state = self->states;
        while (elem_state) {
            knd_log("%s  STR -> %s [#%.*s]\n", offset,
                    elem_state->val,
                    KND_STATE_SIZE, elem_state->state);
            elem_state = elem_state->next;
        }
        return;
    case KND_ATTR_NUM:
        elem_state = self->states;
        while (elem_state) {
            knd_log("%s    NUM -> \"%s\" [#%.*s]", offset,
                    elem_state->val,
                    KND_STATE_SIZE, elem_state->state);
            elem_state = elem_state->next;
        }
        return; */
    case KND_ATTR_FILE:
        elem_state = self->states;
        while (elem_state) {
            knd_log("%s  FILE -> %s [#%lu]\n", offset,
                    elem_state->val,
                    (unsigned long)elem_state->state);
            elem_state = elem_state->next;
        }
        return;
    default:
        break;
    }
}

/*
static int
kndElem_register_seq(struct kndElem *self,
                     struct ooDict *idx,
                     char *seq,
                     size_t seq_size,
                     size_t seqnum)
{
    struct kndLinearSeqRec *rec;
    struct kndRefSet *refset;
    struct kndObjRef *objref;
    struct kndSortTag *tag;
    struct kndSortAttr *attr;
    int err;
    
    // knd_log("    .. SEQ \"%s\" [pos: %lu]\n",
    //  seq, (unsigned long)seqnum);
    
    rec = idx->get(idx, seq);
    if (!rec) {
        //knd_log("    == first occurence of SEQ \"%s\"\n",
        //  seq);

        rec = malloc(sizeof(struct kndLinearSeqRec));
        if (!rec) {
            err = knd_NOMEM;
            goto final;
        }
        memset(rec, 0, sizeof(struct kndLinearSeqRec));

        err = kndRefSet_new(&refset);
        if (err) goto final;
        memcpy(refset->name, seq, seq_size);
        refset->name_size = seq_size;
        rec->refset = refset;

        err = idx->set(idx,  seq, (void*)rec);
        if (err) goto final;
    }
    else {
        //knd_log("    == known SEQ \"%s\"!\n",
        //  seq);
        refset = rec->refset;
        //refset->str(refset, 1, 6);
    }

    
    // build ref
    err = kndObjRef_new(&objref);
    if (err) goto final;
        
    objref->obj = self->obj;
    memcpy(objref->obj_id, self->obj->id, KND_ID_SIZE);
    objref->obj_id_size = KND_ID_SIZE;
    
    memcpy(objref->name, self->obj->name, self->obj->name_size);
    objref->name_size = self->obj->name_size;

    // sorting by LINEAR POS
    err = kndSortTag_new(&tag);
    if (err) return err;

    attr = malloc(sizeof(struct kndSortAttr));
    if (!attr) return knd_NOMEM;

    memset(attr, 0, sizeof(struct kndSortAttr));
    attr->type = KND_FACET_CATEGORICAL;

    memcpy(attr->name, "L", 1);
    attr->name_size = 1;

    attr->val_size = sprintf(attr->val,
                             "P%lu", (unsigned long)seqnum);
    attr->numval = seqnum;
    
    tag->attrs[0] = attr;
    tag->num_attrs = 1;

    objref->sorttag = tag;

    err = refset->add_ref(refset, objref);
    if (err) goto final;
    
    err = knd_OK;

 final:
    return err;
}
*/

static int
kndElem_index_list_aggr(struct kndElem *self)
{
    //char buf[KND_NAME_SIZE];
    //size_t buf_size;

    struct kndObject *obj;
    struct kndElem *elem;
    
    //const char *c;
    //const char *b;
    int i = 0;
    int err = knd_FAIL;

    if (DEBUG_ELEM_LEVEL_2)
        knd_log("\n\n  ELEM:    .. indexing LIST of aggr objs \"%s\"..\n",
                self->states->val);
    
    obj = self->aggr;
    while (obj) {
        elem = obj->elems;
        
        while (elem) {

            elem->tag = self->tag;
            
            if (DEBUG_ELEM_LEVEL_2) {
                knd_log("\nELEM:   obj %d) aggr elem idx: \"%s\"  TYPE: %d TAG: %p\n", i,
                    elem->attr->idx_name,
                        elem->attr->type,
                        elem->tag);
                elem->str(elem, 1);
            }
            
            if (!elem->attr->idx_name_size)
                goto next_elem;

            switch (elem->attr->type) {
            case KND_ATTR_TEXT:
                err = elem->text->index(elem->text);
                if (err) return err;
                break;
            case KND_ATTR_ATOM:
                elem->out = self->out;
                err = index_atom(elem, self->aggr);
                if (err) return err;
                break;
            case KND_ATTR_REF:
                /*err = kndElem_index_ref(elem);
                  if (err) return err;*/
                break;
            default:
                break;
            }
            
            
        next_elem:
            elem = elem->next;
        }
        
        i++;
        obj = obj->next;
    }

    return knd_OK;
}

/*
static int
kndElem_index_list(struct kndElem *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;

    struct ooDict *idx = NULL;
    //struct kndMatchPoint *mp;
    struct kndObject *obj;
    struct kndElem *elem;
    
    //const char *c;
    //const char *b;

    //bool in_seq = false;
    
    size_t seqnum = 0;
    long accented = -1;
    
    int err = knd_FAIL;

    
    //knd_log("    .. indexing LIST ELEM \"%s\"..\n",
    //  self->val);

    if (!strcmp(self->attr->idx_name, "Attr"))
        return kndElem_index_list_aggr(self);

    // TODO: linear SEQ idx
    //idx = self->obj->cache->linear_seq_idx;

    obj = self->aggr;
    while (obj) {
        elem = obj->elems;
        buf_size = 0;
        
        while (elem) {
            memcpy(buf + buf_size, elem->states->val, elem->states->val_size);
            buf_size += elem->states->val_size;
            
            elem = elem->next;
        }
        buf[buf_size] = '\0';
        
        if (buf[buf_size - 1] == '!') {
            buf[buf_size - 1] = '\0';
            buf_size -= 1;
        }
        
        //knd_log("  idx SEQ %lu: \"%s\"\n",
        //        (unsigned long)seqnum, buf);


        // fixme: idx is always null.
        err = kndElem_register_seq(self, idx, buf, buf_size, seqnum);
        if (err) goto final;
        
        seqnum++;
        obj = obj->next;
    }

    // integral body of matching points
    self->obj->matchpoints = malloc(sizeof(struct kndMatchPoint) * seqnum);
    if (!self->obj->matchpoints) return knd_NOMEM;

    memset(self->obj->matchpoints, 0, sizeof(struct kndMatchPoint) * seqnum);
    self->obj->num_matchpoints = seqnum;
    self->obj->max_score = KND_MATCH_MAX_SCORE * self->obj->num_matchpoints; 


    //knd_log("  == NUM matchpoints: %lu\n",
    //        (unsigned long)self->obj->num_matchpoints);

    if (accented >= 0)
        self->obj->accented = accented; // fixme
    
    err = knd_OK;

 final:
    return err;
}
*/

static int index_ref(struct kndElem *self)
{
    struct kndConcept *conc, *bc;
    struct kndRefSet *refset;
    struct kndObjRef *objref;
    struct kndSortTag *tag;
    struct kndRelClass *relc;
    struct kndRelType *reltype;
    struct ooDict *idx;
    //const char *classname;
    //bool browse_level = 0;
    
    int err;

    if (!self->attr) return knd_FAIL;

    conc = self->attr->conc;
    if (!conc) return knd_FAIL;

    if (DEBUG_ELEM_LEVEL_3)
        knd_log("    .. index REF: %s::%s   REF bound IDX: \"%s\"  BROWSE LEVEL: %d\n",
                conc->name,
                self->states->val, self->attr->idx_name,  self->attr->browse_level);

    if (self->attr->browse_level) {
        if (DEBUG_ELEM_LEVEL_3)
            knd_log("\n\n    NB: this obj should be excluded from default browsing!\n\n\n");

        self->obj->is_subord = true;
    }
    

    /* add ref */
    err = kndObjRef_new(&objref);
    if (err) return err;
        
    objref->obj = self->obj;

    /* aggr obj */
    if (self->obj->parent)
        objref->obj = self->obj->parent->obj;

    memcpy(objref->obj_id, objref->obj->id, KND_ID_SIZE);
    objref->obj_id_size = KND_ID_SIZE;
    
    memcpy(objref->name, objref->obj->name, objref->obj->name_size);
    objref->name_size = objref->obj->name_size;
        
    err = kndSortTag_new(&tag);
    if (err) return err;
    objref->sorttag = tag;

    if (DEBUG_ELEM_LEVEL_2)
        knd_log("\n    .. add REF to \"%s\"..\n",
                objref->obj_id);
    
    err = refset->term_idx(refset, objref);
    if (err) return err;
    
    err = refset->add_ref(refset, objref);
    if (err) return err;

    if (DEBUG_ELEM_LEVEL_TMP)
        knd_log("\n    ++ REF to \"%s\" OK!!\n",
                objref->obj_id);

    return knd_OK;
}


static int kndElem_set_full_name(struct kndElem *self,
                                 char *name,
                                 size_t *name_size)
{
    //struct kndElem *elem;
    //struct kndObject *obj;
    char *s = name;
    size_t chunk_size = *name_size;
    int err;
    
    if (self->obj && self->obj->parent) {
        err = kndElem_set_full_name(self->obj->parent,
                              name, name_size);
        if (err) return err;

        s = name + (*name_size);
        
        memcpy(s, "_", 1);
        s++;
        chunk_size++;
    }
    
    memcpy(s, self->states->val, self->states->val_size);
    chunk_size += self->states->val_size;
    *name_size += chunk_size;
    return knd_OK;
}

static int index_atom(struct kndElem *self,
                      struct kndObject *obj __attribute__((unused)))
{
    struct kndSortTag *tag = NULL;
    struct kndSortAttr *attr;
    int err;

    if (DEBUG_ELEM_LEVEL_TMP)
        knd_log("\n    .. index ATOM: %s   VAL: %s\n",
                self->attr->name,
                self->states->val);

    attr = malloc(sizeof(struct kndSortAttr));
    if (!attr) return knd_NOMEM;
    memset(attr, 0, sizeof(struct kndSortAttr));

    /* default type */
    attr->type = KND_FACET_ATOMIC;

    err = kndElem_set_full_name(self, attr->name, &attr->name_size);
    if (err) return err;
    
    knd_log("ELEM full name: \"%s\" [%lu]\n",
            attr->name,
            (unsigned long)attr->name_size);

    /* special types */
    if (self->attr->idx_name_size) {
        if (!strcmp(self->attr->idx_name, "Accu")) {
            attr->type = KND_FACET_ACCUMULATED;
        }
        
        if (!strcmp(self->attr->idx_name, "Category")) {
            attr->type = KND_FACET_CATEGORICAL;
        }
        
        if (!strcmp(self->attr->idx_name, "Topic")) {
            attr->type = KND_FACET_CATEGORICAL;
        }
    }
    
    memcpy(attr->val, self->states->val, self->states->val_size);
    attr->val_size = self->states->val_size;

    knd_log("    INDEX ATOM:  %s => \"%s\" [type: %d]\n",
            attr->name, attr->val, attr->type);

    /* do not index simple atomic values   TODO! */
    /*if (attr->type == KND_FACET_ATOMIC) {
        free(attr);
        return knd_OK;
        }*/
    
    tag = self->tag;
    if (!tag) {
        knd_log("  -- no sort tag found for \"%s\"\n", self->states->val);
        return knd_FAIL;
    }

    tag->attrs[tag->num_attrs] = attr;
    tag->num_attrs++;
    
    err = knd_OK;

    return err;
}


static int
kndElem_index(struct kndElem *self)
{
    int err;
    
    if (DEBUG_ELEM_LEVEL_3)
        knd_log("    .. indexing ELEM \"%s\"  attr: \"%s\"..\n",
                self->states->val,
                self->attr->name);

    if (self->is_list) {
        /*if (self->attr->idx_name_size) { */

        err = kndElem_index_list_aggr(self);
        if (err) goto final;

    } else {
        /*if (self->attr->conc && self->attr->conc->idx_name_size) {*/

        if (self->attr->conc) {
            self->aggr->tag = self->tag;
            /*err = self->aggr->index_inline(self->aggr);
            if (err) goto final;
            */
            return knd_OK;
        }
        
    }

    switch (self->attr->type) {
    case KND_ATTR_TEXT:
        err = self->text->index(self->text);
        if (err) goto final;
        break;
    case KND_ATTR_ATOM:
        if (self->attr->idx_name_size) {
            err = index_atom(self, NULL);
            return err;
        }
        break;
    case KND_ATTR_REF:
        err = index_ref(self);
        return err;
        
    default:
        break;
    }
    
    err = knd_OK;
    
final:
    return err;
}

static int
kndElem_contribute_refset(struct kndElem *self __attribute__((unused)),
                          struct kndLinearSeqRec *rec,
                          size_t seqnum)
{
    struct kndFacet *facet;
    struct kndRefSet *refset;
    //struct kndObjRef *objref;

    int err;

    /* no facets */
    if (!rec->refset->num_facets) {
        rec->refset->numval = seqnum;
        err = rec->refset->contribute(rec->refset, seqnum);
        goto final;
    }

    for (size_t i = 0; i < rec->refset->num_facets; i++) {
        facet = rec->refset->facets[i];
        /*knd_log("Facet: %s [numval: %lu]\n", facet->name,
                (unsigned long)facet->numval);
        */
        for (size_t j = 0; j < facet->num_refsets; j++) {
            refset = facet->refsets[j];
            if (refset->numval > (seqnum + 1)) break;

            /*knd_log("  == RefSet: %s [numval: %lu] total: %lu\n", refset->name,
              (unsigned long)refset->numval, (unsigned long)refset->num_refs); */
            err = refset->contribute(refset, seqnum);
            if (err) goto final;
        }

    }

    err = knd_OK;

final:
    return err;
}

static int
kndElem_intersect(struct kndElem *self,
                  struct ooDict *idx,
                  const char *seq,
                  size_t seq_size,
                  size_t seqnum)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    size_t UTF_val;
    
    struct kndLinearSeqRec *rec;
    int err;

    knd_log("\n    .. intersect SEQ \"%s\" [pos: %lu]\n",
            seq, (unsigned long)seqnum);
    
    rec = idx->get(idx, seq);
    if (rec) {
        rec->refset->str(rec->refset, 0, 10);
        
        err = kndElem_contribute_refset(self, rec, seqnum);
        if (err) goto final;
    }

    /* try syll split */
    err = knd_read_UTF8_char(seq, seq_size,
                             &UTF_val, &buf_size);
    if (err) goto final;

    /* a vowel was removed? */
    if ((seq_size - buf_size) > 1) {
        memcpy(buf, seq, buf_size);
        memcpy(buf + buf_size, "#", 1);
        buf[buf_size + 1] = '\0';
        
        knd_log("\n    .. with vowel dropped: \"%s\"\n",
                buf);

        rec = idx->get(idx, (const char*)buf);
        if (rec) {

            rec->refset->str(rec->refset, 0, 10);

            err = kndElem_contribute_refset(self, rec, seqnum);
            if (err) goto final;
        }
    }
    
    err = knd_OK;
    
 final:
    return err;
}

static int
kndElem_match(struct kndElem *self,
              const char *rec,
              size_t rec_size __attribute__((unused)))
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;

    struct ooDict *idx = NULL;
    
    const char *c;
    const char *b;

    //bool in_seq = false;
    
    size_t seqnum = 0;
    int err = knd_FAIL;
    
    knd_log("    .. Elem LinearSeq MATCH..\n");

    c = rec;
    b = c;
    /*
    idx = self->obj->cache->linear_seq_idx;
    */
    
    while (*c) {
        switch (*c) {
        case '|':
            buf_size = c - b;
            memcpy(buf, b, buf_size);
            buf[buf_size] = '\0';

            // fixme: idx is always null.
            err = kndElem_intersect(self, idx, buf, buf_size, seqnum);
            if (err) goto final;

            b = c + 1;
            seqnum++;
            break;
        default:
            break;
        }
        c++;
    }

    /* last seq */
    buf_size = c - b;
    if (buf_size) {
        memcpy(buf, b, buf_size);
        buf[buf_size] = '\0';

        err = kndElem_intersect(self, idx, buf, buf_size, seqnum);
        if (err) goto final;
    }
    
    err = knd_OK;

 final:
    return err;
}

static int
kndElem_export_JSON(struct kndElem *self,
                    bool is_concise __attribute__((unused)))
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    char pathbuf[KND_TEMP_BUF_SIZE];
    //size_t pathbuf_size;

    char dirname[KND_NAME_SIZE];
    size_t dirname_size;

    //struct ooDict *idx;
    //struct kndRefSet *refset;
    struct kndObject *obj;

    //struct kndObject *refobj;

    struct kndElem *elem;
    struct kndText *text;
    struct kndRef *ref;

    struct stat linkstat;
    struct kndOutput *out = self->out;
    size_t curr_size;
    //unsigned long numval;
    int err;

    if (self->aggr) {
        if (self->is_list) {
            buf_size = sprintf(buf, "\"%s_l\":[",
                               self->states->val);
            err = out->write(out, buf, buf_size);
            if (err) return err;


            obj = self->aggr;
            while (obj) {
                obj->out = out;
                err = obj->export(obj);

                if (obj->next) {
                    err = out->write(out, ",", 1);
                    if (err) return err;
                }

                obj = obj->next;
            }

            err = out->write(out, "]", 1);
            if (err) return err;

            return knd_OK;
        }

        /* single anonymous aggr obj */
        err = out->write(out, "\"", 1);
        if (err) goto final;
        err = out->write(out, self->attr->name, self->attr->name_size);
        if (err) goto final;
        err = out->write(out, "\":", strlen("\":"));
        if (err) goto final;
        
        self->aggr->out = out;
        err = self->aggr->export(self->aggr);
        
        return err;
    }

    /* attr name */
    err = out->write(out, "\"", 1);
    if (err) goto final;
    err = out->write(out, self->attr->name, self->attr->name_size);
    if (err) goto final;
    err = out->write(out, "\":", strlen("\":"));
    if (err) goto final;

    /* key:value repr */
    switch (self->attr->type) {
    case KND_ATTR_NUM:
        err = out->write(out, self->states->val, self->states->val_size);
        if (err) goto final;
        return knd_OK;
    case KND_ATTR_STR:
    case KND_ATTR_BIN:
        err = out->write(out, "\"", 1);
        if (err) goto final;
        err = out->write(out, self->states->val, self->states->val_size);
        if (err) goto final;
        err = out->write(out, "\"", 1);
        if (err) goto final;
        return knd_OK;
    default:
        break;
    }

    /* nested repr */
    err = out->write(out, "{", 1);
    if (err) goto final;

    curr_size = out->buf_size;

    if (self->attr) {
        switch (self->attr->type) {
        case  KND_ATTR_TEXT:
            text = self->text;
            text->out = out;
            err = text->export(text, KND_FORMAT_JSON);
            if (err) goto final;
            break;
            /*case KND_ATTR_FILE:
            err = out->write(out, "\"file\":\"", strlen("\"file\":\""));
            if (err) goto final;
            err = out->write(out,
                                   self->states->val, self->states->val_size);
            if (err) goto final;
            err = out->write(out, "\"", 1);
            if (err) goto final;
            */
            /* soft link the actual file */
            /* TODO: remove reader
                               buf_size = sprintf(buf, "%s/%s",
                               self->obj->cache->repo->user->reader->webpath,
                               self->states->val); */

            /*dirname_size = sprintf(dirname,
                                   "%s/%s", self->obj->cache->repo->path,
                                   self->obj->cache->baseclass->name);
            knd_make_id_path(pathbuf,
                             dirname,
                             self->obj->id, self->states->val);
            
            if (DEBUG_ELEM_LEVEL_2)
                knd_log("SOFT LINK: %s -> %s\n", buf, pathbuf);

            err = lstat(buf, &linkstat);
            if (err) {
                err = symlink((const char*)pathbuf, (const char*)buf);
                if (err) {
                    if (DEBUG_ELEM_LEVEL_TMP)
                        knd_log("  -- soft link failed: %d :(\n", err);
                    return err;
                }
            }
            
            if (self->states) {
                buf_size = sprintf(buf, ",\"_st\":%lu",
                                   (unsigned long)self->states->state);
                err = out->write(out, buf, buf_size);
                if (err) goto final;
            }
            
            break; */
        case KND_ATTR_REF:
            ref = self->ref;
            ref->out = out;
            err = ref->export(ref, KND_FORMAT_JSON);
            if (err) goto final;
            break;
        case KND_ATTR_CALC:
            if (DEBUG_ELEM_LEVEL_TMP)
                knd_log("  .. CALC elem!\n");

            buf_size = sprintf(buf, "\"val\":\"%s\"",
                               self->states->val);
            err = out->write(out, buf, buf_size);
            if (err) goto final;
            break;
        default:
            break;
        }
    }
    else {
        if (self->states) {
            buf_size = sprintf(buf, "\"val\":\"%s\"",
                               self->states->val);
            err = out->write(out, buf, buf_size);
            if (err) goto final;
        }
    }
    

final:

    return err;
}


static int 
kndElem_export_GSL(struct kndElem *self)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    //char pathbuf[KND_TEMP_BUF_SIZE];
    //size_t pathbuf_size;

    //struct kndElem *elem;
    struct kndElemState *elem_state;

    int err;
    
    buf_size = sprintf(buf, "{%s ", self->states->val);
    err = self->out->write(self->out, buf, buf_size);
    if (err) goto final;

    if (self->attr) {
        /*if (self->attr->type == KND_ATTR_TEXT) {
            text = self->text;
            text->str(text, depth + 1);
            }*/

        /*if (self->attr->type == KND_ATTR_ATOM) {
            elem_state = self->states;
            while (elem_state) {
                knd_log("%s  ATOM -> %s [#%lu]\n", offset,
                        elem_state->val,
                        (unsigned long)elem_state->state);
                elem_state = elem_state->next;
            }
            }*/

        if (self->attr->type == KND_ATTR_REF) {
            elem_state = self->states;

            if (elem_state) {
                err = self->out->write(self->out,
                                       elem_state->val, elem_state->val_size);
                if (err) goto final;
            }
        }
    }


    err = self->out->write(self->out, "}", 1);
    if (err) goto final;


    
    /* TODO: nested ELEMS */

 final:

    return err;
}


static int 
kndElem_export_list_GSC(struct kndElem *self)
{
    struct kndObject *obj;
    struct kndElem *elem;
    bool is_concise = true;
    int err;
    
    err = self->out->write(self->out, "[", 1);
    if (err) return err;

    err = self->out->write(self->out, self->attr->name, self->attr->name_size);
    if (err) return err;

    if (self->aggr) {
        obj = self->aggr;
        while (obj) {
            obj->out = self->out;
            obj->format = KND_FORMAT_GSC;
            err = obj->export(obj);
            if (err) return err;
            obj = obj->next;
        }
    }
    
    err = self->out->write(self->out, "]", 1);
    if (err) return err;

    return knd_OK;
}


static int 
kndElem_export_GSC(struct kndElem *self)
{
    struct kndElemState *elem_state;
    int err;

    if (DEBUG_ELEM_LEVEL_2)
        knd_log(".. GSC export elem \"%s\"..", self->attr->name);

    if (self->is_list)
        return kndElem_export_list_GSC(self);

    if (self->aggr) {
        self->aggr->out = self->out;
        self->aggr->format =  KND_FORMAT_GSC;
        err = self->aggr->export(self->aggr);
        if (err) {
            knd_log("-- aggr obj export failed :(");
            return err;
        }
        return knd_OK;
    }

    err = self->out->write(self->out, "{", 1);
    if (err) return err;
    
    err = self->out->write(self->out, self->attr->name, self->attr->name_size);
    if (err) return err;

    if (self->attr) {
        if (self->attr->type == KND_ATTR_TEXT) {
            self->text->out = self->out;
            err = self->text->export(self->text, KND_FORMAT_GSC);
            if (err) return err;
        }

        if (self->attr->type == KND_ATTR_REF) {
            self->ref->out = self->out;
            err = self->ref->export(self->ref, KND_FORMAT_GSC);
            if (err) return err;
        }
        
        if (self->attr->type == KND_ATTR_ATOM) {
            elem_state = self->states;

            err = self->out->write(self->out, " ", 1);
            if (err) return err;

            if (elem_state && elem_state->val_size) {
                //knd_remove_nonprintables(elem_state->val);

                err = self->out->write(self->out,
                                       elem_state->val, elem_state->val_size);
                if (err) return err;
            }
            else {
                err = self->out->write(self->out,
                                       "?", 1);
                if (err) return err;
            }
        }

        if (self->attr->type == KND_ATTR_NUM) {
            elem_state = self->states;

            err = self->out->write(self->out, " ", 1);
            if (err) return err;

            if (elem_state && elem_state->val_size) {
                //knd_remove_nonprintables(elem_state->val);

                err = self->out->write(self->out,
                                       elem_state->val, elem_state->val_size);
                if (err) return err;
            }
            else {
                err = self->out->write(self->out,
                                       "0", 1);
                if (err) return err;
            }
        }

        
        if (self->attr->type == KND_ATTR_CALC) {
            elem_state = self->states;

            err = self->out->write(self->out, " ", 1);
            if (err) return err;

            if (elem_state && elem_state->val_size) {
                err = self->out->write(self->out,
                                       elem_state->val, elem_state->val_size);
                if (err) return err;
            }
            else {
                err = self->out->write(self->out,
                                       "?", 1);
                if (err) return err;
            }
        }

        if (self->attr->type == KND_ATTR_FILE) {
            elem_state = self->states;

            err = self->out->write(self->out, " ", 1);
            if (err) return err;

            if (elem_state && elem_state->val_size) {
                knd_remove_nonprintables(elem_state->val);

                err = self->out->write(self->out,
                                       elem_state->val, elem_state->val_size);
                if (err) return err;
            }
            else {
                err = self->out->write(self->out,
                                       "?", 1);
                if (err) return err;
            }
        }

        
    } else {
        elem_state = self->states;

        if (elem_state && elem_state->val_size) {
            err = self->out->write(self->out, " ", 1);
            if (err) return err;

            err = self->out->write(self->out,
                                   elem_state->val, elem_state->val_size);
            if (err) return err;
        }
        else {
            err = self->out->write(self->out,
                                   " ?", 2);
            if (err) return err;
        }

    }
    
    err = self->out->write(self->out, "}", 1);
    if (err) return err;

    return knd_OK;
}


static int 
kndElem_export(struct kndElem *self,
               knd_format format,
               bool is_concise)
{
    int err;
    
    switch(format) {
    case KND_FORMAT_JSON:
        err = kndElem_export_JSON(self, is_concise);
        if (err) return err;
        break;
        /*case KND_FORMAT_HTML:
        err = kndElem_export_HTML(self, is_concise);
        if (err) return err;
        break;*/
    case KND_FORMAT_GSL:
        err = kndElem_export_GSL(self);
        if (err) return err;
        break;
    case KND_FORMAT_GSC:
        err = kndElem_export_GSC(self);
        if (err) return err;
        break;
    default:
        break;
    }
    
    return knd_OK;
}


static int run_set_val(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndElem *self = (struct kndElem*)obj;
    struct kndTaskArg *arg;
    struct kndElemState *state;
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
    if (val_size >= KND_VAL_SIZE) return knd_LIMIT;
    
    state = malloc(sizeof(struct kndElemState));
    if (!state) return knd_NOMEM;
    memset(state, 0, sizeof(struct kndElemState));
    self->states = state;
    self->num_states = 1;


    /* TODO: validate if needed */

    
    if (DEBUG_ELEM_LEVEL_2) {
        switch (self->attr->type) {
        case KND_ATTR_STR:
            knd_log("++ ELEM STR val of class %.*s: \"%.*s\"",
                    self->attr->name_size, self->attr->name, val_size, val);
            break;
        case KND_ATTR_BIN:
            knd_log("++ ELEM BIN val of class %.*s: \"%.*s\"",
                    self->attr->name_size, self->attr->name, val_size, val);
            break;
        default:
            break;
        }
    }
    
    memcpy(state->val, val, val_size);
    state->val[val_size] = '\0';
    state->val_size = val_size;

    if (DEBUG_ELEM_LEVEL_2)
        knd_log("++ ELEM VAL: \"%.*s\"", state->val_size, state->val);

    return knd_OK;
}

static int parse_GSL(struct kndElem *self,
                     const char *rec,
                     size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    int err;
    
    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_val,
          .obj = self
        }
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;
    
    return knd_OK;
}

static int
kndElem_resolve(struct kndElem *self)
{
    struct kndObject *obj;
    struct kndElem *elem;
    struct kndElemState *elem_state;
    struct kndText *text;
    int err;
    
    if (self->aggr) {
        for (obj = self->aggr; obj; obj = obj->next) {
            obj->log = self->log;
            err = obj->resolve(obj);
            if (err) return err;
        }
    }

    switch (self->attr->type) {
    case KND_ATTR_REF:
        self->ref->log = self->log;
        err = self->ref->resolve(self->ref);
        if (err) return err;
    default:
        break;
    }
    
    return knd_OK;
}


extern void
kndElem_init(struct kndElem *self)
{
    self->del = del;
    self->str = str;
    self->parse = parse_GSL;

    self->resolve = kndElem_resolve;
    self->index = kndElem_index;
    self->match = kndElem_match;
    self->export = kndElem_export;
}

extern int
kndElem_new(struct kndElem **obj)
{
    struct kndElem *self;

    self = malloc(sizeof(struct kndElem));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndElem));

    kndElem_init(self);

    *obj = self;

    return knd_OK;
}
