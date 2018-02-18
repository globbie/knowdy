#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_query.h"
#include "knd_repo.h"
#include "knd_output.h"
#include "knd_concept.h"
#include "knd_object.h"
#include "knd_set.h"

#define DEBUG_QUERY_LEVEL_1 0
#define DEBUG_QUERY_LEVEL_2 0
#define DEBUG_QUERY_LEVEL_3 0
#define DEBUG_QUERY_LEVEL_4 0
#define DEBUG_QUERY_LEVEL_5 0
#define DEBUG_QUERY_LEVEL_TMP 1


/*  Query Destructor */
static void kndQuery_del(struct kndQuery *self)
{
    free(self);
}

static
void kndQuery_reset(struct kndQuery *self)
{
    struct kndQuery *q;

    for (size_t i = 0; i < self->num_children; i++) {
        q = self->children[i];
        q->del(q);
    }

    self->num_children = 0;
}

static int
kndQuery_execute(struct kndQuery *self)
{
    //char buf[KND_NAME_SIZE];
    //size_t buf_size;
    
    struct kndQuery *q;
    struct kndSet *parent_set = NULL;
    struct kndSet *set = NULL;

    struct kndSet *sets[KND_MAX_CLAUSES];
    size_t num_sets = 0;
    size_t num_objs;
    
    int err;

    if (DEBUG_QUERY_LEVEL_TMP)
        knd_log("     .. Execute Query Facet \"%s\"\n",
                self->facet_name);

    /* reset */
    //self->cache->select_result = NULL;

    /* default result */
    /*if (!self->num_children) {
        set = self->cache->browser;
        goto extract;
    }
    */
    /* TODO: lookup intersected sets */

    /* get sets for each query clause */
    for (size_t i = 0; i < self->num_children; i++) {
        q = self->children[i];

        if (DEBUG_QUERY_LEVEL_TMP)
            knd_log("     .. Child Query Clause \"%s\"\n",
                    q->facet_name);
        
        /* conceptual search? */
        switch (q->type) {
            /*case KND_QUERY_CONC:

            err = kndQuery_lookup_coderefs(self, q, &set);
            if (err) return err;
            
            break;*/
            
        default:

            /*if (!strcmp(q->val, "/")) {
                set = self->cache->browser;
                goto extract;
                }*/

            /* TODO: try looking up the cache */
            if (parent_set) {
                
            }

                
            /*            err = self->cache->browser->find(self->cache->browser,
                                             q->facet_name,
                                             q->val, q->val_size, &set);
            if (err) {
                if (DEBUG_QUERY_LEVEL_TMP)
                    knd_log("    -- no matching set :(");
                return knd_FAIL;
            }

            parent_set = set;

            */

            sets[num_sets] = set;
            num_sets++;
            



            set = NULL;
            break;
        }
    }


    if (!num_sets) {
        if (DEBUG_QUERY_LEVEL_TMP)
            knd_log("    -- no set found :(\n");
        return knd_FAIL;
    }


    if (num_sets == 1) {
        set = sets[0];
        
        if (DEBUG_QUERY_LEVEL_TMP)
            knd_log("\n\n    ++ single set found, num refs: %lu\n",
                    (unsigned long)set->num_elems);

        if (set->num_elems > KND_RESULT_BATCH_SIZE && !set->num_facets) {
            err = set->facetize(set);
            if (err) return err;
        }
            
        goto extract;
    }

    
    /* more than 1 (one) set found:
     *  _intersection_ is needed! */

    /* TODO err = kndSet_new(&set);
    if (err) return err;

    set->name[0] = '/';
    set->name_size = 1;
    */
    set->out = self->out;
    
    err = set->intersect(set, sets, num_sets);
    if (err) {
        knd_log(" -- NO INTERSECTION result :(\n\n");

        set->del(set);
        return err;
    }

    /* result */
    if (DEBUG_QUERY_LEVEL_TMP) {
        knd_log("\n    ++ intersected set: \"%s\"", set->name);
        set->str(set, 1, 5);
    }


    /* TODO: assign intersected set to browser */

    /* TODO: assign name from intersected sets */

    /* for i < num_sets..

    memcpy(set->name, "OO", 2);
    set->name_size = 2;
    */

extract:

    num_objs = KND_RESULT_BATCH_SIZE;

    if (set->num_elems < KND_RESULT_BATCH_SIZE)
        num_objs = set->num_elems;

    //set->batch_size = num_objs;

    /*if (!set->batch_size) {
        if (DEBUG_QUERY_LEVEL_TMP)
            knd_log("     -- Empty set: %s?\n",
                    set->name);

        return knd_FAIL;
	}*/

    /*    if (DEBUG_QUERY_LEVEL_TMP)
        knd_log("     ++ got matching Set of class: \"%s\".  TOTAL refs: %lu\n\n",
                set->cache->baseclass->name,
                (unsigned long)set->num_elems);
    */
    /* activate facetizing? */
    if (set->num_elems > KND_RESULT_BATCH_SIZE) {
        /*if (set != self->cache->browser) {

                  if (set->num_facets < self->cache->browser->num_facets) {
                knd_log("    .. set needs more facets!\n");

                err = set->facetize(set);
                if (err) return err;
            }
            
            } */

    }


    /*set->str(set, 1, 7);*/

    /* reset selection results */
    /*    self->cache->select_result = NULL;
    memset(self->cache->matches, 0, sizeof(struct kndObject*) * KND_MAX_MATCHES);
    self->cache->num_matches = 0;

    set->cache = self->cache;

    err = set->extract_objs(set);
    if (err) {
        if (DEBUG_QUERY_LEVEL_TMP)
            knd_log("     -- no objs extracted from set %s :(\n",
                    set->name);
        return err;
    }

    self->cache->select_result = set;
    */
    
    return knd_OK;
}

/*  Query Initializer */
int kndQuery_init(struct kndQuery *self)
{
    /* binding our methods */
    self->init = kndQuery_init;
    self->del = kndQuery_del;
    self->reset = kndQuery_reset;
    //self->parse = kndQuery_parse;
    self->exec = kndQuery_execute;

    return knd_OK;
}


extern int 
kndQuery_new(struct kndQuery **q)
{
    struct kndQuery *self;

    self = malloc(sizeof(struct kndQuery));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndQuery));

    kndQuery_init(self);

    *q = self;

    return knd_OK;
}
