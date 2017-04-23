
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/stat.h>

#include "knd_dataclass.h"
#include "knd_elem.h"
#include "knd_attr.h"
#include "knd_object.h"
#include "knd_text.h"
#include "knd_refset.h"
#include "knd_sorttag.h"

#include "../data_writer/knd_data_writer.h"
#include "../data_reader/knd_data_reader.h"

#include "knd_output.h"
#include "knd_user.h"

#define DEBUG_ELEM_LEVEL_1 0
#define DEBUG_ELEM_LEVEL_2 0
#define DEBUG_ELEM_LEVEL_3 0
#define DEBUG_ELEM_LEVEL_4 0
#define DEBUG_ELEM_LEVEL_TMP 1


static int
kndElem_index_atom(struct kndElem *self,
                   struct kndObject *obj);

static int
kndElem_index_ref(struct kndElem *self);

static int
kndElem_parse_ref(struct kndElem *self,
                  const char *rec,
                  size_t *total_size);


static void
kndElem_del(struct kndElem *self)
{
    struct kndElem *elem, *curr_elem;

    elem = self->elems;
    while (elem) {
        curr_elem = elem;
        elem = elem->next;

        kndElem_del(curr_elem);
    }

    free(self);
}


static int
kndElem_str(struct kndElem *self, size_t depth)
{
    size_t offset_size = sizeof(char) * KND_OFFSET_SIZE * depth;
    char *offset = malloc(offset_size + 1);
    struct kndObject *obj;
    struct kndElem *elem;
    struct kndElemState *elem_state;
    struct kndText *text;
    
    memset(offset, ' ', offset_size);
    offset[offset_size] = '\0';

    if (self->is_list) 
        knd_log("%s[\"%s\"\n",
            offset, self->name);
    else
        knd_log("%sELEM \"%s\"\n",
                offset, self->name);

    if (self->inner) {
        if (self->is_list) {
            
            knd_log("%s   inline LIST\n",
                    offset);
            
            obj = self->inner;
            while (obj) {
                obj->str(obj, depth + 1);

                obj = obj->next;
            }
        }
        else {
            knd_log("%s   inline OBJ, root:%p\n",
                    offset, self->root);
            self->inner->str(self->inner, depth + 1);
        }

    }

    if (self->baseclass && self->baseclass->attr) {
        
        if (self->baseclass->attr->type == KND_ELEM_TEXT) {
            text = self->text;
            text->str(text, depth + 1);
        }

        if (self->baseclass->attr->type == KND_ELEM_ATOM) {
            elem_state = self->states;
            while (elem_state) {
                knd_log("%s  ATOM -> %s [#%lu]\n", offset,
                        elem_state->val,
                        (unsigned long)elem_state->state);
                elem_state = elem_state->next;
            }
        }

        if (self->baseclass->attr->type == KND_ELEM_FILE) {
            elem_state = self->states;
            while (elem_state) {
                knd_log("%s  FILE -> %s [#%lu]\n", offset,
                        elem_state->val,
                        (unsigned long)elem_state->state);
                elem_state = elem_state->next;
            }
        }

        if (self->baseclass->attr->type == KND_ELEM_REF) {
            elem_state = self->states;
            while (elem_state) {
                knd_log("%s  REF -> %s:%s:%s => %p\n", offset,
                        /*self->baseclass->attr->classname,*/
                        self->baseclass->attr->dataclass->name,
                        self->refclass_name,
                        elem_state->val,
                        elem_state->refobj);

                elem_state = elem_state->next;
            }
        }
        
    }
    
    elem = self->elems;
    while (elem) {
        elem->str(elem, depth + 1);
        elem = elem->next;
    }

    if (self->is_list)
        knd_log("%s]\n",
                offset);

    return knd_OK;
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
kndElem_index_list_inner(struct kndElem *self)
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
        knd_log("\n\n  ELEM:    .. indexing LIST of inner objs \"%s\"..\n",
                self->name);
    
    obj = self->inner;
    while (obj) {
        elem = obj->elems;
        
        while (elem) {

            elem->tag = self->tag;
            
            if (DEBUG_ELEM_LEVEL_2) {
                knd_log("\nELEM:   obj %d) inner elem idx: \"%s\"  TYPE: %d TAG: %p\n", i,
                    elem->baseclass->attr->idx_name,
                        elem->baseclass->attr->type,
                        elem->tag);
                elem->str(elem, 1);
            }
            
            if (!elem->baseclass->attr->idx_name_size)
                goto next_elem;

            switch (elem->baseclass->attr->type) {
            case KND_ELEM_TEXT:
                err = elem->text->index(elem->text);
                if (err) return err;
                break;
            case KND_ELEM_ATOM:
                elem->out = self->out;
                err = kndElem_index_atom(elem, self->inner);
                if (err) return err;
                break;
            case KND_ELEM_REF:
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
    //  self->name);

    if (!strcmp(self->baseclass->idx_name, "Attr"))
        return kndElem_index_list_inner(self);

    // TODO: linear SEQ idx
    //idx = self->obj->cache->linear_seq_idx;

    obj = self->inner;
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

static int
kndElem_index_ref(struct kndElem *self)
{
    struct kndDataClass *dc, *bc;
    struct kndRepoCache *cache = NULL;
    struct kndRefSet *refset;
    struct kndObjRef *objref;
    struct kndSortTag *tag;
    struct kndRelClass *relc;
    struct kndRelType *reltype;
    struct ooDict *idx;
    //const char *classname;
    //bool browse_level = 0;
    
    int err;

    if (!self->baseclass->attr) return knd_FAIL;

    dc = self->baseclass->attr->dataclass;
    if (!dc) return knd_FAIL;

    if (DEBUG_ELEM_LEVEL_3)
        knd_log("    .. index REF: %s::%s   REF bound IDX: \"%s\"  BROWSE LEVEL: %d\n",
                dc->name,
                self->states->val, self->baseclass->attr->idx_name,  self->baseclass->attr->browse_level);

    if (self->baseclass->attr->browse_level) {
        if (DEBUG_ELEM_LEVEL_3)
            knd_log("\n\n    NB: this obj should be excluded from default browsing!\n\n\n");

        self->obj->is_subord = true;
    }
    
    err = self->obj->cache->repo->get_cache(self->obj->cache->repo, dc, &cache);
    if (err) return knd_FAIL;
    
    bc = self->obj->cache->baseclass;

    if (DEBUG_ELEM_LEVEL_2)
        knd_log(" .. RELCLASS \"%s\" points to \"%s\"\n", bc->name, dc->name);
    
    relc = cache->rel_classes;
    while (relc) {
        if (relc->dc == bc) break;
        relc = relc->next;
    }

    /* add a relclass */
    if (!relc) {
        relc = malloc(sizeof(struct kndRelClass));
        if (!relc) return knd_NOMEM;

        memset(relc, 0, sizeof(struct kndRelClass));
        
        relc->dc = bc;
        
        relc->next = cache->rel_classes;
        cache->rel_classes = relc;
    }

    if (DEBUG_ELEM_LEVEL_2)
        knd_log(" .. RELCLASS \"%s\" points to \"%s\"\n",
                bc->name, dc->name);
    
    reltype = relc->rel_types;
    while (reltype) {
        if (reltype->attr == self->baseclass->attr) break;

        reltype = reltype->next;
    }

    /* add a reltype */
    if (!reltype) {
        reltype = malloc(sizeof(struct kndRelType));
        if (!reltype) return knd_NOMEM;

        reltype->attr = self->baseclass->attr;

        err = ooDict_new(&reltype->idx, KND_MEDIUM_DICT_SIZE);
        if (err) return knd_NOMEM;
        
        reltype->next = relc->rel_types;
        relc->rel_types = reltype;
    }

    if (DEBUG_ELEM_LEVEL_2)
        knd_log(" .. got RELTYPE \"%s\"!\n", reltype->attr->name);

    idx = reltype->idx;

    refset = idx->get(idx, (const char*)self->states->val);
    if (!refset) {
        if (DEBUG_ELEM_LEVEL_3)
            knd_log("    == first rec of RELated obj \"%s\" (CLASS: %s)!\n",
                    self->states->val, dc->name);
            
        err = kndRefSet_new(&refset);
        if (err) return err;
            
        memcpy(refset->name, self->states->val, self->states->val_size);
        refset->name_size = self->states->val_size;
            
        err = idx->set(idx,  (const char*)self->states->val, (void*)refset);
        if (err) return err;
    }
    
    /*else {
        if (DEBUG_ELEM_LEVEL_TMP)
            refset->str(refset, 1, 5);
            }*/

    /* add ref */
    err = kndObjRef_new(&objref);
    if (err) return err;
        
    objref->obj = self->obj;

    /* inner obj */
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


static int
kndElem_set_full_name(struct kndElem *self,
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
    
    memcpy(s, self->name, self->name_size);
    chunk_size += self->name_size;

    
    *name_size += chunk_size;
    
    return knd_OK;
}

static int
kndElem_index_atom(struct kndElem *self,
                   struct kndObject *obj __attribute__((unused)))
{
    struct kndSortTag *tag = NULL;
    struct kndSortAttr *attr;
    int err;

    if (DEBUG_ELEM_LEVEL_TMP)
        knd_log("\n    .. index ATOM: %s   VAL: %s\n",
                self->baseclass->name,
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
    if (self->baseclass->attr->idx_name_size) {
        if (!strcmp(self->baseclass->attr->idx_name, "Accu")) {
            attr->type = KND_FACET_ACCUMULATED;
        }
        
        if (!strcmp(self->baseclass->attr->idx_name, "Category")) {
            attr->type = KND_FACET_CATEGORICAL;
        }
        
        if (!strcmp(self->baseclass->attr->idx_name, "Topic")) {
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
        knd_log("  -- no sort tag found for \"%s\"\n", self->name);
        return knd_FAIL;
    }

    tag->attrs[tag->num_attrs] = attr;
    tag->num_attrs++;
    
    err = knd_OK;

    return err;
}

/*
static int
kndElem_index_container(struct kndElem *self)
{
    struct kndRefSet *refset;
    struct kndObjRef *objref;
    struct kndSortTag *tag;
    struct ooDict *idx;
    int err;

    knd_log("   .. index CONTAINER: %s\n", self->states->val);

    idx = self->obj->cache->contain_idx;

    refset = (struct kndRefSet*)idx->get(idx, (const char*)self->states->val);
    if (!refset) {
        err = kndRefSet_new(&refset);
        if (err) goto final;
            
        memcpy(refset->name, self->states->val, self->states->val_size);
        refset->name_size = self->states->val_size;
            
        err = idx->set(idx,  (const char*)self->states->val, (void*)refset);
        if (err) goto final;
    }
    
    err = kndObjRef_new(&objref);
    if (err) goto final;
        
    objref->obj = self->obj;
    memcpy(objref->obj_id, self->obj->id, KND_ID_SIZE);
    objref->obj_id_size = KND_ID_SIZE;
    
    memcpy(objref->name, self->obj->name, self->obj->name_size);
    objref->name_size = self->obj->name_size;
        
    err = kndSortTag_new(&tag);
    if (err) return err;
    objref->sorttag = tag;
        
    err = refset->add_ref(refset, objref);
    if (err) goto final;

 final:
    return err;
}
*/

static int
kndElem_index(struct kndElem *self)
{
    //struct kndElem *elem;
    //struct kndText *text;
    //struct ooDict *idx;

    //struct kndAttr *elem_attr;

    //struct kndRefSet *refset;
    //struct kndObjRef *objref;
    //struct kndSortTag *tag;
    int err;
    
    if (!self->baseclass) return knd_FAIL;
    
    if (DEBUG_ELEM_LEVEL_3)
        knd_log("    .. indexing ELEM \"%s\"  baseclass: \"%s\"..\n",
                self->name,
                self->baseclass->attr_name);

    if (self->is_list) {
        /*if (self->baseclass->idx_name_size) { */

        err = kndElem_index_list_inner(self);
        if (err) goto final;

    } else {

        /*if (self->baseclass->dc && self->baseclass->dc->idx_name_size) {*/

        if (self->baseclass->dc) {
            self->inner->tag = self->tag;
            err = self->inner->index_inline(self->inner);
            if (err) goto final;

            return knd_OK;
        }
        
    }

    
    if (!self->baseclass->attr) return knd_OK;

    switch (self->baseclass->attr->type) {
    case KND_ELEM_TEXT:
        err = self->text->index(self->text);
        if (err) goto final;
        break;
    case KND_ELEM_ATOM:

        if (self->baseclass->attr->idx_name_size) {
            err = kndElem_index_atom(self, NULL);
            return err;
        }
        
        break;
    case KND_ELEM_REF:
        err = kndElem_index_ref(self);
        return err;
        
        /*    case KND_ELEM_CONTAINER:
        err = kndElem_index_container(self);
        if (err) goto final;
        */
        
    default:
        break;
    }
    
    /*elem = self->elems;
    while (elem) {
        elem->str(elem, depth + 1);
        elem = elem->next;
    }
    */
    
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
kndElem_get_elemclass(struct kndElem *self,
                      const char *name,
                      size_t name_size __attribute__((unused)),
                      struct kndDataElem **elem)
{
    struct kndAttr *attr;
    struct kndDataElem *de = NULL;
    struct kndDataClass *dc = NULL;
    int err;

    if (self->obj->dc) {
        dc = self->obj->dc;
    }
    else if (self->parent) {
        de = self->parent->elems;
        if (self->parent->attr) {
            attr = self->parent->attr;
            if (attr->dataclass) {
                dc = attr->dataclass;
            }
        }
    }
    else {
        dc = self->obj->cache->baseclass;
    }

    /*dc->str(dc, 1);*/
    
    dc->rewind(dc);

    do {
        err = dc->next_elem(dc, &de);
        if (!de) break;

        
        if (!strcmp(de->name, name)) {
            if (DEBUG_ELEM_LEVEL_3)
                knd_log("  ++ elem %s confirmed: %s!\n",
                        name, de->attr_name);
            *elem = de;
            return knd_OK;
        }
    } while (de);

    
    return knd_FAIL;
}

static int
kndElem_check_type(struct kndElem *self,
                   const char *rec,
                   size_t name_size,
                   struct kndDataElem **result)
{
    struct kndDataElem *de = NULL;
    int err;
    
    self->name_size = name_size;
    memcpy(self->name, rec, name_size);
    self->name[name_size] = '\0';
    
    /* check elem name validity */
    err = kndElem_get_elemclass(self,
                                (const char*)self->name, name_size,
                                &de);
    if (err) {
        knd_log("  -- data ELEM \"%s\" not approved :(\n", self->name);
        return err;
    }
    
    *result = de;
    
    return knd_OK;
}


static int
kndElem_parse_list(struct kndElem *self,
                   const char *rec,
                   size_t *total_size)
{
    //char buf[KND_NAME_SIZE];
    size_t buf_size;

    struct kndObject *obj = NULL;
    //struct kndElem *elem = NULL;
    struct kndDataElem *de = NULL;
    const char *c;
    const char *b;

    //bool in_item = false;
    bool in_name = true;
    //bool in_idx = true;

    size_t chunk_size;
    int err;

    c = rec;
    b = c;

    if (DEBUG_ELEM_LEVEL_3)
        knd_log("  .. elem list parse from: \"%s\"\n\n",
                rec);
    
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
            if (in_name) {
                buf_size = c - b;

                /*self->name_size = buf_size;
                memcpy(self->name, b, buf_size);
                self->name[buf_size] = '\0';
                */

                buf_size = c - b;
                if (!buf_size) {
                    knd_log("  -- empty elem name in \"%s\"\n",
                            rec);
                    return knd_FAIL;
                }

                err = kndElem_check_type(self, b, buf_size, &de);
                if (err) goto final;
                self->baseclass = de;
                
                /*knd_log("LIST ITEM CLASS: \"%s\" type confirmed: %s\n",
                        self->name, self->baseclass->name);
                */
                
                in_name = false;
            }
            break;
        case '{':
            if (in_name) {
                buf_size = c - b;

                if (!buf_size) {
                    knd_log("  -- empty elem name in \"%s\"\n",
                            rec);
                    return knd_FAIL;
                }

                err = kndElem_check_type(self, b, buf_size, &de);
                if (err) goto final;
                
                /*knd_log("LIST ITEM CLASS: \"%s\" type confirmed!\n",
                        self->name);
                */
                
                self->baseclass = de;

                in_name = false;
            }

            if (DEBUG_ELEM_LEVEL_3)
                knd_log("LIST OBJ: \"%s\" %p\n",
                        self->baseclass->name, self->baseclass->dc);

            if (self->baseclass && self->baseclass->dc) {

                if (DEBUG_ELEM_LEVEL_3)
                    knd_log("  .. parsing inline obj \"%s\" from: \"%s\"\n",
                            self->baseclass->name, c);
                
                err = kndObject_new(&obj);
                if (err) goto final;

                obj->root = self->root;
                obj->parent = self;
                obj->dc = de->dc;
                obj->cache = self->obj->cache;
                obj->out = self->obj->out;
                
                c++;
                err = obj->parse_inline(obj, c, &chunk_size);
                if (err) goto final;

                if (!self->inner_tail) {
                    self->inner_tail = obj;
                    self->inner = obj;
                }
                else {
                    self->inner_tail->next = obj;
                    self->inner_tail = obj;
                }
                
                c += chunk_size;
                
            }
            
            break;
        case ']':

            
            *total_size = c - rec;
            return knd_OK;
        }
        c++;
    }

final:
    return err;
}



static int
kndElem_check_name(struct kndElem *self,
                   const char *b,
                   const char *c,
                   size_t *total_size)
{
    struct kndDataElem *de;
    struct kndObject *obj;
    struct kndText *text;

    size_t buf_size;
    size_t chunk_size = 0;
    int err;
    
    buf_size = c - b;
    if (!buf_size) {
        knd_log("  -- empty elem name in \"%s\"\n",
                b);
        return knd_FAIL;
    }
    
    err = kndElem_check_type(self, b, buf_size, &de);
    if (err) return err;
                
    self->baseclass = de;

    if (DEBUG_ELEM_LEVEL_3)
        knd_log("  ++ got elem class: \"%s\" attr: %s\n",
                de->name, de->attr_name);
    
    if (de->dc) {
        err = kndObject_new(&obj);
        if (err) return err;
        
        obj->parent = self;
        obj->root = self->root;
        obj->dc = de->dc;
        obj->cache = self->obj->cache;
        obj->out = self->out;
        
        err = obj->parse_inline(obj, c, &chunk_size);
        if (err) return err;
        self->inner = obj;
        
        *total_size = chunk_size;

        if (DEBUG_ELEM_LEVEL_3)
            knd_log("\n\n  -- end of inline OBJ! REMAINDER: %s\n\n", c);
        
        return knd_OK;
    }
                
    if (!de->attr)
        return knd_OK;
    
    if (DEBUG_ELEM_LEVEL_3)
        knd_log("   elem type: %s\n", knd_elem_names[de->attr->type]);

    switch (de->attr->type) {
    case KND_ELEM_ATOM:
    case KND_ELEM_FILE:
    case KND_ELEM_CALC:
        break;
    case KND_ELEM_REF:

        err = kndElem_parse_ref(self, c, &chunk_size);
        if (err) return err;

        *total_size = chunk_size;
        return knd_OK;

        break;
    case KND_ELEM_TEXT:
        err = kndText_new(&text);
        if (err) return err;

        text->elem = self;
        text->out = self->out;

        chunk_size = 0;
        err = text->parse(text, c, &chunk_size);
        if (err) return err;

        self->text = text;
        
        *total_size = chunk_size;
        return knd_OK;
    default:
        break;
    }
                
    return knd_OK;
}


static int
kndElem_parse_ref(struct kndElem *self,
                  const char *rec,
                  size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;

    struct kndElemState *elem_state;
    struct kndDataClass *parent_dc, *dc;

    const char *b;
    const char *c;

    size_t chunk_size = 0;
    //size_t curr_size = 0;
    int err = knd_FAIL;

    bool in_cls = false;
    bool in_name = false;
    bool in_obj_id = false;

    c = rec;
    b = c;

    if (DEBUG_ELEM_LEVEL_3)
        knd_log("  .. REF parse from: \"%s\"\n\n",
                rec);

    elem_state = malloc(sizeof(struct kndElemState));
    if (!elem_state)
        return knd_NOMEM;
    memset(elem_state, 0, sizeof(struct kndElemState));

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
            buf_size = c - b;
            if (buf_size >= KND_NAME_SIZE) break;
            
            memcpy(buf, b, buf_size);
            buf[buf_size] = '\0';
            
            if (!strcmp(buf, "c")) {
                in_cls = true;
                b = c + 1;
                break;
            }
            
            if (!strcmp(buf, "n")) {
                in_name = true;
                b = c + 1;
                break;
            }

            if (!strcmp(buf, "id")) {
                in_obj_id = true;
                b = c + 1;
                break;
            }

            break;
        case '{':
            b = c + 1;
            break;
        case '}':

            if (in_cls) {
                chunk_size = c - b;
                memcpy(self->refclass_name, b, chunk_size);
                self->refclass_name_size = chunk_size;

                if (self->obj->cache->repo->user->reader)
                    parent_dc = self->obj->cache->repo->user->reader->dc;
                else
                    parent_dc = self->obj->cache->repo->user->writer->dc;

                /* check classname */
                dc = (struct kndDataClass*)parent_dc->class_idx->get\
                    (parent_dc->class_idx,
                     self->refclass_name);

                if (!dc) {
                    if (DEBUG_ELEM_LEVEL_TMP)
                        knd_log("  .. classname \"%s\" is not valid...\n",
                                self->refclass_name);
                    return knd_FAIL;
                }

                self->refclass = dc;
                in_cls = false;
                break;
            }


            if (in_name) {
                chunk_size = c - b;
                if (!chunk_size) {
                    if (DEBUG_ELEM_LEVEL_TMP)
                        knd_log("  -- empty name in REF \"%s\" :(\n",
                                rec);
                    return knd_FAIL;
                }
                memcpy(elem_state->val, b, chunk_size);
                elem_state->val_size = chunk_size;

                in_name = false;
                break;
            }

            if (in_obj_id) {
                chunk_size = c - b;
                if (!chunk_size) {
                    if (DEBUG_ELEM_LEVEL_TMP)
                        knd_log("  -- empty obj id in REF \"%s\" :(\n",
                                rec);
                    return knd_FAIL;
                }
                memcpy(elem_state->ref, b, chunk_size);
                elem_state->ref_size = chunk_size;

                in_obj_id = false;
                break;
            }

            elem_state->state = self->obj->cache->repo->state;
            elem_state->next = self->states;
            self->states = elem_state;
            self->num_states++;

            *total_size = c - rec;
            return knd_OK;
        }
        c++;
    }


    if (elem_state) free(elem_state);

    return err;
}


static int
kndElem_parse(struct kndElem *self,
              const char *rec,
              size_t *total_size)
{
    //char buf[KND_NAME_SIZE];
    size_t buf_size;

    //struct kndDataElem *de = NULL;
    struct kndElem *elem = NULL;
    struct kndElemState *elem_state = NULL;

    const char *c;
    const char *b;

    bool in_body = false;
    bool in_name = false;

    bool in_elem = false;
    bool in_atomic_val = false;

    size_t chunk_size;
    int err = knd_FAIL;
    
    c = rec;
    b = c;

    if (DEBUG_ELEM_LEVEL_3)
        knd_log("   .. parsing elem REC: %s\n",
                rec);
    
    while (*c) {
        switch (*c) {
            /* non-whitespace char */
        default:
            if (!in_body) break;

            if (!in_atomic_val) {
                in_atomic_val = true;
                b = c;
                break;
            }
            
            break;
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            /* whitespace */
            if (!in_body) break;

            if (!in_name) {
                if (self->name_size) {

                    if (DEBUG_ELEM_LEVEL_3) {
                        knd_log("   list item name already set: %s\n",
                                self->name);
                        knd_log("  item attr: parent: %p baseclass: %p\n",
                                self->parent, self->baseclass);
                    }
                    in_name = true;
                    break;
                }

                chunk_size = 0;
                err = kndElem_check_name(self, b, c, &chunk_size);
                if (err) return err;

                if (chunk_size) {
                    c += chunk_size;
                    *total_size = c - rec;
                    
                    return knd_OK;
                }

                in_name = true;
                in_atomic_val = true;
                b = c + 1;
                break;
            }
            
            break;
        case '{':
            if (!in_body) {
                in_body = true;
                b = c + 1;
                break;
            }

            if (!in_name) {
                if (self->name_size) {
                    if (DEBUG_ELEM_LEVEL_3) {
                        knd_log("   list item name already set: %s\n",
                                self->name);
                        knd_log("  item attr: parent: %p baseclass: %p\n",
                                self->parent, self->baseclass);
                    }
                    in_name = true;
                    break;
                }

                chunk_size = 0;
                err = kndElem_check_name(self, b, c, &chunk_size);
                if (err) return err;

                if (chunk_size) {
                    c += chunk_size;
                    *total_size = c - rec;
                    return knd_OK;
                }

                in_name = true;
                break;
            }


            if (!in_elem) {
                err = kndElem_new(&elem);
                if (err) goto final;
                
                elem->parent = self->baseclass;
                elem->obj = self->obj;

                if (!self->baseclass) {
                    knd_log("   .. pass on the baseclass to list item: %s\n",
                            self->name);
                    elem->parent = self->parent;
                }

                err = elem->parse(elem, c, &chunk_size);
                if (err) goto final;

                if (!self->tail) {
                    self->tail = elem;
                    self->elems = elem;
                }
                else {
                    self->tail->next = elem;
                    self->tail = elem;
                }

                self->num_elems++;

                c += chunk_size;

                b = c + 1;

                if (DEBUG_ELEM_LEVEL_3)
                    knd_log(" ++ elem \"%s\" parsed OK, continue from: \"%s\"\n", elem->name, c);

                elem = NULL;
                in_elem = false;
                in_atomic_val = false;
                break;
            }

            break;
        case '}':

            if (in_atomic_val) {
                buf_size = c - b;

                /* check empty val? */
                if (!buf_size) {
                    err = knd_FAIL;
                    goto final;
                }

                if (DEBUG_ELEM_LEVEL_2)
                    knd_log("    == atomic VAL: %s [size: %lu]\n",
                            b, (unsigned long)buf_size);

                elem_state = malloc(sizeof(struct kndElemState));
                if (!elem_state) {
                    err = knd_NOMEM;
                    goto final;
                }

                memset(elem_state, 0, sizeof(struct kndElemState));

                memcpy(elem_state->val, b, buf_size);
                elem_state->val_size = buf_size;

                elem_state->state = self->obj->cache->repo->state;

                elem_state->next = self->states;
                self->states = elem_state;
                self->num_states++;

                in_atomic_val =  false;
                in_elem  =  false;
                in_name = false;
            }

            if (in_name) {
                if (DEBUG_ELEM_LEVEL_TMP) {
                    knd_log("  -- elem \"%s\" not complete :(\n",
                            self->name);
                }
                err = knd_FAIL;
                goto final;
            }


            if (DEBUG_ELEM_LEVEL_2)
                knd_log("  -- end of elem \"%s\"\n", self->name);

            *total_size = c - rec;

            return knd_OK;
        case '[':
            if (!in_name) {
                chunk_size = 0;
                err = kndElem_check_name(self, b, c, &chunk_size);
                if (err) return err;

                if (chunk_size) {
                    c += chunk_size;
                    *total_size = c - rec;

                    return knd_OK;
                }

                in_name = true;
                break;
            }

            err = kndElem_new(&elem);
            if (err) goto final;

            elem->parent = self->baseclass;
            elem->obj = self->obj;

            if (DEBUG_ELEM_LEVEL_3)
                knd_log("\n  == LIST ATTR: %s\n",
                        elem->parent->name, elem->parent->attr_name);

            c++;
            err = kndElem_parse_list(elem, c, &chunk_size);
            if (err) goto final;

            /*elem->next = self->elems;
            self->elems = elem;
            */
            if (!self->tail) {
                self->tail = elem;
                self->elems = elem;
            }
            else {
                self->tail->next = elem;
                self->tail = elem;
            }

            self->num_elems++;

            c += chunk_size;

            if (DEBUG_ELEM_LEVEL_3)
                knd_log("  remainder after LIST parse: \"%s\"\n\n", c);

            break;
        }
        c++;
    }

    err = knd_FAIL;

final:

    if (elem)
        elem->del(elem);

    return err;
}








static int
kndElem_update(struct kndElem *self,
               const char *rec,
               size_t *total_size)
{
    char buf[KND_LARGE_BUF_SIZE];
    size_t buf_size;

    //struct kndElem *elem = NULL;
    knd_elem_type type = 0; // fixme

    struct kndElemState *elem_state = NULL;
    //struct kndText *text;

    const char *c;
    const char *b;

    bool in_oper = false;
    bool in_state = false;
    bool in_val = false;
    bool in_set_val = false;
    //bool in_text = false;

    size_t chunk_size;
    long numval;
    int err = knd_FAIL;
    
    c = rec;
    b = c;

    if (self->baseclass && self->baseclass->attr)
        type = self->baseclass->attr->type;

    if (DEBUG_ELEM_LEVEL_TMP)
        knd_log("  .. ELEM update: \"%s\" [type: %d]\n", rec, type);

    if (type == KND_ELEM_TEXT) {
        err = self->text->update(self->text, rec, &chunk_size);
        if (err) goto final;

        c += chunk_size;
        
        *total_size = c - rec;
        return knd_OK;
    }


    if (type == KND_ELEM_LIST) {
        knd_log("  .. update list of elems..\n");
        return knd_OK;
    }
    
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
            break;
        case '#':
            if (!in_state) {
                in_state = true;
                b = c + 1;
                break;
            }
            break;
        case '=':
            if (!in_set_val) {
                in_set_val = true;
                b = c + 1;
                break;
            }
            break;
        case '{':
            if (!in_oper) {
                in_oper = true;
                b = c + 1;
                break;
            }

            b = c + 1;
            break;
        case '}':

            if (in_state) {
                buf_size = c - b;
                memcpy(buf, b, buf_size);
                buf[buf_size] = '\0';

                err = knd_parse_num((const char*)buf, &numval);
                if (err) goto final;
                
                if (DEBUG_ELEM_LEVEL_3)
                    knd_log("   == CURR STATE to confirm: %lu\n",
                            (unsigned long)numval);

                /* is the recent state greater? */
                if (self->states->state > (unsigned long)numval) {
                    knd_log("  -- current state is greater than this, please check the updates!\n");
                    err = knd_FAIL;
                    goto final;
                }

                /* alloc new state */
                elem_state = malloc(sizeof(struct kndElemState));
                if (!elem_state) {
                    err = knd_NOMEM;
                    goto final;
                }
                memset(elem_state, 0, sizeof(struct kndElemState));
                elem_state->state = self->obj->cache->repo->state;


                in_state = false;
                in_val = true;
                break;
            }

            if (in_set_val) {
                buf_size = c - b;
                memcpy(elem_state->val, b, buf_size);
                elem_state->val_size = buf_size;

                if (DEBUG_ELEM_LEVEL_3)
                    knd_log("   == SET VAL: %s\n", elem_state->val);

                elem_state->next = self->states;
                self->states = elem_state;
                self->num_states++;
                elem_state = NULL;
                in_set_val = false;
                break;
            }

            /*knd_log("  -- end of elem \"%s\"\n", self->name);*/

            *total_size = c - rec;

            return knd_OK;
        }
        c++;
    }

    err = knd_FAIL;

final:

    if (elem_state)
        free(elem_state);

    return err;
}

/*
static int 
kndElem_calc(struct kndElem *self,
             struct kndRefSet *refset,
             unsigned long *result)
{
    struct kndObjRef *ref;
    struct kndAttr *attr;
    struct kndElem *elem;
    long numval;
    unsigned long total = 0;
    int err;

    attr = self->baseclass->attr;
        
    if (DEBUG_ELEM_LEVEL_3)
        knd_log("  ... calc oper %s on elem %s..\n",
                attr->calc_oper, attr->calc_attr);
    
    for (size_t i = 0; i < refset->inbox_size; i++) {
        ref = refset->inbox[i];

        elem = ref->obj->elems;
        while (elem) {

            if (!elem->baseclass) goto next_elem;
            if (!elem->baseclass->attr) goto next_elem;
            
            if (!strcmp(attr->calc_attr, elem->baseclass->attr->name)) {
                err = knd_parse_num((const char*)elem->states->val, &numval);
                if (err) goto final;

                total += numval;
            }
        next_elem:
            elem = elem->next;
        }
        
    }

    *result = total;
    
    err = knd_OK;
 final:
    return err;
}
*/



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

    struct stat linkstat;

    size_t curr_size;
    //unsigned long numval;
    int err;

    if (self->inner) {
        if (self->is_list) {
            buf_size = sprintf(buf, "\"%s_l\":[",
                               self->name);
            err = self->out->write(self->out, buf, buf_size);
            if (err) return err;


            obj = self->inner;
            while (obj) {
                obj->out = self->out;
                err = obj->export(obj, KND_FORMAT_JSON, 0);

                if (obj->next) {
                    err = self->out->write(self->out, ",", 1);
                    if (err) return err;
                }

                obj = obj->next;
            }

            err = self->out->write(self->out, "]", 1);
            if (err) return err;

            return knd_OK;
        }

        /* single anonymous inner obj */
        buf_size = sprintf(buf, "\"%s\":",
                           self->name);

        err = self->out->write(self->out, buf, buf_size);
        if (err) goto final;

        self->inner->out = self->out;
        err = self->inner->export(self->inner, KND_FORMAT_JSON, 0);
        
        return err;
    }


    buf_size = sprintf(buf, "\"%s\":{",
                       self->name);

    err = self->out->write(self->out, buf, buf_size);
    if (err) goto final;

    curr_size = self->out->buf_size;

    if (self->baseclass && self->baseclass->attr) {

        switch (self->baseclass->attr->type) {
        case  KND_ELEM_TEXT:
            text = self->text;
            text->out = self->out;

            err = text->export(text, KND_FORMAT_JSON);
            if (err) goto final;
            
            break;
        case KND_ELEM_ATOM:

            knd_remove_nonprintables(self->states->val);

            buf_size = sprintf(buf, "\"val\":\"%s\"",
                               self->states->val);
            err = self->out->write(self->out, buf, buf_size);
            if (err) goto final;

            if (self->states->state) {
                buf_size = sprintf(buf, ",\"_st\":%lu",
                                   (unsigned long)self->states->state);
                err = self->out->write(self->out, buf, buf_size);
                if (err) goto final;
            }
            
            break;
        case KND_ELEM_FILE:

            err = self->out->write(self->out, "\"file\":\"", strlen("\"file\":\""));
            if (err) goto final;
            
            err = self->out->write(self->out,
                                   self->states->val, self->states->val_size);
            if (err) goto final;

            err = self->out->write(self->out, "\"", 1);
            if (err) goto final;

            
            /* soft link the actual file */
            buf_size = sprintf(buf, "%s/%s",
                               self->obj->cache->repo->user->reader->webpath,
                               self->states->val);

            dirname_size = sprintf(dirname,
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
            
            if (self->states->state) {
                buf_size = sprintf(buf, ",\"_st\":%lu",
                                   (unsigned long)self->states->state);
                err = self->out->write(self->out, buf, buf_size);
                if (err) goto final;
            }
            
            break;
        case KND_ELEM_REF:

            err = self->out->write(self->out, "\"ref\":\"", strlen("\"ref\":\""));
            if (err) goto final;

            if (self->refclass_name_size) {
                err = self->out->write(self->out, self->refclass_name,
                                       self->refclass_name_size);
                if (err) goto final;
                err = self->out->write(self->out, "/", 1);
                if (err) goto final;
            }

            if (self->states->ref_size) {
                err = self->out->write(self->out,
                                       self->states->ref,
                                       self->states->ref_size);
                if (err) goto final;
                err = self->out->write(self->out, ":", 1);
                if (err) goto final;
            }

            if (self->states->val_size) {
                err = self->out->write(self->out,
                                       self->states->val,
                                       self->states->val_size);
                if (err) goto final;
            }
            else {
                err = self->out->write(self->out,
                                       "?", 1);
                if (err) goto final;
            }

            err = self->out->write(self->out, "\"", 1);
            if (err) goto final;
            
            if (self->states->refobj) {
                obj = self->states->refobj;
                
                err = self->out->write(self->out, ",\"obj\":", strlen(",\"obj\":"));
                if (err) goto final;

                obj->out = self->out;
                obj->export_depth = self->obj->export_depth + 1;
                
                err = obj->export(obj, KND_FORMAT_JSON, 0);
                if (err) goto final;
            }
            
            if (self->states->state) {
                buf_size = sprintf(buf, ",\"_st\":%lu",
                                   (unsigned long)self->states->state);
                err = self->out->write(self->out, buf, buf_size);
                if (err) goto final;
            }

            break;
        case KND_ELEM_CALC:

            if (DEBUG_ELEM_LEVEL_TMP)
                knd_log("  .. CALC elem!\n");

            buf_size = sprintf(buf, "\"val\":\"%s\"",
                               self->states->val);
            err = self->out->write(self->out, buf, buf_size);
            if (err) goto final;


            
            /* idx = self->obj->cache->contain_idx;
            refset = (struct kndRefSet*)idx->get(idx, (const char*)self->obj->name);
            if (!refset) break;

 
            err = kndElem_calc(self, refset, &numval);
            if (err) goto final;
            
            buf_size = sprintf(buf, "\"calc\":\"%lu\"",
                               (unsigned long)numval);
            
            err = self->out->write(self->out, buf, buf_size);
            if (err) goto final;
            */
            break;
        default:
            break;
        }
    }
    else {
        if (self->states->val_size) {
            buf_size = sprintf(buf, "\"val\":\"%s\"",
                               self->states->val);
            err = self->out->write(self->out, buf, buf_size);
            if (err) goto final;
        }
    }
    
    
    /* ELEMS */
    if (self->elems) {
        
        if (self->out->buf_size > curr_size) {
            err = self->out->write(self->out, ",", 1);
            if (err) goto final;
        }

        if (self->is_list_item) {
            err = self->out->write(self->out, ",", 1);
            if (err) goto final;
        }
        
        if (!self->is_list) {
            err = self->out->write(self->out, "\"elems\":{", strlen("\"elems\":{"));
            if (err) goto final;
        }
    }
    
    elem = self->elems;
    while (elem) {
        elem->out = self->out;

        err = elem->export(elem, KND_FORMAT_JSON, 1);
        if (err) goto final;

        if (elem->next) {
            err = self->out->write(self->out, ",", 1);
            if (err) goto final;
        }

        elem = elem->next;
    }

    if (self->elems) {
        if (self->is_list) {
            err = self->out->write(self->out, "]", 1);
            if (err) goto final;
        }
        
        else {
            err = self->out->write(self->out, "}", 1);
            if (err) goto final;
        }
    }


    if (!self->is_list) {
        err = self->out->write(self->out, "}", 1);
        if (err) goto final;
    }
    
    /* optional */
    /*if (self->reltypes) {
        err = kndElem_export_backrefs_JSON(self);
        if (err) goto final;
    }*/

final:

    return err;
}





static int 
kndElem_export_HTML(struct kndElem *self,
                    bool is_concise __attribute__((unused)))
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    //char pathbuf[KND_TEMP_BUF_SIZE];
    //size_t pathbuf_size;

    //struct ooDict *idx;
    //struct kndRefSet *refset;
    struct kndObject *obj;
    
    //struct kndElem *elem;
    struct kndText *text;
    size_t curr_size;
    //unsigned long numval;
    int err = knd_FAIL;

    if (DEBUG_ELEM_LEVEL_3)
        knd_log("  .. export elem HTML: %s\n", self->name);
    
    if (self->inner) {
        if (self->is_list) {

            buf_size = sprintf(buf, "<DIV class=\"%s\">",
                               self->name);
            err = self->out->write(self->out, buf, buf_size);
            if (err) return err;

            obj = self->inner;
            while (obj) {
                
                obj->out = self->out;

                err = obj->export(obj, KND_FORMAT_HTML, 0);

                /*if (obj->next) {
                    err = self->out->write(self->out, ",", 1);
                    if (err) return err;
                    }*/
                
                obj = obj->next;
            }
            
            err = self->out->write(self->out, "</DIV>", strlen("</DIV>"));
            if (err) return err;

            return knd_OK;
        }

        /* single anonymous inner obj */
        buf_size = sprintf(buf, "<DIV class=\"oo-%s\">",
                           self->name);
        err = self->out->write(self->out, buf, buf_size);
        if (err) goto final;

        self->inner->out = self->out;
        err = self->inner->export(self->inner, KND_FORMAT_HTML, 0);

        err = self->out->write(self->out, "</DIV>", strlen("</DIV>"));
        if (err) goto final;

        return err;
    }


    /*if (self->obj->dc) {
        knd_log("   .. elem's parent OBJ style:\"%s\"\n",
                self->obj->dc->style_name);

        if (!strcmp(self->obj->dc->style_name, "PLAIN")) {
            err = self->out->write(self->out,
                                   "<P class=\"oo-plain-txt\">",
                                   strlen("<P class=\"oo-plain-txt\">"));
            if (err) goto final;
        }
    }
    else {
        buf_size = sprintf(buf, "<H3>%s</H3>",
                           self->name);
        err = self->out->write(self->out, buf, buf_size);
        if (err) goto final;
        } */


    
    curr_size = self->out->buf_size;

    if (self->baseclass && self->baseclass->attr) {

        switch (self->baseclass->attr->type) {
        case  KND_ELEM_TEXT:
            text = self->text;
            text->out = self->out;

            err = text->export(text, KND_FORMAT_HTML);
            if (err) goto final;
            
            break;
        case KND_ELEM_ATOM:

            knd_remove_nonprintables(self->states->val);

            buf_size = sprintf(buf, "%s",
                               self->states->val);
            err = self->out->write(self->out, buf, buf_size);
            if (err) goto final;

            /*if (self->states->state) {
                buf_size = sprintf(buf, ",\"_st\":%lu",
                                   (unsigned long)self->states->state);
                err = self->out->write(self->out, buf, buf_size);
                if (err) goto final;
                }*/
            
            break;
        case KND_ELEM_REF:

            /*buf_size = sprintf(buf, "\"ref\":\"%s\"",
                               self->states->val);
            err = self->out->write(self->out, buf, buf_size);
            if (err) goto final;
            */
            
            if (self->states->refobj) {
                /*err = self->out->write(self->out, ",\"obj\":", strlen(",\"obj\":"));
                if (err) goto final;
                */
                obj = self->states->refobj;
                obj->out = self->out;
                obj->export_depth = self->obj->export_depth + 1;

                err = obj->export(obj, KND_FORMAT_HTML, 0);
                if (err) goto final;
            }
            
            if (self->states->state) {
                buf_size = sprintf(buf, ",\"_st\":%lu",
                                   (unsigned long)self->states->state);
                err = self->out->write(self->out, buf, buf_size);
                if (err) goto final;
            }

            break;
        case KND_ELEM_CALC:
            /*idx = self->obj->cache->contain_idx;
            refset = (struct kndRefSet*)idx->get(idx, (const char*)self->obj->name);
            if (!refset) break;

 
            err = kndElem_calc(self, refset, &numval);
            if (err) goto final;
            
            buf_size = sprintf(buf, "%lu",
                               (unsigned long)numval);
            err = self->out->write(self->out, buf, buf_size);
            if (err) goto final;
            */
            break;
        case KND_ELEM_CONTAINER:
            /*buf_size = sprintf(buf, "\"cont\":\"%s\"",
                               self->states->val);
            err = self->out->write(self->out, buf, buf_size);
            if (err) goto final; */
            break;
        default:
            break;
        }
    }


    /*if (self->obj->dc) {
        knd_log("   .. elem's parent OBJ style:\"%s\"\n",
                self->obj->dc->style_name);

        if (!strcmp(self->obj->dc->style_name, "PLAIN")) {
            err = self->out->write(self->out,
                                   "</P>",
                                   strlen("</P>"));
            if (err) goto final;
        }
        } */

    
    /*elem = self->elems;
    while (elem) {
        elem->out = self->out;

        err = elem->export(elem, KND_FORMAT_HTML, 1);
        if (err) goto final;

        elem = elem->next;
        } */

    /* if (self->elems) {
        if (self->is_list) {
            err = self->out->write(self->out, "]", 1);
            if (err) goto final;
        }
        
        else {
            err = self->out->write(self->out, "}", 1);
            if (err) goto final;
        }
    }


    if (!self->is_list) {
        err = self->out->write(self->out, "}", 1);
        if (err) goto final;
    }
    */

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
    
    buf_size = sprintf(buf, "{%s ",
                       self->name);
    err = self->out->write(self->out, buf, buf_size);
    if (err) goto final;

    if (self->baseclass && self->baseclass->attr) {
        /*if (self->baseclass->attr->type == KND_ELEM_TEXT) {
            text = self->text;
            text->str(text, depth + 1);
            }*/

        /*if (self->baseclass->attr->type == KND_ELEM_ATOM) {
            elem_state = self->states;
            while (elem_state) {
                knd_log("%s  ATOM -> %s [#%lu]\n", offset,
                        elem_state->val,
                        (unsigned long)elem_state->state);
                elem_state = elem_state->next;
            }
            }*/

        if (self->baseclass->attr->type == KND_ELEM_REF) {
            elem_state = self->states;

            if (elem_state) {
                err = self->out->write(self->out,
                                       elem_state->val, elem_state->val_size);
                if (err) goto final;
            }
        }

        /*if (self->baseclass->attr->type == KND_ELEM_CONTAINER) {
            elem_state = self->states;
            while (elem_state) {
                knd_log("%s  CONTAIN -> %s [#%lu]\n", offset,
                        elem_state->val,
                        (unsigned long)elem_state->state);
                elem_state = elem_state->next;
            }

            }*/
        
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
    //char buf[KND_TEMP_BUF_SIZE];
    //size_t buf_size;
    struct kndObject *obj;
    int err;
    
    err = self->out->write(self->out, "[", 1);
    if (err) return err;

    err = self->out->write(self->out, self->name, self->name_size);
    if (err) return err;

    if (self->inner) {
        obj = self->inner;
        while (obj) {
            obj->out = self->out;
            err = obj->export(obj, KND_FORMAT_GSC, 0);
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
    //char buf[KND_TEMP_BUF_SIZE];
    //size_t buf_size;

    //char pathbuf[KND_TEMP_BUF_SIZE];
    //size_t pathbuf_size;

    struct kndElemState *elem_state;
    
    int err;

    if (self->is_list)
        return kndElem_export_list_GSC(self);

    if (self->inner) {
        self->inner->out = self->out;
        return self->inner->export(self->inner, KND_FORMAT_GSC, 0);
    }

    err = self->out->write(self->out, "{", 1);
    if (err) return err;
    
    err = self->out->write(self->out, self->name, self->name_size);
    if (err) return err;


    if (self->baseclass && self->baseclass->attr) {

        if (self->baseclass->attr->type == KND_ELEM_TEXT) {
            self->text->out = self->out;

            err = self->text->export(self->text,  KND_FORMAT_GSC);
            if (err) return err;
        }

        if (self->baseclass->attr->type == KND_ELEM_REF) {
            elem_state = self->states;

            if (self->refclass_name_size) {
                err = self->out->write(self->out,
                                       "{c ", strlen("{c "));
                err = self->out->write(self->out,
                                       self->refclass_name,
                                       self->refclass_name_size);
                if (err) return err;
                err = self->out->write(self->out, "}", 1);
                if (err) return err;
            }
            
            if (elem_state->refobj) {
                err = self->out->write(self->out,
                                       "{id ", strlen("{id "));
                err = self->out->write(self->out,
                                       elem_state->refobj->id, KND_ID_SIZE);
                if (err) return err;
                err = self->out->write(self->out, "}", 1);
                if (err) return err;
           } 
            
            if (elem_state->val_size) {
                err = self->out->write(self->out,
                                       "{n ", strlen("{n "));
                err = self->out->write(self->out,
                                       elem_state->val, elem_state->val_size);
                
                if (err) return err;
                err = self->out->write(self->out, "}", 1);
                if (err) return err;
            }
        }
        
        if (self->baseclass->attr->type == KND_ELEM_ATOM) {
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

        if (self->baseclass->attr->type == KND_ELEM_CALC) {
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

        if (self->baseclass->attr->type == KND_ELEM_FILE) {
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

        /*if (self->baseclass->attr->type == KND_ELEM_CONTAINER) {
            elem_state = self->states;
            while (elem_state) {
                knd_log("%s  CONTAIN -> %s [#%lu]\n", offset,
                        elem_state->val,
                        (unsigned long)elem_state->state);
                elem_state = elem_state->next;
            }

            }*/
        
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
    case KND_FORMAT_HTML:
        err = kndElem_export_HTML(self, is_concise);
        if (err) return err;
        break;
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


extern void
kndElem_init(struct kndElem *self)
{    
    self->del = kndElem_del;
    self->str = kndElem_str;
    self->parse = kndElem_parse;
    self->update = kndElem_update;
    self->index = kndElem_index;
    self->match = kndElem_match;
    self->parse_list = kndElem_parse_list;
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
