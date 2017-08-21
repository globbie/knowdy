#pragma once

#include "knd_utils.h"

struct kndObject;
struct kndOutput;
struct kndUser;
struct kndTask;

struct kndRetriever
{
    char name[KND_TEMP_BUF_SIZE];
    size_t name_size;

    char path[KND_TEMP_BUF_SIZE];
    size_t path_size;

    char schema_path[KND_TEMP_BUF_SIZE];
    size_t schema_path_size;

    char *webpath;
    size_t webpath_size;

    char default_repo_name[KND_NAME_SIZE];
    size_t default_repo_name_size;

    char default_repo_title[KND_TEMP_BUF_SIZE];
    size_t default_repo_title_size;
    
    size_t curr_state;

    /* services */
    char coll_request_addr[KND_NAME_SIZE];
    size_t coll_request_addr_size;

    void *delivery;
    char delivery_addr[KND_NAME_SIZE];
    size_t delivery_addr_size;

    char inbox_frontend_addr[KND_NAME_SIZE];
    size_t inbox_frontend_addr_size;

    char inbox_backend_addr[KND_NAME_SIZE];
    size_t inbox_backend_addr_size;

    /*char *update_addr;
    size_t update_addr_size;
    void *update_service;
    */
    
    struct kndOutput *out;
    struct kndOutput *spec_out;
    struct kndOutput *obj_out;
    
    size_t maze_cache_size;

    char *last_obj_id;
    char *curr_obj_id;

    char lang_code[KND_NAME_SIZE];
    size_t lang_code_size;

    struct kndTask *task;
    struct kndUser *admin;

    /**********  interface methods  **********/
    int (*del)(struct kndRetriever *self);

    int (*str)(struct kndRetriever *self);

    int (*init)(struct kndRetriever *self);

    int (*start)(struct kndRetriever *self);

    void (*reset)(struct kndRetriever *self);

};

extern int kndRetriever_new(struct kndRetriever **self,
			    const char *config);
