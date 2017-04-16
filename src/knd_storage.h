#ifndef KND_STORAGE_H
#define KND_STORAGE_H

#define KND_NUM_AGENTS 1

struct agent_args {
    int agent_id;
    struct kndStorage *storage;
};


struct kndStorage
{
    char *name;
    size_t name_size;

    char *path;
    size_t path_size;

    bool is_daemon;
    char *pid_filename;

    char *cur_id; /* next obj id */
    char *key_id;

    char **id_pool;
    size_t id_count;

    void *context;

    size_t num_agents;

    /*struct kndPartition **partitions;
      struct kndMaze **mazes; */

    struct ooDict *obj_index;

    struct ooDict *search_cache;

    /**********  interface methods  **********/
    int (*del)(struct kndStorage *self);
    int (*str)(struct kndStorage *self);

    int (*start)(struct kndStorage *self);

    /*int (*add)(struct kndStorage *self, 
      struct kndData *data);

      int (*get)(struct kndStorage *self, 
      struct kndData *data); */

};

extern int kndStorage_new(struct kndStorage **self, 
			  const char *config);
#endif
