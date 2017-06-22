#pragma once

#include "knd_config.h"
#include "knd_output.h"

struct kndCollRef
{
    char *name;
    char *service_address;

    struct kndCollRef *next;
};


struct kndColl
{
    char *name;
    size_t name_size;

    void *context;
    struct kndOutput *out;
    
    char request_proxy_frontend[KND_NAME_SIZE];
    size_t request_proxy_frontend_size;
    char request_proxy_backend[KND_NAME_SIZE];
    size_t request_proxy_backend_size;

    char record_proxy_frontend[KND_NAME_SIZE];
    size_t record_proxy_frontend_size;
    char record_proxy_backend[KND_NAME_SIZE];
    size_t record_proxy_backend_size;

    char publish_proxy_frontend[KND_NAME_SIZE];
    size_t publish_proxy_frontend_size;
    char publish_proxy_backend[KND_NAME_SIZE];
    size_t publish_proxy_backend_size;

    char select_proxy_frontend[KND_NAME_SIZE];
    size_t select_proxy_frontend_size;
    char select_proxy_backend[KND_NAME_SIZE];
    size_t select_proxy_backend_size;
    
    size_t max_num_objs;
    size_t max_storage_size;

    struct kndCollRef *children;

    /**********  interface methods  **********/
    int (*del)(struct kndColl *self);
    int (*str)(struct kndColl *self);
   
    int (*add)(struct kndColl *self,
	       const char *spec,
	       const char *obj);

    int (*get)(struct kndColl *self,
	       const char *spec);

    int (*find)(struct kndColl *self,
		const char *spec,
		const char *request);

    int (*remove)(struct kndColl *self,
		  const char *spec);

    int (*start)(struct kndColl *self);

    int (*find_route)(struct kndColl *self,
		      const char *spec,
		      const char **dest_coll_addr);


};

extern int kndColl_new(struct kndColl **self,
		       const char *config);
