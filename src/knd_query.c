#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_query.h"
#include "knd_repo.h"
#include "knd_output.h"

#include "knd_data_reader.h"
#include "knd_dataclass.h"
#include "knd_object.h"
#include "knd_refset.h"
#include "knd_conc.h"
#include "knd_facet.h"

#define DEBUG_QUERY_LEVEL_1 0
#define DEBUG_QUERY_LEVEL_2 0
#define DEBUG_QUERY_LEVEL_3 0
#define DEBUG_QUERY_LEVEL_4 0
#define DEBUG_QUERY_LEVEL_5 0
#define DEBUG_QUERY_LEVEL_TMP 1


/*  Query Destructor */
static
void kndQuery_del(struct kndQuery *self)
{
    free(self);
}


static
void kndQuery_reset(struct kndQuery *self)
{
    struct kndQuery *q;
    int i, err;

    for (i = 0; i < self->num_children; i++) {
        q = self->children[i];
        q->del(q);
    }

    self->num_children = 0;
}


static
void kndQuery_sort_children(struct kndQuery *self)
{
    knd_log(" .. sorting Query children ..\n");
}



static int
kndQuery_lookup_coderefs(struct kndQuery *self,
                         struct kndQuery *q,
                         struct kndRefSet **result)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;

    struct kndRefSet *refset = NULL;
    struct kndConc *conc;
    struct kndCodeRef *coderef;
    
    struct kndRefSet *refsets[KND_MAX_CLAUSES];
    size_t num_refsets = 0;

    int err;

    coderef = q->coderefs;
    while (coderef) {

        coderef->out = self->out;
        self->out->reset(self->out);
        
        err = coderef->export(coderef, 0, KND_FORMAT_GSL);
        if (err) goto final;

        knd_log(".. lookup: %s\n", self->out->buf);
                
        /* get the conc proposition from IDX */
        /*conc = self->maze->conc_idx->get(self->maze->conc_idx,
                                          (const char*)self->out->buf);
        */
        
        if (!conc) {
            refset = NULL;
            err = knd_NO_MATCH;
            goto final;
        }
        
        refset = conc->refset;
        if (!refset) {
            err = knd_FAIL;
            goto final;
        }

        if (num_refsets >= KND_MAX_CLAUSES) {
            err = knd_LIMIT;
            goto final;
        }
                
        refsets[num_refsets] = refset;
        refset = NULL;
        num_refsets++;
                
        coderef = coderef->next;
    }


    if (!num_refsets) {
        err = knd_NO_MATCH;
        goto final;
    }
    
    if (num_refsets == 1) {
        *result = refsets[0];
        return knd_OK;
    }

    /* refsets intersection  */
    err = kndRefSet_new(&refset);
    if (err) goto final;
            
    memcpy(refset->name, "/", 1);
    refset->name_size = 1;


    err = refset->intersect(refset, refsets, num_refsets);
    if (err) goto final;

    *result = refset;
    return knd_OK;
    
 final:
    return err;
}


static int
kndQuery_execute(struct kndQuery *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    
    struct kndQuery *q;
    struct kndRefSet *parent_refset = NULL;
    struct kndRefSet *refset = NULL;

    struct kndRefSet *refsets[KND_MAX_CLAUSES];
    size_t num_refsets = 0;
    size_t num_objs;
    
    int i, err;

    if (DEBUG_QUERY_LEVEL_TMP)
        knd_log("     .. Execute Query Facet \"%s\"\n",
                self->facet_name);

    /* reset */
    self->cache->select_result = NULL;

    /* default result */
    if (!self->num_children) {
        refset = self->cache->browser;
        goto extract;
    }

    /* TODO: lookup intersected refsets */
    


    /* get refsets for each query clause */
    for (i = 0; i < self->num_children; i++) {
        q = self->children[i];

        if (DEBUG_QUERY_LEVEL_TMP)
            knd_log("     .. Child Query Clause \"%s\"\n",
                    q->facet_name);
        
        /* conceptual search? */
        switch (q->type) {
            /*case KND_QUERY_CONC:

            err = kndQuery_lookup_coderefs(self, q, &refset);
            if (err) return err;
            
            break;*/
            
        default:

            /*if (!strcmp(q->val, "/")) {
                refset = self->cache->browser;
                goto extract;
                }*/

            /* TODO: try looking up the cache */
            if (parent_refset) {
                
            }

                
            err = self->cache->browser->find(self->cache->browser,
                                             q->facet_name,
                                             q->val, q->val_size, &refset);
            if (err) {
                if (DEBUG_QUERY_LEVEL_TMP)
                    knd_log("    -- no matching refset :(");
                return knd_FAIL;
            }

            parent_refset = refset;

            

            refsets[num_refsets] = refset;
            num_refsets++;
            



            refset = NULL;
            break;
        }
    }


    if (!num_refsets) {
        if (DEBUG_QUERY_LEVEL_TMP)
            knd_log("    -- no refset found :(\n");
        return knd_FAIL;
    }


    if (num_refsets == 1) {
        refset = refsets[0];
        
        if (DEBUG_QUERY_LEVEL_TMP)
            knd_log("\n\n    ++ single refset found, num refs: %lu\n",
                    (unsigned long)refset->num_refs);

        if (refset->num_refs > KND_RESULT_BATCH_SIZE && !refset->num_facets) {
            err = refset->facetize(refset);
            if (err) return err;
        }
            
        goto extract;
    }

    
    /* more than 1 (one) refset found:
     *  _intersection_ is needed! */

    err = kndRefSet_new(&refset);
    if (err) return err;

    refset->name[0] = '/';
    refset->name_size = 1;
    
    refset->cache = self->cache;
    refset->out = self->out;
    
    err = refset->intersect(refset, refsets, num_refsets);
    if (err) {
        knd_log(" -- NO INTERSECTION result :(\n\n");

        refset->del(refset);
        return err;
    }

    /* result */
    if (DEBUG_QUERY_LEVEL_TMP) {
        knd_log("\n    ++ intersected refset: \"%s\"", refset->name);
        refset->str(refset, 1, 5);
    }


    /* TODO: assign intersected refset to browser */

    /* TODO: assign name from intersected refsets */

    /* for i < num_refsets..
    
    memcpy(refset->name, "OO", 2);
    refset->name_size = 2;
    */
    

    
    
 extract:
    
    num_objs = KND_RESULT_BATCH_SIZE;
    
    if (refset->num_refs < KND_RESULT_BATCH_SIZE)
        num_objs = refset->num_refs;
    
    refset->batch_size = num_objs;

    if (!refset->batch_size) {
        if (DEBUG_QUERY_LEVEL_TMP)
            knd_log("     -- Empty refset: %s?\n",
                    refset->name);

        return knd_FAIL;
    }
    
    if (DEBUG_QUERY_LEVEL_TMP)
        knd_log("     ++ got matching RefSet of class: \"%s\".  TOTAL refs: %lu\n\n",
                refset->cache->baseclass->name,
                (unsigned long)refset->num_refs);

    /* activate facetizing? */
    if (refset->num_refs > KND_RESULT_BATCH_SIZE) {
        if (refset != self->cache->browser) {

            if (refset->num_facets < self->cache->browser->num_facets) {
                knd_log("    .. refset needs more facets!\n");

                err = refset->facetize(refset);
                if (err) return err;
            }
            
        }
        
    }


    /*refset->str(refset, 1, 7);*/

    
    /* reset selection results */
    self->cache->select_result = NULL;
    memset(self->cache->matches, 0, sizeof(struct kndObject*) * KND_MAX_MATCHES);
    self->cache->num_matches = 0;

    refset->cache = self->cache;
    
    err = refset->extract_objs(refset);
    if (err) {
        if (DEBUG_QUERY_LEVEL_TMP)
            knd_log("     -- no objs extracted from refset %s :(\n",
                    refset->name);
        return err;
    }
    
    self->cache->select_result = refset;

    return knd_OK;
}


static int
kndQuery_add_child(struct kndQuery *self,
                   struct kndQuery *q,
                   const char      *rec,
                   size_t           rec_size)
{
    const char *c, *b;
    size_t curr_size = 0;
    bool in_clause = false;
    bool in_base = false;
    bool in_spec = false;

    struct kndCodeRef *coderef = NULL;
    int i, err = knd_FAIL;

    if (DEBUG_QUERY_LEVEL_TMP)
        knd_log("   .. adding query clause [type: %d]: \"%s\" => \"%s\"\n",
                q->type, q->facet_name, rec);

    
    if (q->type == KND_QUERY_CONC) {

        knd_log(".. parse conc proposition \"%s\"\n", rec);
        
        c = rec;
            
        while (*c) {
            switch (*c) {
            case '[':
                if (in_clause) {
                    if (in_base) {
                        in_spec = true;
                        break;
                    }
                    in_base = true;
                    break;
                }

                in_clause = true;
                
                b = c;
                break;
            case ']':
                if (in_spec)
                    break;
                    
                if (in_base)
                    break;

                err =  kndCodeRef_new(&coderef);
                if (err) goto final;
                coderef->type = KND_ELEM_CG;

                err = coderef->parse(coderef, b, c - b + 1);
                if (err) goto final;

                coderef->next = q->coderefs;
                q->coderefs = coderef;
                in_clause = false;
                b = c;
                
                break;
            case '(':
                if (in_base) {
                    in_spec = true;
                }
                break;
            case ')':
                if (in_spec) {
                    in_spec = false;
                    in_base = false;
                }
                break;
            default:
                break;
            }
            c++;
        }
        goto assign;
    }


    /* check size limit */
    if (rec_size >= sizeof(q->val)) {
        knd_log("  -- input value size exceeds limit :(\n");
        return knd_NOMEM;
    }

    strncpy(q->val, rec, rec_size);
    q->val[rec_size] = '\0';
    q->val_size = rec_size;

 assign:
    
    self->children[self->num_children] = q;
    self->num_children++;

    err = knd_OK;
    
 final:
    
    if (coderef)
        coderef->del(coderef);

    return err;
}


/* parse and verify input rec */
static int
kndQuery_parse(struct kndQuery *self,
               const char      *rec,
               size_t           rec_size)
{
    char buf[KND_NAME_SIZE + 1];
    size_t buf_size;

    struct kndQuery *q = NULL;
    struct kndQuery *prev_q = NULL;
    
    const char *c, *b, *s;
    size_t curr_size = 0;

    bool in_clause = false;
    bool in_facet = false;
    
    bool in_number = false;
    int err = knd_FAIL;

    if (DEBUG_QUERY_LEVEL_TMP)
        knd_log("\n  ... parse query string: \"%s\"\n", rec);

    if (!strcmp(rec, "/")) return knd_OK;
    
    c = rec;
    b = rec;
    
    while (*c) {
        switch (*c) {
        case '(':
            if (!in_clause) {
                in_clause = true;
                /* fail by default */
                err = knd_FAIL;
            }
            b = c + 1;
            break;
        case ':':
            buf_size = c - b;

            if (buf_size >= KND_NAME_SIZE) return knd_LIMIT;
            
            memcpy(buf, b, buf_size);
            buf[buf_size] = '\0';

            /* check doublets */
            /*self->children[self->num_children] = q;*/

            /* get query clause */
            err = kndQuery_new(&q);
            if (err) return err;
            
            memcpy(q->facet_name, b, buf_size);
            q->facet_name[buf_size] = '\0';
            
            q->facet_name_size = buf_size;
            q->type = self->type;

            if (DEBUG_QUERY_LEVEL_TMP)
                knd_log("    == IDX name: \"%s\" [%lu]\n",
                        q->facet_name, (unsigned long)q->facet_name_size);
            
            b = c + 1;
            break;

        case '|':

            if (!q) return knd_FAIL;

            buf_size = c - b;
            if (buf_size >= KND_NAME_SIZE) return knd_LIMIT;
            memcpy(buf, b, buf_size);
            buf[buf_size] = '\0';

            err = kndQuery_add_child(self, q,
                                     (const char*)buf, buf_size);
            if (err) goto final;
            
            prev_q = q;
        
            /* get query clause */
            err = kndQuery_new(&q);
            if (err) return err;
            
            memcpy(q->facet_name, prev_q->facet_name, prev_q->facet_name_size);
            q->facet_name_size = prev_q->facet_name_size;
            q->type = self->type;
            
            b = c + 1;
            break;

        case ';':
            buf_size = c - b;
            memcpy(buf, b, buf_size);
            buf[buf_size] = '\0';

            if (DEBUG_QUERY_LEVEL_TMP)
                knd_log("   .. query facet value: \"%s\"\n", buf);

            /* check value */

            if (!q) {
                err = knd_FAIL;
                goto final;
            }

            err = kndQuery_add_child(self, q,
                               (const char*)buf, buf_size);
            if (err) goto final;
            
            q = NULL;
            
            b = c + 1;
            break;
        case ')':
            if (in_clause) {
                in_clause = false;
                err = knd_OK;
            }
            buf_size = c - b;

            if (!buf_size)
                goto final;

            memcpy(buf, b, buf_size);
            buf[buf_size] = '\0';

            if (DEBUG_QUERY_LEVEL_TMP)
                knd_log("    == FACET \"%s\" value: \"%s\"\n",
                        q->facet_name, buf);
            if (!q) return knd_FAIL;
            
            err = kndQuery_add_child(self, q,
                                     (const char*)buf, buf_size);
            if (err) goto final;
            
            q = NULL;

            b = c + 1;
            break;

        default:
            break;
        }
        c++;
    }

 final:

    /* sort queries */
    if (self->num_children > 1)
        kndQuery_sort_children(self);
    
    if (q)
        q->del(q);
    
    return err;
}

/*  Query Initializer */
int kndQuery_init(struct kndQuery *self)
{
    /* binding our methods */
    self->init = kndQuery_init;
    self->del = kndQuery_del;
    self->reset = kndQuery_reset;
    self->parse = kndQuery_parse;
    self->exec = kndQuery_execute;

    return knd_OK;
}


extern int 
kndQuery_new(struct kndQuery **q)
{
    struct kndQuery *self;
    int err;
    
    self = malloc(sizeof(struct kndQuery));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndQuery));

    /*err = kndOutput_new(&self->out, KND_TEMP_BUF_SIZE);
    if (err) return err;
    */
    
    kndQuery_init(self);

    *q = self;

    return knd_OK;

 error:
    free(self);
    return err;
}
