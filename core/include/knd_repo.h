#pragma once                                    \

#include "knd_utils.h"
#include "knd_facet.h"
#include "knd_dict.h"

struct kndClass;
struct kndObject;
struct kndRepo;
struct kndRepoMigration;
struct kndRefSet;
struct kndUser;
struct kndQuery;
struct glbOutput;

struct kndSpecInstruction;
struct kndSpecArg;

struct kndFlatCell
{
    struct kndObject *obj;
    long span;
    long estim;
};

struct kndFlatRow
{
    struct kndFlatCell cols[KND_MAX_FLAT_COLS];
    size_t num_cols;
};

struct kndFlatTable
{
    struct kndFlatRow rows[KND_MAX_FLAT_ROWS];

    long totals[KND_MAX_FLAT_ROWS];
    size_t num_totals;
    
    size_t num_rows;
};

struct kndSeqAlt
{
    char seq[KND_NAME_SIZE];
    size_t seq_size;

    /* permutation cause */
    char permut_type[KND_NAME_SIZE];
    
    struct kndSeqAlt *next;
};

struct kndLinearSeqRec
{
    struct kndRefSet *refset;

    struct kndSeqAlt *alts;
};



struct kndAtomIdx
{
    char name[KND_NAME_SIZE];
    size_t name_size;

    knd_facet_type type;

    struct kndRefSet *refset;
    struct kndAtomIdx *next;
};


struct kndRepo
{
    char id[KND_ID_SIZE + 1];
    char last_id[KND_ID_SIZE + 1];
    
    char name[KND_NAME_SIZE];
    size_t name_size;

    char last_db_state[KND_ID_SIZE + 1];
    char db_state[KND_ID_SIZE + 1];

    size_t state;
    
    size_t match_state;
    size_t num_repos;

    bool batch_mode;
    bool restore_mode;

    char path[KND_TEMP_BUF_SIZE];
    size_t path_size;

    char sid[KND_TID_SIZE + 1];
    size_t sid_size;

    const char *locale;
    size_t locale_size;

    struct kndSpecInstruction *instruct;
    
    struct glbOutput *out;
    struct glbOutput *path_out;
    struct glbOutput *log;
    
    /* local repo index */
    struct ooDict *repo_idx;
    struct kndRepo *curr_repo;
    
    struct kndUser *user;
    struct kndTask *task;

    struct kndRepoMigration *migrations[KND_MAX_MIGRATIONS];
    size_t num_migrations;
    struct kndRepoMigration *migration;
    
    struct kndRepoCache *cache;
    struct kndRepoCache *curr_cache;
    
    size_t intersect_matrix_size;
    
    struct kndClass *curr_class;
    
    /**********  interface methods  **********/
    int (*del)(struct kndRepo *self);

    int (*str)(struct kndRepo *self);

    int (*init)(struct kndRepo *self);

    int (*read_state)(struct kndRepo *self, const char *rec, size_t *chunk_size);

    int (*parse_task)(void *self, const char *rec, size_t *chunk_size);

    int (*get_repo)(struct kndRepo *self, const char *uid, struct kndRepo **repo);

    int (*open)(struct kndRepo *self);
    int (*restore)(struct kndRepo *self);

    int (*read)(struct kndRepo *self, const char *id);

    int (*sync)(struct kndRepo *self);

    int (*import)(struct kndRepo *self, const char *rec, size_t *total_size);
    int (*update)(struct kndRepo *self, const char *rec);

    int (*export)(struct kndRepo *self, knd_format format);

    int (*get_obj)(struct kndRepo *self,  struct kndSpecArg *args, size_t num_args);

    int (*get_cache)(struct kndRepo *self, struct kndClass *c,
                     struct kndRepoCache **cache);

    int (*get_guid)(struct kndRepo *self, struct kndClass *c,
                    const char *obj_name,
                    size_t      obj_name_size,
                    char *result);

    int (*read_obj)(struct kndRepo *self,
                    const char *guid,
                    struct kndRepoCache *cache,
                    struct kndObject **result);
};

extern int kndRepo_init(struct kndRepo *self);
extern int kndRepo_new(struct kndRepo **self);
