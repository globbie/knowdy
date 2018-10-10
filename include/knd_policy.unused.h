#ifndef KND_POLICY_H
#define KND_POLICY_H

#include "knd_config.h"

struct kndPolicy
{
    char id[KND_ID_SIZE + 1];
    bool is_default;

    /**********  interface methods  **********/
    int (*del)(struct kndPolicy *self);

    int (*str)(struct kndPolicy *self);

    int (*init)(struct kndPolicy *self);

    int (*read)(struct kndPolicy *self,
                const char *id);
};

extern int kndPolicy_init(struct kndPolicy *self);
extern int kndPolicy_new(struct kndPolicy **self);
#endif
