#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_mempool.h"
#include "knd_class_inst.h"
#include "knd_elem.h"
#include "knd_rel.h"
#include "knd_rel_arg.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_state.h"
#include "knd_attr.h"
#include "knd_set.h"
#include "knd_user.h"
#include "knd_facet.h"

static void del(struct kndMemPool *self)
{
    if (self->user_ctxs)           free(self->user_ctxs);
    if (self->updates)             free(self->updates);
    if (self->update_idx)          free(self->update_idx);
    if (self->update_selected_idx) free(self->update_selected_idx);
    if (self->states)              free(self->states);
    if (self->class_updates)       free(self->class_updates);

    if (self->objs)            free(self->objs);
    if (self->obj_dirs)        free(self->obj_dirs);
    if (self->obj_entries)     free(self->obj_entries);
    if (self->elems)           free(self->elems);

    if (self->rels)            free(self->rels);
    if (self->rel_entries)        free(self->rel_entries);
    if (self->rel_insts)       free(self->rel_insts);
    if (self->rel_refs)        free(self->rel_refs);
    if (self->rel_arg_insts)   free(self->rel_arg_insts);
    if (self->rel_arg_inst_refs)   free(self->rel_arg_inst_refs);
    if (self->rel_updates)   free(self->rel_updates);
    if (self->rel_inst_updates) free(self->rel_inst_updates);

    if (self->procs)           free(self->procs);
    if (self->proc_entries)       free(self->proc_entries);
    if (self->proc_args)       free(self->proc_args);
    if (self->proc_insts)      free(self->proc_insts);

    free(self);
}

static int present_status(struct kndMemPool *self,
                          struct glbOutput *out)
{
    int err;

    err = out->writef(out, "{\"used_pages\":%zu}", self->pages_used);    RET_ERR();

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
    q->mempool = self;
    self->num_sets++;
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
    self->num_facets++;
    *result = f;
    return knd_OK;
}

static int new_user_ctx(struct kndMemPool *self,
                        struct kndUserContext **result)
{
    struct kndUserContext *ctx;
    if (self->num_user_ctxs >= self->max_user_ctxs) {
        return knd_NOMEM;
    }
    ctx = &self->user_ctxs[self->num_user_ctxs];
    memset(ctx, 0, sizeof(struct kndUserContext));
    self->num_user_ctxs++;
    *result = ctx;
    return knd_OK;
}

static int new_update(struct kndMemPool *self,
                      struct kndUpdate **result)
{
    struct kndUpdate *upd;
    int e;
    if (self->num_updates >= self->max_updates) {
        knd_log("-- updates memory limit exceeded, max: %zu", self->max_updates);
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
    upd->numid = self->num_updates;
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
        e = self->log->write(self->log, "class update mempool exhausted",
                             strlen("class update mempool exhausted"));
        if (e) return e;

        knd_log("-- class update mempool exhausted, max: %zu",
                self->max_class_updates);
        return knd_LIMIT;
    }
    upd = &self->class_updates[self->num_class_updates];
    memset(upd, 0, sizeof(struct kndClassUpdate));

    self->num_class_updates++;
    *result = upd;
    return knd_OK;
}

static int new_class_inst(struct kndMemPool *self,
                   struct kndClassInst **result)
{
    struct kndClassInst *obj;
    

    if (self->num_objs >= self->max_objs) {
        return knd_NOMEM;
    }
    obj = &self->objs[self->num_objs];
    memset(obj, 0, sizeof(struct kndClassInst));
    kndClassInst_init(obj);
    self->num_objs++;
    *result = obj;
    return knd_OK;
}

static int new_state(struct kndMemPool *self,
                     struct kndState **result)
{
    struct kndState *state;
    if (self->num_states >= self->max_states) {
        knd_log("-- state mempool exhausted, max: %zu", self->max_states);
        return knd_NOMEM;
    }
    state = &self->states[self->num_states];
    memset(state, 0, sizeof(struct kndState));
    self->num_states++;
    *result = state;
    return knd_OK;
}

static int new_class_inst_entry(struct kndMemPool *self,
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

static int new_class_inst_elem(struct kndMemPool *self,
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

static int new_class_inst_dir(struct kndMemPool *self,
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

static int new_rel_entry(struct kndMemPool *self,
                         struct kndRelEntry **result)
{
    struct kndRelEntry *entry;

    if (self->num_rel_entries >= self->max_rel_entries) {
        return knd_LIMIT;
    }
    entry = &self->rel_entries[self->num_rel_entries];
    memset(entry, 0, sizeof(struct kndRelEntry));
    self->num_rel_entries++;
    *result = entry;
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
        knd_log("-- rel update mempool exhausted, num:%zu max: %zu",
                self->num_rel_updates,
                self->max_rel_updates);

        self->log->reset(self->log);
        e = self->log->write(self->log, "rel updates memory limit reached",
                             strlen("rel updates memory limit reached"));
        if (e) return e;
        return knd_LIMIT;
    }

    upd = &self->rel_updates[self->num_rel_updates];
    memset(upd, 0, sizeof(struct kndRelUpdate));

    self->num_rel_updates++;
    *result = upd;
    return knd_OK;
}

static int new_rel_inst_update(struct kndMemPool *self,
                               struct kndRelInstanceUpdate **result)
{
    struct kndRelInstanceUpdate *upd;

    if (self->num_rel_inst_updates >= self->max_rel_inst_updates) return knd_NOMEM;
    upd = &self->rel_inst_updates[self->num_rel_inst_updates];
    memset(upd, 0, sizeof(struct kndRelInstanceUpdate));

    self->num_rel_inst_updates++;
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

static int new_proc_entry(struct kndMemPool *self,
                          struct kndProcEntry **result)
{
    struct kndProcEntry *entry;
    if (self->num_proc_entries >= self->max_proc_entries) {
        return knd_LIMIT;
    }
    entry = &self->proc_entries[self->num_proc_entries];
    memset(entry, 0, sizeof(struct kndProcEntry));
    self->num_proc_entries++;
    *result = entry;
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
    //kndProcArg_init(arg, NULL);
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

extern int knd_mempool_alloc(struct kndMemPool *self,
                             knd_mempage_t page_size,
                             size_t obj_size, void **result)
{
    struct kndMemPage *page = NULL, *prev_page, **head_page, **tail_page;
    size_t *pages_used;
    size_t max_pages = 0;
    size_t max_page_payload_size;

    //knd_log(".. alloc: page_type:%d size:%zu", page_size, obj_size);
    
    switch (page_size) {
    case KND_MEMPAGE_NORMAL:
        max_page_payload_size = self->page_payload_size;
        head_page = &self->head_page;
        tail_page = &self->tail_page;
        pages_used = &self->pages_used;
        max_pages = self->num_pages;
        break;
    case KND_MEMPAGE_MED:
        max_page_payload_size = self->med_page_payload_size;
        head_page = &self->head_med_page;
        tail_page = &self->tail_med_page;
        pages_used = &self->med_pages_used;
        max_pages = self->num_med_pages;
        break;
    case KND_MEMPAGE_SMALL:
        max_page_payload_size = self->small_page_payload_size;
        head_page = &self->head_small_page;
        tail_page = &self->tail_small_page;
        pages_used = &self->small_pages_used;
        max_pages = self->num_small_pages;
        break;
    case KND_MEMPAGE_TINY:
        max_page_payload_size = self->tiny_page_payload_size;
        head_page = &self->head_tiny_page;
        tail_page = &self->tail_tiny_page;
        pages_used = &self->tiny_pages_used;
        max_pages = self->num_tiny_pages;
        break;
    case KND_MEMPAGE_LARGE:
        max_page_payload_size = self->page_payload_size;
        head_page = &self->head_page;
        tail_page = &self->tail_page;
        pages_used = &self->pages_used;
        max_pages = self->num_pages;
        break;
    default:
        max_page_payload_size = self->page_payload_size;
        head_page = &self->head_page;
        tail_page = &self->tail_page;
        pages_used = &self->pages_used;
        max_pages = self->num_pages;
        break;
    }

    if (obj_size >= max_page_payload_size) {
        knd_log("-- mem page size exceeded: %zu", obj_size);
        return knd_LIMIT;
    }

    if (*pages_used + 1 > max_pages) {
        knd_log("-- mem limit reached: max pages:%zu [%d]",
                max_pages, page_size);
        return knd_LIMIT;
    }

    //knd_log("head:%p tail:%p page:%p", *head_page, *tail_page, page);

    page = *tail_page;
    prev_page = page->prev;
    prev_page->next = NULL;
    *tail_page = prev_page;

    page->prev = NULL;
    page->next = *head_page;
    (*head_page)->prev = page;
    *head_page = page;
    (*pages_used)++;

    *result = (void*)page->data;
    return knd_OK;
}

extern void knd_mempool_free(struct kndMemPool *self,
                             knd_mempage_t page_size,
                             void *page_data)
{
    struct kndMemPage *page = NULL; //, *prev_page, **head_page, **tail_page;
    size_t offset;
    char *c;

    knd_log(".. free page:%p", page_data);
    c = (char*)page_data - sizeof(struct kndMemPage) + sizeof(void*);
    page = (void*)c;
    knd_log(".. page prev:%p page next:%p", page->prev, page->next);
}

static void build_linked_list(char *pages,
                              size_t num_pages,
                              size_t page_size,
                              struct kndMemPage **head_page,
                              struct kndMemPage **tail_page)
{
    struct kndMemPage *page, *prev_page = NULL;
    char *c = NULL;
    size_t offset = 0;

    *head_page = (struct kndMemPage*)pages;
    for (size_t i = 0; i < num_pages; i++) {
        offset = (i * page_size);
        c = pages + offset;
        page = (struct kndMemPage*)c;
        c = (char*)page + sizeof(struct kndMemPage);
        page->data = (void*)c;

        //knd_log("%zi) offset:%zu page:%p", i, offset, page);

        if (prev_page) {
            prev_page->next = page;
            page->prev = prev_page;
        }
        prev_page = page;
    }
    *tail_page = page;
}

static int alloc(struct kndMemPool *self)
{
    size_t total_alloc_size = 0;

    /* regular pages */
    if (!self->page_size)
        self->page_size = KND_MEMPAGE_SIZE;
    if (self->page_size <= sizeof(struct kndMemPage)) return knd_LIMIT;
    self->page_payload_size = self->page_size - sizeof(struct kndMemPage);
    if (!self->num_pages)
        self->num_pages = KND_NUM_MEMPAGES;

    self->pages = calloc(self->num_pages, self->page_size);
    if (!self->pages) {
        knd_log("-- mem pages not allocated :(");
        return knd_NOMEM;
    }

    /* medium size pages */
    if (!self->med_page_size)
        self->med_page_size = KND_MED_MEMPAGE_SIZE;
    if (self->med_page_size <= sizeof(struct kndMemPage)) return knd_LIMIT;
    self->med_page_payload_size = self->med_page_size - sizeof(struct kndMemPage);
    if (!self->num_med_pages)
        self->num_med_pages = KND_NUM_MED_MEMPAGES;

    self->med_pages = calloc(self->num_med_pages, self->med_page_size);
    if (!self->med_pages) {
        knd_log("-- mem pages not allocated :(");
        return knd_NOMEM;
    }

    
    /* small pages */
    if (!self->small_page_size)
        self->small_page_size = KND_SMALL_MEMPAGE_SIZE;
    if (self->small_page_size <= sizeof(struct kndMemPage)) return knd_LIMIT;
    self->small_page_payload_size = self->small_page_size - sizeof(struct kndMemPage);
    if (!self->num_small_pages)
        self->num_small_pages = KND_NUM_SMALL_MEMPAGES;

    self->small_pages = calloc(self->num_small_pages, self->small_page_size);
    if (!self->small_pages) {
        knd_log("-- mem pages not allocated :(");
        return knd_NOMEM;
    }

    /* tiny pages */
    if (!self->tiny_page_size)
        self->tiny_page_size = KND_TINY_MEMPAGE_SIZE;
    if (self->tiny_page_size <= sizeof(struct kndMemPage)) return knd_LIMIT;
    self->tiny_page_payload_size = self->tiny_page_size - sizeof(struct kndMemPage);
    if (!self->num_tiny_pages)
        self->num_tiny_pages = KND_NUM_TINY_MEMPAGES;

    self->tiny_pages = calloc(self->num_tiny_pages, self->tiny_page_size);
    if (!self->tiny_pages) {
        knd_log("-- mem pages not allocated :(");
        return knd_NOMEM;
    }

    build_linked_list(self->pages, self->num_pages, self->page_size,
                      &self->head_page, &self->tail_page);

    build_linked_list(self->med_pages, self->num_med_pages, self->med_page_size,
                      &self->head_med_page, &self->tail_med_page);

    build_linked_list(self->small_pages, self->num_small_pages, self->small_page_size,
                      &self->head_small_page, &self->tail_small_page);

    build_linked_list(self->tiny_pages, self->num_tiny_pages, self->tiny_page_size,
                      &self->head_tiny_page, &self->tail_tiny_page);

    total_alloc_size = (self->num_pages * self->page_size);
    total_alloc_size += self->num_med_pages * self->med_page_size;
    total_alloc_size += self->num_small_pages * self->small_page_size;
    total_alloc_size += self->num_tiny_pages * self->tiny_page_size;

    knd_log("== MemPool total bytes alloc'd: %zu", total_alloc_size);
    
    if (!self->max_sets)         self->max_sets =              KND_MIN_SETS;
    if (!self->max_set_elem_idxs) self->max_set_elem_idxs =    KND_MIN_SETS;
    if (!self->max_user_ctxs)       self->max_user_ctxs =  KND_MIN_USER_CONTEXTS;
    if (!self->max_facets)       self->max_facets =  KND_MIN_FACETS;
    if (!self->max_updates)      self->max_updates = KND_MIN_UPDATES;
    if (!self->max_states)       self->max_states =  KND_MIN_STATES;
    if (!self->max_class_updates) self->max_class_updates = KND_MIN_UPDATES;
    if (!self->max_users)        self->max_users =   KND_MIN_USERS;

    if (!self->max_objs)         self->max_objs =    KND_MIN_OBJS;
    if (!self->max_obj_dirs)     self->max_obj_dirs = self->max_objs;
    if (!self->max_obj_entries)  self->max_obj_entries = self->max_objs;
    if (!self->max_elems)        self->max_elems = self->max_objs;
    if (!self->max_rels)         self->max_rels =    KND_MIN_RELS;
    if (!self->max_rel_entries)     self->max_rel_entries = KND_MIN_REL_INSTANCES;

    if (!self->max_rel_refs)     self->max_rel_refs = self->max_rels;
    if (!self->max_rel_insts)    self->max_rel_insts = KND_MIN_REL_INSTANCES;
    if (!self->max_rel_arg_insts) self->max_rel_arg_insts = KND_MIN_RELARG_INSTANCES;
    if (!self->max_rel_arg_inst_refs) self->max_rel_arg_inst_refs = self->max_rel_arg_insts;

    if (!self->max_rel_updates) self->max_rel_updates = KND_MIN_UPDATES;
    if (!self->max_rel_inst_updates) self->max_rel_inst_updates = self->max_rel_insts;

    if (!self->max_procs)         self->max_procs =  KND_MIN_PROCS;
    if (!self->max_proc_entries)     self->max_proc_entries = self->max_procs;
    if (!self->max_proc_args)     self->max_proc_args =  self->max_procs;
    if (!self->max_proc_insts)    self->max_proc_insts = KND_MIN_PROC_INSTANCES;
    if (!self->max_proc_updates) self->max_proc_updates = KND_MIN_UPDATES;
    if (!self->max_proc_update_refs) self->max_proc_update_refs = KND_MIN_UPDATES;

    self->sets = calloc(self->max_sets, sizeof(struct kndSet));
    if (!self->sets) {
        knd_log("-- sets not allocated :(");
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

    self->user_ctxs = calloc(self->max_user_ctxs, sizeof(struct kndUserContext));
    if (!self->user_ctxs) {
        knd_log("-- user contexts not allocated :(");
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

    self->objs = calloc(self->max_objs, sizeof(struct kndClassInst));
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

    self->rel_entries = calloc(self->max_rel_entries, sizeof(struct kndRelEntry));
    if (!self->rel_entries) {
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

    // TODO: set max
    self->rel_inst_updates = calloc(self->max_rel_insts, sizeof(struct kndRelInstanceUpdate));
    if (!self->rel_inst_updates) {
        knd_log("-- rel inst updates not allocated :(");
        return knd_NOMEM;
    }

    self->procs = calloc(self->max_procs, sizeof(struct kndProc));
    if (!self->procs) {
        knd_log("-- procs not allocated :(");
        return knd_NOMEM;
    }

    self->proc_entries = calloc(self->max_proc_entries, sizeof(struct kndProcEntry));
    if (!self->proc_entries) {
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

    //knd_log("++ mempool alloc'd!");
    return knd_OK;
}

static gsl_err_t
parse_memory_settings(struct kndMemPool *self, const char *rec, size_t *total_size)
{
    struct gslTaskSpec specs[] = {
        {   .name = "max_user_ctxs",
            .name_size = strlen("max_use_ctxs"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_user_ctxs
        },
        {   .name = "max_normal_pages",
            .name_size = strlen("max_normal_pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->num_pages
        },
        {   .name = "max_med_pages",
            .name_size = strlen("max_med_pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->num_med_pages
        },
        {   .name = "max_small_pages",
            .name_size = strlen("max_small_pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->num_small_pages
        },
        {   .name = "max_tiny_pages",
            .name_size = strlen("max_tiny_pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->num_tiny_pages
        },
        {
            .name = "max_states",
            .name_size = strlen("max_states"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_states
        },
        {
            .name = "max_updates",
            .name_size = strlen("max_updates"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_updates
        },
        {
            .name = "max_class_updates",
            .name_size = strlen("max_class_updates"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_class_updates
        },
        {
            .name = "max_rel_updates",
            .name_size = strlen("max_rel_updates"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_rel_updates
        },
        {
            .name = "max_objs",
            .name_size = strlen("max_objs"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_objs
        },
        {
            .name = "max_elems",
            .name_size = strlen("max_elems"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_elems
        },
        {
            .name = "max_rels",
            .name_size = strlen("max_rels"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_rels
        },
        {
            .name = "max_rel_args",
            .name_size = strlen("max_rels_args"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_rel_args
        },
        {
            .name = "max_rel_refs",
            .name_size = strlen("max_rel_refs"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_rel_refs
        },
        {
            .name = "max_rel_instances",
            .name_size = strlen("max_rel_instances"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_rel_insts
        },
        {
            .name = "max_rel_arg_instances",
            .name_size = strlen("max_rel_arg_instances"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_rel_arg_insts
        },
        {
            .name = "max_rel_arg_inst_refs",
            .name_size = strlen("max_rel_arg_inst_refs"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_rel_arg_inst_refs
        },
        {
            .name = "max_procs",
            .name_size = strlen("max_procs"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_procs
        },
        {
            .name = "max_proc_instances",
            .name_size = strlen("max_proc_instances"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_proc_insts
        },
        {
            .name = "max_sets",
            .name_size = strlen("max_sets"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_sets
        },
        {
            .name = "max_set_elem_idxs",
            .name_size = strlen("max_set_elem_idxs"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_set_elem_idxs
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

extern void kndMemPool_init(struct kndMemPool *self)
{
    self->del = del;

    self->parse = parse_memory_settings;
    self->alloc = alloc;
    self->present = present_status;

    self->new_set = new_set;
    self->new_set_elem_idx = new_set_elem_idx;
    self->new_facet = new_facet;
    self->new_user_ctx = new_user_ctx;
    self->new_update = new_update;
    self->new_state = new_state;
    self->new_class_update = new_class_update;

    self->new_class_inst = new_class_inst;
    self->new_class_inst_dir = new_class_inst_dir;
    self->new_class_inst_entry = new_class_inst_entry;
    self->new_class_inst_elem = new_class_inst_elem;

    self->new_rel = new_rel;
    self->new_rel_entry = new_rel_entry;
    self->new_rel_ref = new_rel_ref;
    self->new_rel_inst = new_rel_inst;
    self->new_rel_arg_inst = new_rel_arg_inst;
    self->new_rel_arg_inst_ref = new_rel_arg_inst_ref;
    self->new_rel_update = new_rel_update;
    self->new_rel_inst_update = new_rel_inst_update;

    self->new_proc = new_proc;
    self->new_proc_entry = new_proc_entry;
    self->new_proc_arg = new_proc_arg;
    self->new_proc_update = new_proc_update;
    self->new_proc_update_ref = new_proc_update_ref;
}

extern int kndMemPool_new(struct kndMemPool **obj)
{
    struct kndMemPool *self;
    int err;
    self = malloc(sizeof(struct kndMemPool));
    if (!self) return knd_NOMEM;
    memset(self, 0, sizeof(struct kndMemPool));

    err = glbOutput_new(&self->log, KND_MED_BUF_SIZE);
    if (err != knd_OK) goto error;

    kndMemPool_init(self);
    *obj = self;
    return knd_OK;

error:
    del(self);
    return err;
}

