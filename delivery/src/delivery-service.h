#pragma once

#include <kmq.h>

#include "knd_dict.h"
#include "knd_utils.h"
#include "knd_user.h"

struct kndAuthRec
{
    char uid[KND_ID_SIZE];
    size_t num_requests;

    struct ooDict *cache;

    // TODO: billing data
    // one time passwords
};

struct kndTID
{
    char tid[KND_TID_SIZE];
    size_t size;
};

struct kndResult
{
    //knd_proc_state_t proc_state;
    knd_format format;

    char sid[KND_SID_SIZE];
    size_t sid_size;
    bool sid_required;
    char *header;
    size_t header_size;

    char *obj;
    size_t obj_size;

    time_t start;
    time_t finish;

    // file
    char *filename;
    size_t filename_size;
    size_t filesize;
    char *mimetype;
    size_t mimetype_size;

    struct kndResult *next;
};

struct kndRepoRec
{
    char classname[KND_NAME_SIZE];

    char *obj;
    size_t obj_size;

    char *summaries;
    size_t summaries_size;

    struct ooDict *matches;

    struct kndRepoRec *next;
};

struct kndDeliveryOptions
{
    char *config_file;
};

struct kndDeliveryService
{
    struct kmqKnode *knode;
    struct kmqEndPoint *entry_point;

    const struct kndDeliveryOptions *opts;

    char name[KND_NAME_SIZE];
    size_t name_size;

    char path[KND_TEMP_BUF_SIZE];
    size_t path_size;

    char addr[KND_TEMP_BUF_SIZE];
    size_t addr_size;

    char inbox[KND_TEMP_BUF_SIZE];
    size_t inbox_size;

    char tid[KND_TID_SIZE];
    size_t tid_size;

    char sid[KND_SID_SIZE];
    size_t sid_size;

    struct ooDict *auth_idx;
    struct ooDict *sid_idx;
    struct ooDict *uid_idx;

    struct kndAuthRec *default_rec;
    struct kndAuthRec *spec_rec;

    struct ooDict *idx;

    struct kndTID *tids;
    size_t max_tids;
    size_t num_tids;

    char *task;
    size_t task_size;
    char *obj;
    size_t obj_size;

    char *reply_obj;
    size_t reply_obj_size;

    struct kndUser *admin;
    struct kndOutput *out;

    /*********************  public interface  *********************************/
    int (*start)(struct kndDeliveryService *self);
    int (*del)(struct kndDeliveryService *self);
};

int kndDeliveryService_new(struct kndDeliveryService **service, const struct kndDeliveryOptions *opts);

