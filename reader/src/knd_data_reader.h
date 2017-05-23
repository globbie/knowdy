#ifndef KND_DATAREADER_H
#define KND_DATAREADER_H


#include "knd_utils.h"
#include "knd_repo.h"

struct kndObject;
struct kndDataClass;
struct kndOutput;
struct kndUser;
struct kndSpec;

struct kndDataReader
{
    char *name;
    size_t name_size;

    char *path;
    size_t path_size;

    char *webpath;
    size_t webpath_size;

    char default_repo_name[KND_NAME_SIZE];
    size_t default_repo_name_size;

    char default_repo_title[KND_TEMP_BUF_SIZE];
    size_t default_repo_title_size;
    
    size_t curr_state;

    /* services */
    void *monitor;
    char *monitor_addr;
    size_t monitor_addr_size;

    void *delivery;
    char *delivery_addr;
    size_t delivery_addr_size;

    char *inbox_frontend_addr;
    size_t inbox_frontend_addr_size;

    char *inbox_backend_addr;
    size_t inbox_backend_addr_size;

    char *update_addr;
    size_t update_addr_size;
    void *update;


    struct ooDict *repo_idx;

    struct kndOutput *out;
    struct kndOutput *spec_out;
    struct kndOutput *obj_out;
    
    size_t maze_cache_size;

    char *last_obj_id;
    char *curr_obj_id;

    /* policies */
    
    /*struct kndPolicy *policy_storage;
    size_t policy_storage_size;
    
    struct ooDict *policy_index;
    struct kndPolicy **policies;
    size_t num_policies;
    */

    
    char lang_code[KND_NAME_SIZE];
    size_t lang_code_size;

    struct kndSpec *spec;
    struct kndUser *admin;
    struct kndUser *curr_user;

    /**********  interface methods  **********/
    int (*del)(struct kndDataReader *self);

    int (*str)(struct kndDataReader *self);

    int (*init)(struct kndDataReader *self);

    int (*start)(struct kndDataReader *self);

    int (*get_repo)(struct kndDataReader *self,
                    const char *name,
                    size_t name_size,
                    struct kndRepo **repo);

    void (*reset)(struct kndDataReader *self);

    int (*get_class)(struct kndDataReader *self,
                     const char *classname,
                     struct kndDataClass **result);
};

extern int kndDataReader_new(struct kndDataReader **self,
			    const char *config);
#endif
