#pragma once

#include "knd_config.h"

struct kndPermit
{
    char id[KND_ID_SIZE];
    bool is_default;

};

extern int knd_permit_new(struct kndMemPool *mempool,
                          struct kndPermit **result);
