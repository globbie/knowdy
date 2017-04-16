#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_num.h"
#include "knd_repo.h"
#include "knd_output.h"
#include "knd_elem.h"
#include "knd_object.h"
#include "knd_objref.h"
#include "knd_utils.h"

#define DEBUG_NUM_LEVEL_0 0
#define DEBUG_NUM_LEVEL_1 0
#define DEBUG_NUM_LEVEL_2 0
#define DEBUG_NUM_LEVEL_3 0
#define DEBUG_NUM_LEVEL_TMP 1

static int 
kndNum_del(struct kndNum *self)
{

    free(self);

    return knd_OK;
}

static int 
kndNum_str(struct kndNum *self, size_t depth)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size, curr_size;
    char *c;
    size_t i, offset_size = sizeof(char) * KND_OFFSET_SIZE * depth;
    char *offset = malloc(offset_size + 1);

    struct kndNumState *curr_state;
     
    memset(offset, ' ', offset_size);
    offset[offset_size] = '\0';

    curr_state = self->states;
    while (curr_state) {

        curr_state = curr_state->next;
    }
    
    return knd_OK;
}



static int 
kndNum_index(struct kndNum *self)
{
    char buf[KND_LARGE_BUF_SIZE];
    size_t buf_size;

    struct kndObject *obj;
    struct kndNumState *curr_state;
    
    int err = knd_FAIL;

    obj = self->elem->obj;

    curr_state = self->states;
    

        
    return knd_OK;

 final:
    return err;
}


static int 
kndNum_export(struct kndNum *self,
               knd_format format)
{
    char buf[KND_LARGE_BUF_SIZE];
    size_t buf_size;

    struct kndNumState *curr_state;

    struct kndObject *obj;
    int err = knd_FAIL;

    obj = self->elem->obj;

    curr_state = self->states;

    err = knd_OK;
    
 final:
    return err;
}


static int 
kndNum_parse(struct kndNum *self,
             const char     *rec,
             size_t          *total_size)
{
    int err;




    err = knd_OK;
    
 final:
    return err;
}


extern int 
kndNum_new(struct kndNum **num)
{
    struct kndNum *self;
    
    self = malloc(sizeof(struct kndNum));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndNum));

    self->del = kndNum_del;
    self->str = kndNum_str;
    self->export = kndNum_export;
    self->parse = kndNum_parse;
    self->index = kndNum_index;

    *num = self;

    return knd_OK;
}
