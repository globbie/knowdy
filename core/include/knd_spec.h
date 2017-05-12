/**
 *   Copyright (c) 2011-2017 by Dmitri Dmitriev
 *   All rights reserved.
 *
 *   This file is part of the Knowdy Search Engine, 
 *   and as such it is subject to the license stated
 *   in the LICENSE file which you have received 
 *   as part of this distribution.
 *
 *   Project homepage:
 *   <http://www.globbie.net>
 *
 *   Initial author and maintainer:
 *         Dmitri Dmitriev aka M0nsteR <dmitri@globbie.net>
 *
 *   ----------
 *   knd_spec.h
 *   Knowdy Spec
 */

#ifndef KND_SPEC_H
#define KND_SPEC_H

#include "knd_config.h"

struct kndOutput;


typedef enum knd_agent_type { KND_AGENT_DEFAULT, 
                              KND_AGENT_AUTH,
                              KND_AGENT_USER,
                              KND_AGENT_REPO,
                              KND_AGENT_CONCEPT
} knd_agent_type;


struct kndSpecAgent
{
    char name[KND_NAME_SIZE];
    size_t name_size;
    knd_agent_type type;
    
};


struct kndSpecArg
{
    char name[KND_NAME_SIZE];
    size_t name_size;
    char val[KND_NAME_SIZE];
    size_t val_size;
};


struct kndSpecInstruction
{
    char name[KND_NAME_SIZE];
    size_t name_size;
    knd_agent_type type;

    char repo_name[KND_NAME_SIZE];
    size_t repo_name_size;
    size_t repo_state;

    char user_name[KND_NAME_SIZE];
    size_t user_name_size;

    char class_name[KND_NAME_SIZE];
    size_t class_name_size;

    char proc_name[KND_NAME_SIZE];
    size_t proc_name_size;

    char *obj;
    size_t obj_size;

    struct kndSpecArg args[KND_MAX_INSTRUCTION_ARGS];
    size_t num_args;
};



struct kndSpec
{
    struct kndSpecInstruction instructions[KND_MAX_INSTRUCTIONS];
    size_t num_instructions;

    char sid[KND_NAME_SIZE];
    size_t sid_size;

    char uid[KND_NAME_SIZE];
    size_t uid_size;

    char tid[KND_NAME_SIZE];
    size_t tid_size;

    char *obj;
    size_t obj_size;
    
    struct kndOutput *out;
    
    /******** public methods ********/
    void (*str)(struct kndSpec *self,
               size_t depth);

    void (*del)(struct kndSpec *self);

    void (*reset)(struct kndSpec *self);
    
    int (*parse)(struct kndSpec *self,
                 char     *rec,
                 size_t          *total_size);
    
    int (*export)(struct kndSpec *self,
                  knd_format format);
};

/* constructor */
extern int kndSpec_new(struct kndSpec **self);

#endif /* KND_SPEC_H */
