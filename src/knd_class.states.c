#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

/* numeric conversion by strtol */
#include <errno.h>
#include <limits.h>

#include "knd_config.h"
#include "knd_mempool.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_user.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_http_codes.h"

#include <gsl-parser.h>

#define DEBUG_CLASS_SELECT_LEVEL_1 0
#define DEBUG_CLASS_SELECT_LEVEL_2 0
#define DEBUG_CLASS_SELECT_LEVEL_3 0
#define DEBUG_CLASS_SELECT_LEVEL_4 0
#define DEBUG_CLASS_SELECT_LEVEL_5 0
#define DEBUG_CLASS_SELECT_LEVEL_TMP 1

static int retrieve_inst_updates(struct kndStateRef *ref,
                                 struct kndSet *set)
{
    struct kndState *state = ref->state;
    struct kndClassInstEntry *inst_entry;
    struct kndStateRef *child_ref;
    int err;

    knd_log("++ state: %zu  type:%d", state->numid, ref->type);

    for (child_ref = state->children; child_ref; child_ref = child_ref->next) {
        err = retrieve_inst_updates(child_ref, set);                          RET_ERR();
    }

    switch (ref->type) {
        case KND_STATE_CLASS_INST:
            inst_entry = ref->obj;

            if (DEBUG_CLASS_SELECT_LEVEL_TMP) {
                knd_log("** inst id:%.*s", inst_entry->id_size, inst_entry->id);
            }

            err = set->add(set,
                           inst_entry->id,
                           inst_entry->id_size, (void*)inst_entry);                   RET_ERR();

            /* TODO: filter out the insts
               that were created and removed _after_
               the requested update _gt */

            break;
        default:
            break;
    }




    return knd_OK;
}

int knd_class_get_inst_updates(struct kndClass *self, size_t gt, size_t lt,
                               size_t unused_var(eq),
                               struct kndSet *set)
{
    struct kndState *state;
    struct kndStateRef *ref;
    int err;

    if (DEBUG_CLASS_SELECT_LEVEL_2)
        knd_log(".. class %.*s (repo:%.*s) to extract instance updates",
                self->name_size, self->name, self->entry->repo->name_size, self->entry->repo->name);

    if (!lt) lt = self->num_inst_states + 1;

    for (state = self->inst_states; state; state = state->next) {
        if (state->numid >= lt) continue;
        if (state->numid <= gt) continue;

        // TODO
        if (!state->children) continue;

        for (ref = state->children; ref; ref = ref->next) {
            err = retrieve_inst_updates(ref, set);                    RET_ERR();
        }
    }

    return knd_OK;
}

int knd_retrieve_class_updates(struct kndStateRef *ref, struct kndSet *set)
{
    struct kndState *state = ref->state;
    struct kndClassEntry *entry;
    struct kndStateRef *child_ref;
    int err;

    knd_log("++ class state: %zu  type:%d", state->numid, ref->type);

    for (child_ref = state->children; child_ref; child_ref = child_ref->next) {
        err = knd_retrieve_class_updates(child_ref, set);                             RET_ERR();
    }

    switch (ref->type) {
        case KND_STATE_CLASS:
            entry = ref->obj;
            if (!entry) {
                knd_log("-- no class ref in state ref");
                return knd_OK;
            }

            if (DEBUG_CLASS_SELECT_LEVEL_TMP) {
                knd_log("** class:%.*s", entry->name_size, entry->name);
            }

            err = set->add(set,
                           entry->id,
                           entry->id_size, (void*)entry);                   RET_ERR();

            /* TODO: filter out the insts
               that were created and removed _after_
               the requested update _gt */

            break;
        default:
            break;
    }
    return knd_OK;
}

extern int knd_class_get_updates(struct kndClass *self,
                                 size_t gt, size_t lt,
                                 size_t unused_var(eq),
                                 struct kndSet *set)
{
    struct kndState *state;
    struct kndStateRef *ref;
    int err;

    if (DEBUG_CLASS_SELECT_LEVEL_2)
        knd_log(".. class %.*s (repo:%.*s) to extract updates..",
                self->name_size, self->name,
                self->entry->repo->name_size, self->entry->repo->name);

    if (!lt) lt = self->states->numid + 1;

    FOREACH (state, self->states) {
        if (state->numid >= lt) continue;
        if (state->numid <= gt) continue;

        // TODO
        if (!state->children) continue;
        for (ref = state->children; ref; ref = ref->next) {
            err = knd_retrieve_class_updates(ref, set);                               RET_ERR();
        }
    }

    return knd_OK;
}

extern int knd_class_get_desc_updates(struct kndClass *self,
                                      size_t gt, size_t lt,
                                      size_t unused_var(eq),
                                      struct kndSet *set)
{
    struct kndState *state;
    struct kndStateRef *ref;
    int err;

    if (DEBUG_CLASS_SELECT_LEVEL_2)
        knd_log(".. class %.*s (repo:%.*s) to extract descendant updates..",
                self->name_size, self->name,
                self->entry->repo->name_size, self->entry->repo->name);

    if (!lt) lt = self->desc_states->numid + 1;
    FOREACH (state, self->desc_states) {
        if (state->numid >= lt) continue;
        if (state->numid <= gt) continue;

        // TODO
        if (!state->children) continue;
        FOREACH (ref, state->children) {
            err = knd_retrieve_class_updates(ref, set);                               RET_ERR();
        }
    }

    return knd_OK;
}
