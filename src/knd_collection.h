#ifndef KND_COLL_H
#define KND_COLL_H

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
    
    char *path;
    size_t path_size;

    bool is_daemon;
    char *pid_filename;

    void *context;

    char *request_proxy_frontend;
    char *request_proxy_backend;

    char *record_proxy_frontend;
    char *record_proxy_backend;

    char *publish_proxy_frontend;
    char *publish_proxy_backend;

    char *select_proxy_frontend;
    char *select_proxy_backend;
    
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
#endif
