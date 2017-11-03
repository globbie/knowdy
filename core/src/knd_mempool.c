#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_mempool.h"
#include "knd_output.h"
#include "knd_concept.h"
#include "knd_object.h"
#include "knd_rel.h"
#include "knd_proc.h"

static void del(struct kndMemPool *self)
{
    free(self);
}

static int new_class(struct kndMemPool *self,
                     struct kndConcept **result)
{
    struct kndConcept *c;
    int e;

    if (self->num_classes >= self->max_classes) {
        self->log->reset(self->log);
        e = self->log->write(self->log, "memory limit reached",
                             strlen("memory limit reached"));
        if (e) return e;
        
        knd_log("-- memory limit reached :(");
        return knd_MAX_LIMIT_REACHED;
    }
    c = &self->classes[self->num_classes];
    memset(c, 0, sizeof(struct kndConcept));
    kndConcept_init(c);
    self->num_classes++;
    *result = c;
    return knd_OK;
}

static int new_obj(struct kndMemPool *self,
                   struct kndObject **result)
{
    struct kndObject *obj;
    int e;

    if (self->num_objs >= self->max_objs) {
        self->log->reset(self->log);
        e = self->log->write(self->log, "memory limit reached",
                             strlen("memory limit reached"));
        if (e) return e;

        knd_log("-- memory limit reached :(");
        return knd_MAX_LIMIT_REACHED;
    }
    obj = &self->objs[self->num_objs];
    memset(obj, 0, sizeof(struct kndObject));
    kndObject_init(obj);
    self->num_objs++;
    *result = obj;
    return knd_OK;
}

static int new_rel(struct kndMemPool *self,
                   struct kndRel **result)
{
    struct kndRel *rel;
    int e;

    if (self->num_rels >= self->max_rels) {
        self->log->reset(self->log);
        e = self->log->write(self->log, "memory limit reached",
                             strlen("memory limit reached"));
        if (e) return e;
        knd_log("-- Rel pool memory limit reached :(");
        return knd_MAX_LIMIT_REACHED;
    }

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
        return knd_MAX_LIMIT_REACHED;
    }
    rel_inst = &self->rel_insts[self->num_rel_insts];
    memset(rel_inst, 0, sizeof(struct kndRelInstance));
    kndRelInstance_init(rel_inst);
    self->num_rel_insts++;
    *result = rel_inst;
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
        return knd_MAX_LIMIT_REACHED;
    }

    proc = &self->procs[self->num_procs];
    memset(proc, 0, sizeof(struct kndProc));
    kndProc_init(proc);
    self->num_procs++;
    *result = proc;
    return knd_OK;
}

static int alloc(struct kndMemPool *self)
{
    if (!self->max_classes)  self->max_classes = KND_MIN_CLASSES;
    if (!self->max_objs)     self->max_objs =    KND_MIN_OBJS;
    if (!self->max_rels)     self->max_rels =    KND_MIN_RELS;
    if (!self->max_rel_insts) self->max_rel_insts = KND_MIN_REL_INSTANCES;
    if (!self->max_procs)    self->max_procs =  KND_MIN_PROCS;
    if (!self->max_proc_insts) self->max_proc_insts = KND_MIN_PROC_INSTANCES;

    self->classes = calloc(self->max_classes, sizeof(struct kndConcept));
    if (!self->classes) {
        knd_log("-- classes not allocated :(");
        return knd_NOMEM;
    }

    self->objs = calloc(self->max_objs, sizeof(struct kndObject));
    if (!self->objs) {
        knd_log("-- objs not allocated :(");
        return knd_NOMEM;
    }

    self->rels = calloc(self->max_rels, sizeof(struct kndRel));
    if (!self->rels) {
        knd_log("-- rels not allocated :(");
        return knd_NOMEM;
    }

    self->rel_insts = calloc(self->max_rel_insts, sizeof(struct kndRelInstance));
    if (!self->rel_insts) {
        knd_log("-- rel insts not allocated :(");
        return knd_NOMEM;
    }

    self->procs = calloc(self->max_procs, sizeof(struct kndProc));
    if (!self->procs) {
        knd_log("-- procs not allocated :(");
        return knd_NOMEM;
    }

    self->proc_insts = calloc(self->max_proc_insts, sizeof(struct kndProcInstance));
    if (!self->proc_insts) {
        knd_log("-- proc insts not allocated :(");
        return knd_NOMEM;
    }

    knd_log("TOTAL allocations: classes:%zu  objs:%zu  elems:%zu  rels:%zu  procs:%zu",
            self->max_classes, self->max_objs, self->max_elems, self->max_rels, self->max_procs);

    return knd_OK;
}

extern void
kndMemPool_init(struct kndMemPool *self)
{
    self->alloc = alloc;
    self->new_class = new_class;
    self->new_obj = new_obj;
    self->new_rel = new_rel;
    self->new_rel_inst = new_rel_inst;
    self->new_proc = new_proc;
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
