#ifndef KND_DATAWRITER_H
#define KND_DATAWRITER_H

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
    
struct kndDataWriter
{
    char *name;
    size_t name_size;

    char path[KND_TEMP_BUF_SIZE];
    size_t path_size;

    char schema_path[KND_TEMP_BUF_SIZE];
    size_t schema_path_size;

    void *delivery;
    char *delivery_addr;
    size_t delivery_addr_size;
    
    char *inbox_frontend_addr;
    size_t inbox_frontend_addr_size;

    char *inbox_backend_addr;
    size_t inbox_backend_addr_size;
    
    size_t state_count;
  
    char *last_obj_id;
    char *curr_obj_id;
    size_t num_objs;
    size_t max_num_objs;

    /* current class */
    char classname[KND_CONC_NAME_BUF_SIZE];
    size_t classname_size;
  
    /* persistent storage */
    char db_path[KND_MED_BUF_SIZE];
    size_t db_path_size;

    /* complex concept index */
    struct kndMaze *maze;

    struct kndOutput *out;
    struct kndOutput *spec_out;
    struct kndOutput *obj_out;

    struct kndTask *task;
    
    struct ooDict *repo_idx;
    struct kndUser *admin;

    struct kndUser *curr_user;

    /* valid classes */
    struct kndDataClass *dc;

    struct kndPolicy *policy;

    char uid[KND_SMALL_BUF_SIZE];
    size_t uid_size;

    char sid[KND_SMALL_BUF_SIZE];
    size_t sid_size;

    
    /**********  interface methods  **********/
    void (*del)(struct kndDataWriter *self);

    int (*start)(struct kndDataWriter *self);
};

extern int kndDataWriter_new(struct kndDataWriter **self,
                             const char *config);
#endif
