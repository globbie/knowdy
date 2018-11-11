/**
 *   Copyright (c) 2011-2018 by Dmitri Dmitriev
 *   All rights reserved.
 *
 *   This file is part of the Knowdy Graph DB, 
 *   and as such it is subject to the license stated
 *   in the LICENSE file which you have received 
 *   as part of this distribution.
 *
 *   Project homepage:
 *   <http://www.knowdy.net>
 *
 *   Initial author and maintainer:
 *         Dmitri Dmitriev aka M0nsteR <dmitri@globbie.net>
 *
 *   ----------
 *   knd_rel.h
 *   Knowdy Rel Element
 */

#pragma once

#include "knd_config.h"

struct glbOutput;
struct kndTask;
struct kndRelArg;
struct kndRelArgInstance;
struct kndRelInstance;
struct kndUpdate;
struct kndRelUpdate;
struct kndRepo;
struct kndSet;
struct kndTask;

struct kndRelInstEntry
{
    char id[KND_ID_SIZE];
    size_t id_size;
    char name[KND_NAME_SIZE];
    size_t name_size;
    struct kndRelInstance *inst;

    char *block;
    size_t block_size;
    size_t offset;
};

struct kndRelInstance
{
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;
    const char *name;
    size_t name_size;

    struct kndRelInstEntry *entry;
    struct kndTask *task;

    struct kndState *states;
    size_t num_states;

    size_t depth;
    size_t max_depth;

    struct kndRel *rel;
    struct kndRelArgInstance *args;

    struct kndRelInstance *next;
};

struct kndRelEntry
{
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;

    const char *name;
    size_t name_size;
    struct kndRel *rel;
    struct kndRepo *repo;

    int fd;
    size_t global_offset;
    size_t curr_offset;
    size_t block_size;

    size_t body_size;
    size_t obj_block_size;
    size_t dir_size;

    struct kndRelEntry **children;
    size_t num_children;

    struct kndSet *inst_idx;
    size_t num_insts;
    struct ooDict *inst_name_idx;

    bool is_terminal;
    struct kndRelEntry *next;
};

struct kndRelRef
{
    struct kndRel *rel;
    struct kndSet *idx;
    size_t num_insts;

    struct kndState *states;
    size_t init_state;
    size_t num_states;

    struct kndRelRef *next;
};

struct kndRel
{
    const char *name;
    size_t name_size;

    char *id;
    size_t id_size;

    struct kndState *states;
    size_t num_states;
    int fd;

    struct kndTranslation *tr;

    struct kndRepo *repo;

    struct glbOutput *out;
    struct glbOutput *dir_out;
    struct glbOutput *log;

    const char *dbpath;
    size_t dbpath_size;

    struct kndTask *task;
    const char *locale;
    size_t locale_size;
    knd_format format;

    bool batch_mode;

    struct kndRelArg *args;
    struct kndRelArg *tail_arg;
    size_t num_args;

    /* allocator */
    struct kndMemPool *mempool;

    struct kndUpdate *curr_update;

    /* incoming */
    struct kndRel *inbox;
    size_t inbox_size;
    size_t num_rels;

    struct kndRelInstance *inst_inbox;
    size_t inst_inbox_size;

    struct kndRelEntry *entry;

    struct ooDict *rel_name_idx;
    struct ooDict *rel_idx;

    struct kndSet *class_idx;
    struct ooDict *class_name_idx;

    const char *frozen_output_file_name;
    size_t frozen_output_file_name_size;
    size_t frozen_size;

//    char *trailer_buf;
//    size_t trailer_buf_size;
//    size_t trailer_max_buf_size;

    bool is_resolved;
    size_t depth;
    size_t max_depth;
    struct kndRel *curr_rel;
    struct kndRelInstance *curr_inst;
    struct kndRel *next;

    /******** public methods ********/
    void (*str)(struct kndRel *self);

    void (*del)(struct kndRel *self);
    void (*reset_inbox)(struct kndRel *self);

    int (*get_rel)(struct kndRel *self,
                   const char *name, size_t name_size,
                   struct kndRel **result);

//    int (*parse)(struct kndRel *self,
//                 const char    *rec,
//                 size_t        *total_size);

    int (*read_inst)(struct kndRel *self,
		     struct kndRelInstance *inst,
		     const char *rec,
		     size_t *total_size);

    int (*unfreeze_inst)(struct kndRel *self,
                         struct kndRelInstEntry *entry);

    gsl_err_t (*parse_liquid_updates)(struct kndRel *self,
				const char    *rec,
				size_t        *total_size);
    gsl_err_t (*import)(struct kndRel *self,
                  const char    *rec,
                  size_t        *total_size);
    gsl_err_t (*read)(struct kndRel *self,
                const char    *rec,
                size_t        *total_size);
    int (*read_rel)(struct kndRel *self,
                    struct kndRelEntry *entry,
                    int fd);
    gsl_err_t (*select)(struct kndRel  *self,
                        const char *rec,
                        size_t *total_size,
                        struct kndTask *task);
    int (*coordinate)(struct kndRel *self);
    int (*resolve)(struct kndRel *self, struct kndRelUpdate *update);
    int (*update)(struct kndRel *self, struct kndUpdate *update);
    int (*freeze)(struct kndRel *self,
                  size_t *total_frozen_size,
                  char *output,
                  size_t *total_size,
                  struct kndTask *task);
    int (*export)(struct kndRel *self, struct kndTask *task);
    int (*export_updates)(struct kndRel *self);
    int (*export_inst)(struct kndRelInstance *inst,
                       struct kndTask *task);
    int (*export_inst_set)(struct kndSet *set,
                           struct kndTask *task);
};

extern void kndRel_init(struct kndRel *self);
extern void kndRelInstance_init(struct kndRelInstance *inst);
extern int kndRel_new(struct kndRel **self, struct kndMemPool *mempool);
extern int knd_rel_entry_new(struct kndMemPool *mempool,
                             struct kndRelEntry **result);
extern int knd_rel_inst_new(struct kndMemPool *mempool,
                            struct kndRelInstance **result);
extern int knd_rel_new(struct kndMemPool *mempool,
                       struct kndRel **result);
