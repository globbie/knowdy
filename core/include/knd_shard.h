#pragma once

struct kndShard
{
    char id[KND_ID_SIZE];
    size_t id_size;
    char name[KND_NAME_SIZE];
    size_t name_size;

    struct kndUser *user;

    /**********  interface methods  **********/
    int (*del)(struct kndShard *self);
    int (*str)(struct kndShard *self);
    int (*init)(struct kndShard *self);
};

extern int kndShard_init(struct kndShard *self);
extern int kndShard_new(struct kndShard **self);
