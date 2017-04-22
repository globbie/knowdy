#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_policy.h"
#include "knd_utils.h"

#define DEBUG_POLICY_LEVEL_0 0
#define DEBUG_POLICY_LEVEL_1 0
#define DEBUG_POLICY_LEVEL_2 0
#define DEBUG_POLICY_LEVEL_3 0
#define DEBUG_POLICY_LEVEL_TMP 1

static int 
kndPolicy_del(struct kndPolicy *self)
{

    free(self);

    return knd_OK;
}

static int 
kndPolicy_str(struct kndPolicy *self __attribute__((unused)))
{

    knd_log("POLICY");

    return knd_OK;
}


extern int 
kndPolicy_init(struct kndPolicy *self)
{
    memset(self, 0, sizeof(struct kndPolicy));

    self->del = kndPolicy_del;
    self->str = kndPolicy_str;

    return knd_OK;
}



extern int 
kndPolicy_new(struct kndPolicy **policy)
{
    struct kndPolicy *self;

    self = malloc(sizeof(struct kndPolicy));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndPolicy));


    self->del = kndPolicy_del;
    self->str = kndPolicy_str;


    *policy = self;

    return knd_OK;
}
