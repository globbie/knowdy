#ifndef KND_DELIVERY_H
#define KND_DELIVERY_H

#define KND_NUM_AGENTS 1

#include <time.h>

#include <knd_dict.h>
#include <knd_utils.h>

#include "knd_monitor.h"


struct kndAuthRec
{
    char uid[KND_ID_SIZE];
    size_t num_requests;

    struct ooDict *cache;

    /* TODO: billing data */

    /* one time passwords */
    
};

struct kndResult
{
    knd_proc_state_t proc_state;
    knd_format format;
    
    char   *header;
    size_t header_size;

    char *body;
    size_t body_size;

    /* file */
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

struct kndDelivery
{
    char name[KND_NAME_SIZE];
    size_t name_size;
    
    char path[KND_TEMP_BUF_SIZE];
    size_t path_size;
    
    char addr[KND_TEMP_BUF_SIZE];
    size_t addr_size;

    char inbox[KND_TEMP_BUF_SIZE];
    size_t inbox_size;

    char tid[KND_NAME_SIZE];
    size_t tid_size;

    struct ooDict *auth_idx;
    struct ooDict *sid_idx;
    struct ooDict *uid_idx;

    struct kndAuthRec *default_rec;
    struct kndAuthRec *spec_rec;
    
    struct ooDict *repo_idx;

    char *task;
    size_t task_size;
    char *obj;
    size_t obj_size;
    
    struct kndOutput *out;

    struct kndMonitor *monitor;
    
    /**********  interface methods  **********/
    int (*del)(struct kndDelivery *self);
    int (*str)(struct kndDelivery *self);

    int (*start)(struct kndDelivery *self);

    int (*process)(struct kndDelivery *self, 
		   struct kndData *data);

    int (*add)(struct kndDelivery *self, 
		   struct kndData *data);

    int (*get)(struct kndDelivery *self, 
		   struct kndData *data);

};

extern int kndDelivery_new(struct kndDelivery **self, 
			  const char *config);
#endif
