#ifndef KND_LEARNER_H
#define KND_LEARNER_H

#include "knd_utils.h"
#include "knd_dict.h"

struct kndUser;

struct kndCacheRec
{
    char *key;
    size_t key_size;
    char *value;
    size_t value_size;
};
    
struct kndLearner
{
    char name[KND_NAME_SIZE];
    size_t name_size;

    char path[KND_NAME_SIZE];
    size_t path_size;

    char schema_path[KND_NAME_SIZE];
    size_t schema_path_size;

    void *delivery;
    char delivery_addr[KND_NAME_SIZE];
    size_t delivery_addr_size;
    
    char inbox_frontend_addr[KND_NAME_SIZE];
    size_t inbox_frontend_addr_size;

    char inbox_backend_addr[KND_NAME_SIZE];
    size_t inbox_backend_addr_size;
    
    size_t state_count;
  
    char *last_obj_id;
    char *curr_obj_id;
    size_t num_objs;
    size_t max_num_objs;
  
    /* persistent storage */
    char db_path[KND_MED_BUF_SIZE];
    size_t db_path_size;

    struct kndOutput *out;
    struct kndOutput *log;

    struct kndTask *task;
    
    struct ooDict *repo_idx;
    struct kndUser *admin;

    /* concept manager */
    struct kndConcept *dc;
    
    /**********  interface methods  **********/
    void (*del)(struct kndLearner *self);

    int (*start)(struct kndLearner *self);
};

extern int kndLearner_new(struct kndLearner **self,
                             const char *config);
#endif
