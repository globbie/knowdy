#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_mempool.h"
#include "knd_output.h"
#include "knd_concept.h"
#include "knd_object.h"
#include "knd_elem.h"
#include "knd_rel.h"
#include "knd_rel_arg.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_state.h"
#include "knd_query.h"
#include "knd_set.h"
#include "knd_facet.h"

static void del(struct kndMemPool *self)
{
    if (self->updates)             free(self->updates);
    if (self->update_idx)          free(self->update_idx);
    if (self->update_selected_idx) free(self->update_selected_idx);
    if (self->states)              free(self->states);
    if (self->class_updates)       free(self->class_updates);
    if (self->class_update_refs)   free(self->class_update_refs);

    if (self->classes)         free(self->classes);
    if (self->conc_dirs)       free(self->conc_dirs);
    if (self->conc_items)       free(self->conc_items);
    if (self->objs)            free(self->objs);
    if (self->obj_dirs)        free(self->obj_dirs);
    if (self->obj_entries)     free(self->obj_entries);
    if (self->elems)           free(self->elems);

    if (self->rels)            free(self->rels);
    if (self->rel_dirs)        free(self->rel_dirs);
    if (self->rel_insts)       free(self->rel_insts);
    if (self->rel_refs)        free(self->rel_refs);
    if (self->rel_arg_insts)   free(self->rel_arg_insts);
    if (self->rel_arg_inst_refs)   free(self->rel_arg_inst_refs);
    if (self->rel_updates)   free(self->rel_updates);
    if (self->rel_update_refs) free(self->rel_update_refs);

    if (self->procs)           free(self->procs);
    if (self->proc_dirs)       free(self->proc_dirs);
    if (self->proc_args)       free(self->proc_args);
    if (self->proc_insts)      free(self->proc_insts);

    free(self);
}

static int new_class(struct kndMemPool *self,
                     struct kndConcept **result)
{
    struct kndConcept *c;
    if (self->num_classes >= self->max_classes) {
        return knd_NOMEM;
    }
    c = &self->classes[self->num_classes];
    memset(c, 0, sizeof(struct kndConcept));
    kndConcept_init(c);
    self->num_classes++;
    *result = c;
    return knd_OK;
}

static int new_query(struct kndMemPool *self,
                     struct kndQuery **result)
{
    struct kndQuery *q;
    if (self->num_queries >= self->max_queries) {
        return knd_NOMEM;
    }
    q = &self->queries[self->num_queries];
    memset(q, 0, sizeof(struct kndQuery));
    kndQuery_init(q);
    self->num_queries++;
    *result = q;
    return knd_OK;
}

static int new_set(struct kndMemPool *self,
                     struct kndSet **result)
{
    struct kndSet *q;
    if (self->num_sets >= self->max_sets) {
        return knd_NOMEM;
    }
    q = &self->sets[self->num_sets];
    memset(q, 0, sizeof(struct kndSet));
    kndSet_init(q);
    self->num_sets++;
    *result = q;
    return knd_OK;
}

static int new_set_elem(struct kndMemPool *self,
			struct kndSetElem **result)
{
    struct kndSetElem *q;
    if (self->num_set_elems >= self->max_set_elems) {
        return knd_NOMEM;
    }
    q = &self->set_elems[self->num_set_elems];
    memset(q, 0, sizeof(struct kndSetElem));
    self->num_set_elems++;
    *result = q;
    return knd_OK;
}

static int new_set_elem_idx(struct kndMemPool *self,
			    struct kndSetElemIdx **result)
{
    struct kndSetElemIdx *q;
    if (self->num_set_elem_idxs >= self->max_set_elem_idxs) {
        return knd_NOMEM;
    }
    q = &self->set_elem_idxs[self->num_set_elem_idxs];
    memset(q, 0, sizeof(struct kndSetElemIdx));
    self->num_set_elem_idxs++;
    *result = q;
    return knd_OK;
}

static int new_facet(struct kndMemPool *self,
                     struct kndFacet **result)
{
    struct kndFacet *f;
    if (self->num_facets >= self->max_facets) {
        return knd_NOMEM;
    }
    f = &self->facets[self->num_facets];
    memset(f, 0, sizeof(struct kndFacet));
    kndFacet_init(f);
    self->num_facets++;
    *result = f;
    return knd_OK;
}

static int new_update(struct kndMemPool *self,
                      struct kndUpdate **result)
{
    struct kndUpdate *upd;
    int e;

    if (self->num_updates >= self->max_updates) {
        self->log->reset(self->log);
        e = self->log->write(self->log, "memory limit reached",
                             strlen("memory limit reached"));
        if (e) return e;

        knd_log("-- memory limit reached :(");
        return knd_LIMIT;
    }
    upd = &self->updates[self->num_updates];
    memset(upd, 0, sizeof(struct kndUpdate));
    self->num_updates++;
    upd->id = self->num_updates;
    *result = upd;
    return knd_OK;
}

static int new_class_update(struct kndMemPool *self,
                            struct kndClassUpdate **result)
{
    struct kndClassUpdate *upd;
    int e;

    if (self->num_class_updates >= self->max_class_updates) {
        self->log->reset(self->log);
        e = self->log->write(self->log, "memory limit reached",
                             strlen("memory limit reached"));
        if (e) return e;

        knd_log("-- memory limit reached :(");
        return knd_LIMIT;
    }
    upd = &self->class_updates[self->num_class_updates];
    memset(upd, 0, sizeof(struct kndClassUpdate));

    self->num_class_updates++;
    *result = upd;
    return knd_OK;
}

static int new_class_update_ref(struct kndMemPool *self,
                                struct kndClassUpdateRef **result)
{
    struct kndClassUpdateRef *upd;

    if (self->num_class_update_refs >= self->max_class_update_refs) return knd_NOMEM;
    upd = &self->class_update_refs[self->num_class_update_refs];
    memset(upd, 0, sizeof(struct kndClassUpdateRef));

    self->num_class_update_refs++;
    *result = upd;
    return knd_OK;
}

static int new_obj(struct kndMemPool *self,
                   struct kndObject **result)
{
    struct kndObject *obj;
    

    if (self->num_objs >= self->max_objs) {
        return knd_NOMEM;
    }
    obj = &self->objs[self->num_objs];
    memset(obj, 0, sizeof(struct kndObject));
    kndObject_init(obj);
    self->num_objs++;
    *result = obj;
    return knd_OK;
}

static int new_state(struct kndMemPool *self,
		     struct kndState **result)
{
    struct kndState *state;
    if (self->num_states >= self->max_states) {
        return knd_NOMEM;
    }
    state = &self->states[self->num_states];
    memset(state, 0, sizeof(struct kndState));
    self->num_states++;
    *result = state;
    return knd_OK;
}

static int new_obj_entry(struct kndMemPool *self,
                         struct kndObjEntry **result)
{
    struct kndObjEntry *entry;
    if (self->num_obj_entries >= self->max_obj_entries) {
        return knd_LIMIT;
    }
    entry = &self->obj_entries[self->num_obj_entries];
    memset(entry, 0, sizeof(struct kndObjEntry));
    self->num_obj_entries++;
    *result = entry;
    return knd_OK;
}

static int new_obj_elem(struct kndMemPool *self,
                         struct kndElem **result)
{
    struct kndElem *elem;
    if (self->num_elems >= self->max_elems) {
        return knd_LIMIT;
    }
    elem = &self->elems[self->num_elems];
    memset(elem, 0, sizeof(struct kndElem));
    kndElem_init(elem);
    self->num_elems++;
    *result = elem;
    return knd_OK;
}

static int new_conc_dir(struct kndMemPool *self,
                       struct kndConcDir **result)
{
    struct kndConcDir *dir;
    if (self->num_conc_dirs >= self->max_conc_dirs) {
        return knd_LIMIT;
    }
    dir = &self->conc_dirs[self->num_conc_dirs];
    memset(dir, 0, sizeof(struct kndConcDir));
    self->num_conc_dirs++;
    *result = dir;
    return knd_OK;
}

static int new_conc_item(struct kndMemPool *self,
			 struct kndConcItem **result)
{
    struct kndConcItem *item;

    if (self->num_conc_items >= self->max_conc_items) {
        return knd_LIMIT;
    }
    item = &self->conc_items[self->num_conc_items];
    memset(item, 0, sizeof(struct kndConcItem));
    self->num_conc_items++;
    *result = item;
    return knd_OK;
}

static int new_obj_dir(struct kndMemPool *self,
                       struct kndObjDir **result)
{
    struct kndObjDir *dir;
    

    if (self->num_obj_dirs >= self->max_obj_dirs) {
        return knd_LIMIT;
    }
    dir = &self->obj_dirs[self->num_obj_dirs];
    memset(dir, 0, sizeof(struct kndObjDir));
    self->num_obj_dirs++;
    *result = dir;
    return knd_OK;
}

static int new_rel_dir(struct kndMemPool *self,
                       struct kndRelDir **result)
{
    struct kndRelDir *dir;

    if (self->num_rel_dirs >= self->max_rel_dirs) {
        return knd_LIMIT;
    }
    dir = &self->rel_dirs[self->num_rel_dirs];
    memset(dir, 0, sizeof(struct kndRelDir));
    self->num_rel_dirs++;
    *result = dir;
    return knd_OK;
}

static int new_rel_ref(struct kndMemPool *self,
                       struct kndRelRef **result)
{
    struct kndRelRef *ref;

    if (self->num_rel_refs >= self->max_rel_refs) {
        return knd_LIMIT;
    }
    ref = &self->rel_refs[self->num_rel_refs];
    memset(ref, 0, sizeof(struct kndRelRef));
    self->num_rel_refs++;
    *result = ref;
    return knd_OK;
}

static int new_rel(struct kndMemPool *self,
                   struct kndRel **result)
{
    struct kndRel *rel;
    

    if (self->num_rels >= self->max_rels) return knd_NOMEM;

    rel = &self->rels[self->num_rels];
    memset(rel, 0, sizeof(struct kndRel));
    kndRel_init(rel);
    self->num_rels++;
    *result = rel;
    return knd_OK;
}

static int new_rel_inst(struct kndMemPool *self,
                        struct kndRelInstance **result)
{
    struct kndRelInstance *rel_inst;
    int e;

    if (self->num_rel_insts >= self->max_rel_insts) {
        self->log->reset(self->log);
        e = self->log->write(self->log, "memory limit reached",
                             strlen("memory limit reached"));
        if (e) return e;

        knd_log("-- memory limit reached :(");
        return knd_LIMIT;
    }
    rel_inst = &self->rel_insts[self->num_rel_insts];
    memset(rel_inst, 0, sizeof(struct kndRelInstance));
    kndRelInstance_init(rel_inst);
    self->num_rel_insts++;
    *result = rel_inst;
    return knd_OK;
}

static int new_rel_arg_inst(struct kndMemPool *self,
                            struct kndRelArgInstance **result)
{
    struct kndRelArgInstance *rel_arg_inst;
    

    if (self->num_rel_arg_insts >= self->max_rel_arg_insts) {
	knd_log("-- rel arg inst limit reached :(");
	return knd_LIMIT;
    }
    rel_arg_inst = &self->rel_arg_insts[self->num_rel_arg_insts];
    memset(rel_arg_inst, 0, sizeof(struct kndRelArgInstance));
    kndRelArgInstance_init(rel_arg_inst);
    self->num_rel_arg_insts++;
    *result = rel_arg_inst;
    return knd_OK;
}

static int new_rel_arg_inst_ref(struct kndMemPool *self,
                            struct kndRelArgInstRef **result)
{
    struct kndRelArgInstRef *rel_arg_inst_ref;

    if (self->num_rel_arg_inst_refs >= self->max_rel_arg_inst_refs) return knd_LIMIT;
    rel_arg_inst_ref = &self->rel_arg_inst_refs[self->num_rel_arg_inst_refs];
    memset(rel_arg_inst_ref, 0, sizeof(struct kndRelArgInstRef));
    kndRelArgInstRef_init(rel_arg_inst_ref);
    self->num_rel_arg_inst_refs++;
    *result = rel_arg_inst_ref;
    return knd_OK;
}



static int new_rel_update(struct kndMemPool *self,
                            struct kndRelUpdate **result)
{
    struct kndRelUpdate *upd;
    int e;

    if (self->num_rel_updates >= self->max_rel_updates) {
        self->log->reset(self->log);
        e = self->log->write(self->log, "memory limit reached",
                             strlen("memory limit reached"));
        if (e) return e;

        knd_log("-- memory limit reached :(");
        return knd_LIMIT;
    }
    upd = &self->rel_updates[self->num_rel_updates];
    memset(upd, 0, sizeof(struct kndRelUpdate));

    self->num_rel_updates++;
    *result = upd;
    return knd_OK;
}

static int new_rel_update_ref(struct kndMemPool *self,
                                struct kndRelUpdateRef **result)
{
    struct kndRelUpdateRef *upd;
    

    if (self->num_rel_update_refs >= self->max_rel_update_refs) return knd_NOMEM;
    upd = &self->rel_update_refs[self->num_rel_update_refs];
    memset(upd, 0, sizeof(struct kndRelUpdateRef));

    self->num_rel_update_refs++;
    *result = upd;
    return knd_OK;
}

static int new_proc(struct kndMemPool *self,
                   struct kndProc **result)
{
    struct kndProc *proc;
    int e;
    if (self->num_procs >= self->max_procs) {
        self->log->reset(self->log);
        e = self->log->write(self->log, "memory limit reached",
                             strlen("memory limit reached"));
        if (e) return e;
        knd_log("-- Proc pool memory limit reached :(");
        return knd_LIMIT;
    }

    proc = &self->procs[self->num_procs];
    memset(proc, 0, sizeof(struct kndProc));
    kndProc_init(proc);
    self->num_procs++;
    *result = proc;

    return knd_OK;
}

static int new_proc_dir(struct kndMemPool *self,
                       struct kndProcDir **result)
{
    struct kndProcDir *dir;
    if (self->num_proc_dirs >= self->max_proc_dirs) {
        return knd_LIMIT;
    }
    dir = &self->proc_dirs[self->num_proc_dirs];
    memset(dir, 0, sizeof(struct kndProcDir));
    self->num_proc_dirs++;
    *result = dir;
    return knd_OK;
}

static int new_proc_arg(struct kndMemPool *self,
			struct kndProcArg **result)
{
    struct kndProcArg *arg;
    if (self->num_proc_args >= self->max_proc_args) {
        return knd_LIMIT;
    }
    arg = &self->proc_args[self->num_proc_args];
    memset(arg, 0, sizeof(struct kndProcArg));
    kndProcArg_init(arg);
    self->num_proc_args++;
    *result = arg;
    return knd_OK;
}

static int new_proc_update(struct kndMemPool *self,
                            struct kndProcUpdate **result)
{
    struct kndProcUpdate *upd;
    int e;

    if (self->num_proc_updates >= self->max_proc_updates) {
        self->log->reset(self->log);
        e = self->log->write(self->log, "memory limit reached",
                             strlen("memory limit reached"));
        if (e) return e;

        knd_log("-- memory limit reached :(");
        return knd_LIMIT;
    }
    upd = &self->proc_updates[self->num_proc_updates];
    memset(upd, 0, sizeof(struct kndProcUpdate));

    self->num_proc_updates++;
    *result = upd;
    return knd_OK;
}

static int new_proc_update_ref(struct kndMemPool *self,
                                struct kndProcUpdateRef **result)
{
    struct kndProcUpdateRef *upd;
    

    if (self->num_proc_update_refs >= self->max_proc_update_refs) return knd_NOMEM;
    upd = &self->proc_update_refs[self->num_proc_update_refs];
    memset(upd, 0, sizeof(struct kndProcUpdateRef));

    self->num_proc_update_refs++;
    *result = upd;
    return knd_OK;
}


static int alloc(struct kndMemPool *self)
{
    if (!self->max_queries)      self->max_queries = KND_MIN_QUERIES;
    if (!self->max_sets)         self->max_sets =    KND_MIN_SETS;
    if (!self->max_set_elems)    self->max_set_elems =         KND_MIN_SETS;
    if (!self->max_set_elem_idxs) self->max_set_elem_idxs =    KND_MIN_SETS;
    if (!self->max_facets)       self->max_facets =  KND_MIN_FACETS;
    if (!self->max_updates)      self->max_updates = KND_MIN_UPDATES;
    if (!self->max_states)       self->max_states =  KND_MIN_STATES;
    if (!self->max_class_updates) self->max_class_updates = KND_MIN_UPDATES;
    if (!self->max_class_update_refs) self->max_class_update_refs = KND_MIN_UPDATES;
    if (!self->max_users)        self->max_users =   KND_MIN_USERS;
    if (!self->max_classes)      self->max_classes = KND_MIN_CLASSES;
    if (!self->max_conc_dirs)    self->max_conc_dirs = self->max_classes;
    if (!self->max_conc_items)   self->max_conc_items = KND_MIN_CLASSES;
    if (!self->max_objs)         self->max_objs =    KND_MIN_OBJS;
    if (!self->max_obj_dirs)     self->max_obj_dirs = self->max_objs;
    if (!self->max_obj_entries)  self->max_obj_entries = self->max_objs;
    if (!self->max_elems)        self->max_elems = self->max_objs;
    if (!self->max_rels)         self->max_rels =    KND_MIN_RELS;
    if (!self->max_rel_dirs)     self->max_rel_dirs = KND_MIN_REL_INSTANCES;

    if (!self->max_rel_refs)     self->max_rel_refs = self->max_rels;
    if (!self->max_rel_insts)    self->max_rel_insts = KND_MIN_REL_INSTANCES;
    if (!self->max_rel_arg_insts) self->max_rel_arg_insts = KND_MIN_RELARG_INSTANCES;
    if (!self->max_rel_arg_inst_refs) self->max_rel_arg_inst_refs = self->max_rel_arg_insts;

    if (!self->max_rel_updates) self->max_rel_updates = KND_MIN_UPDATES;
    if (!self->max_rel_update_refs) self->max_rel_update_refs = KND_MIN_UPDATES;

    if (!self->max_procs)         self->max_procs =  KND_MIN_PROCS;
    if (!self->max_proc_dirs)     self->max_proc_dirs = self->max_procs;
    if (!self->max_proc_args)     self->max_proc_args =  self->max_procs;
    if (!self->max_proc_insts)    self->max_proc_insts = KND_MIN_PROC_INSTANCES;
    if (!self->max_proc_updates) self->max_proc_updates = KND_MIN_UPDATES;
    if (!self->max_proc_update_refs) self->max_proc_update_refs = KND_MIN_UPDATES;

    self->classes = calloc(self->max_classes, sizeof(struct kndConcept));
    if (!self->classes) {
        knd_log("-- classes not allocated :(");
        return knd_NOMEM;
    }

    self->conc_dirs = calloc(self->max_conc_dirs, sizeof(struct kndConcDir));
    if (!self->conc_dirs) {
        knd_log("-- conc dirs not allocated :(");
        return knd_NOMEM;
    }

    self->conc_items = calloc(self->max_conc_items, sizeof(struct kndConcItem));
    if (!self->conc_items) {
        knd_log("-- conc items not allocated :(");
        return knd_NOMEM;
    }

    self->queries = calloc(self->max_queries, sizeof(struct kndQuery));
    if (!self->queries) {
        knd_log("-- queries not allocated :(");
        return knd_NOMEM;
    }
    self->sets = calloc(self->max_sets, sizeof(struct kndSet));
    if (!self->sets) {
        knd_log("-- sets not allocated :(");
        return knd_NOMEM;
    }
    self->set_elems = calloc(self->max_set_elems, sizeof(struct kndSetElem));
    if (!self->set_elems) {
        knd_log("-- set elems not allocated :(");
        return knd_NOMEM;
    }
    self->set_elem_idxs = calloc(self->max_set_elem_idxs, sizeof(struct kndSetElemIdx));
    if (!self->set_elem_idxs) {
        knd_log("-- set elem idxs not allocated :(");
        return knd_NOMEM;
    }

    self->facets = calloc(self->max_facets, sizeof(struct kndFacet));
    if (!self->facets) {
        knd_log("-- facets not allocated :(");
        return knd_NOMEM;
    }
    
    self->updates = calloc(self->max_updates, sizeof(struct kndUpdate));
    if (!self->updates) {
        knd_log("-- updates not allocated :(");
        return knd_NOMEM;
    }
    self->update_idx = calloc(self->max_updates, sizeof(struct kndUpdate*));
    if (!self->update_idx) {
        knd_log("-- update idx not allocated :(");
        return knd_NOMEM;
    }
    self->update_selected_idx = calloc(self->max_updates, sizeof(struct kndUpdate*));
    if (!self->update_selected_idx) {
        knd_log("-- selected update idx not allocated :(");
        return knd_NOMEM;
    }
    self->states = calloc(self->max_states, sizeof(struct kndState));
    if (!self->states) {
        knd_log("-- states not allocated :(");
        return knd_NOMEM;
    }
    
    self->class_updates = calloc(self->max_updates, sizeof(struct kndClassUpdate));
    if (!self->class_updates) {
        knd_log("-- class updates not allocated :(");
        return knd_NOMEM;
    }

    self->class_update_refs = calloc(self->max_updates, sizeof(struct kndClassUpdateRef));
    if (!self->class_update_refs) {
        knd_log("-- class updates not allocated :(");
        return knd_NOMEM;
    }

    self->objs = calloc(self->max_objs, sizeof(struct kndObject));
    if (!self->objs) {
        knd_log("-- objs not allocated :(");
        return knd_NOMEM;
    }

    self->obj_entries = calloc(self->max_obj_entries, sizeof(struct kndObjEntry));
    if (!self->obj_entries) {
        knd_log("-- obj entries not allocated :(");
        return knd_NOMEM;
    }
    self->elems = calloc(self->max_elems, sizeof(struct kndElem));
    if (!self->elems) {
        knd_log("-- obj elems not allocated :(");
        return knd_NOMEM;
    }

    self->obj_dirs = calloc(self->max_obj_dirs, sizeof(struct kndObjDir));
    if (!self->obj_dirs) {
        knd_log("-- obj dirs not allocated :(");
        return knd_NOMEM;
    }

    self->rels = calloc(self->max_rels, sizeof(struct kndRel));
    if (!self->rels) {
        knd_log("-- rels not allocated :(");
        return knd_NOMEM;
    }

    self->rel_dirs = calloc(self->max_rel_dirs, sizeof(struct kndRelDir));
    if (!self->rel_dirs) {
        knd_log("-- rel dirs not allocated :(");
        return knd_NOMEM;
    }
    self->rel_refs = calloc(self->max_rel_refs, sizeof(struct kndRelRef));
    if (!self->rel_refs) {
        knd_log("-- rel refs not allocated :(");
        return knd_NOMEM;
    }

    self->rel_insts = calloc(self->max_rel_insts, sizeof(struct kndRelInstance));
    if (!self->rel_insts) {
        knd_log("-- rel insts not allocated :(");
        return knd_NOMEM;
    }

    self->rel_arg_insts = calloc(self->max_rel_arg_insts,
                                 sizeof(struct kndRelArgInstance));
    if (!self->rel_arg_insts) {
        knd_log("-- rel arg insts not allocated :(");
        return knd_NOMEM;
    }

    self->rel_arg_inst_refs = calloc(self->max_rel_arg_inst_refs,
                                 sizeof(struct kndRelArgInstRef));
    if (!self->rel_arg_inst_refs) {
        knd_log("-- rel arg insts not allocated :(");
        return knd_NOMEM;
    }

    self->rel_updates = calloc(self->max_updates, sizeof(struct kndRelUpdate));
    if (!self->rel_updates) {
        knd_log("-- rel updates not allocated :(");
        return knd_NOMEM;
    }

    self->rel_update_refs = calloc(self->max_updates, sizeof(struct kndRelUpdateRef));
    if (!self->rel_update_refs) {
        knd_log("-- rel updates not allocated :(");
        return knd_NOMEM;
    }


    self->procs = calloc(self->max_procs, sizeof(struct kndProc));
    if (!self->procs) {
        knd_log("-- procs not allocated :(");
        return knd_NOMEM;
    }

    self->proc_dirs = calloc(self->max_proc_dirs, sizeof(struct kndProcDir));
    if (!self->proc_dirs) {
        knd_log("-- proc dirs not allocated :(");
        return knd_NOMEM;
    }
    self->proc_args = calloc(self->max_proc_args, sizeof(struct kndProcArg));
    if (!self->proc_args) {
        knd_log("-- proc args not allocated :(");
        return knd_NOMEM;
    }

    self->proc_insts = calloc(self->max_proc_insts,
                              sizeof(struct kndProcInstance));
    if (!self->proc_insts) {
        knd_log("-- proc insts not allocated :(");
        return knd_NOMEM;
    }

    self->proc_updates = calloc(self->max_updates, sizeof(struct kndProcUpdate));
    if (!self->proc_updates) {
        knd_log("-- proc updates not allocated :(");
        return knd_NOMEM;
    }

    self->proc_update_refs = calloc(self->max_updates, sizeof(struct kndProcUpdateRef));
    if (!self->proc_update_refs) {
        knd_log("-- proc updates not allocated :(");
        return knd_NOMEM;
    }

    knd_log("TOTAL allocations: classes:%zu  objs:%zu  "
            "elems:%zu  rels:%zu  procs:%zu",
            self->max_classes, self->max_objs, self->max_elems,
            self->max_rels, self->max_procs);

    return knd_OK;
}

extern void
kndMemPool_init(struct kndMemPool *self)
{
    self->del = del;
    self->alloc = alloc;
    self->new_set = new_set;
    self->new_facet = new_facet;
    self->new_query = new_query;
    self->new_update = new_update;
    self->new_state = new_state;
    self->new_class_update = new_class_update;
    self->new_class_update_ref = new_class_update_ref;
    self->new_class = new_class;
    self->new_conc_dir = new_conc_dir;
    self->new_conc_item = new_conc_item;
    self->new_obj = new_obj;
    self->new_obj_dir = new_obj_dir;
    self->new_obj_entry = new_obj_entry;
    self->new_obj_elem = new_obj_elem;
    self->new_rel = new_rel;
    self->new_rel_dir = new_rel_dir;
    self->new_rel_ref = new_rel_ref;
    self->new_rel_inst = new_rel_inst;
    self->new_rel_arg_inst = new_rel_arg_inst;
    self->new_rel_arg_inst_ref = new_rel_arg_inst_ref;
    self->new_rel_update = new_rel_update;
    self->new_rel_update_ref = new_rel_update_ref;
    self->new_proc = new_proc;
    self->new_proc_dir = new_proc_dir;
    self->new_proc_arg = new_proc_arg;
    self->new_proc_update = new_proc_update;
    self->new_proc_update_ref = new_proc_update_ref;
}

extern int
kndMemPool_new(struct kndMemPool **obj)
{
    struct kndMemPool *self;

    self = malloc(sizeof(struct kndMemPool));
    if (!self) return knd_NOMEM;
    memset(self, 0, sizeof(struct kndMemPool));

    memset(self->next_class_id, '0', KND_ID_SIZE);
    memset(self->next_proc_id, '0', KND_ID_SIZE);
    memset(self->next_rel_id, '0', KND_ID_SIZE);

    kndMemPool_init(self);
    *obj = self;
    return knd_OK;
}
