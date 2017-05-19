#ifndef KND_REPO_H
#define KND_REPO_H

#include "knd_utils.h"
#include "knd_policy.h"
#include "knd_facet.h"
#include "knd_dict.h"

struct kndDataWriter;
struct kndDataReader;
struct kndDataClass;
struct kndCustomer;
struct kndRepoGroup;
struct kndObject;
struct kndRepo;
struct kndRefSet;
struct kndUser;
struct kndQuery;
struct kndOutput;

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

/* inverted rel IDX */
struct kndRelType
{
    char attr_name[KND_NAME_SIZE];
    size_t attr_name_size;

    struct kndAttr *attr;
    struct ooDict *idx;

    struct kndObjRef *refs;
    struct kndObjRef *tail;
    size_t num_refs;
    
    struct kndRelType *next;
};

struct kndRelClass
{
    struct kndDataClass *dc;
    struct kndRelType *rel_types;
    
    struct kndRelClass *next;
};


struct kndAtomIdx
{
    char name[KND_NAME_SIZE];
    size_t name_size;

    knd_facet_type type;

    struct kndRefSet *refset;
    struct kndAtomIdx *next;
};


struct kndRepoCache
{
    struct kndDataClass *baseclass;
    struct kndRepo *repo;

    struct ooDict *db;
    struct ooDict *obj_idx;
    size_t num_objs;

    size_t cache_size;
    
    char obj_last_id[KND_ID_SIZE + 1];
    
    struct kndRelClass *rel_classes;
    
    struct kndRefSet *browser;
    struct kndRefSet *name_idx;

    struct kndRefSet *idxs;
    size_t num_idxs;

    struct kndQuery *query;
    struct kndRefSet *select_result;

    struct kndObject *matches[KND_MAX_MATCHES];
    size_t num_matches;

    struct kndRepoCache *parent;
    struct kndRepoCache *children;
    size_t num_children;
    
    struct kndRepoCache *next;
};


struct kndRepoMigration
{
    char id[KND_ID_SIZE + 1];
    char name[KND_NAME_SIZE];
    size_t name_size;

    struct kndRepo *repo;
    struct kndRepoCache *cache;
};



struct kndRepo
{
    char id[KND_ID_SIZE + 1];
    char last_id[KND_ID_SIZE + 1];
    
    char name[KND_NAME_SIZE];
    size_t name_size;

    char title[KND_TEMP_BUF_SIZE];
    size_t title_size;
    
    size_t state;
    size_t match_state;
    size_t num_repos;

    bool batch_mode;
    bool restore_mode;

    char path[KND_TEMP_BUF_SIZE];
    size_t path_size;

    char sid[KND_TID_SIZE + 1];
    size_t sid_size;

    char lang_code[KND_NAME_SIZE];
    size_t lang_code_size;

    struct kndSpecInstruction *instruct;
    
    struct kndOutput *out;
    
    /* local repo index */
    struct ooDict *repo_idx;

    struct kndCustomer *customer;
    struct kndPolicy *policy;
    struct kndRepoGroup *groups;
    struct kndUser *user;


    struct kndRepoMigration *migrations[KND_MAX_MIGRATIONS];
    size_t num_migrations;
    struct kndRepoMigration *migration;
    
    struct kndRepoCache *cache;
    
    size_t intersect_matrix_size;
    
    struct kndDataClass *curr_class;

    
    /**********  interface methods  **********/
    int (*del)(struct kndRepo *self);

    int (*str)(struct kndRepo *self);

    int (*init)(struct kndRepo *self);

    int (*run)(struct kndRepo *self);

    int (*read_state)(struct kndRepo *self, char *rec, size_t *chunk_size);

    int (*add_repo)(struct kndRepo *self, struct kndSpecArg *args, size_t num_args);

    int (*get_repo)(struct kndRepo *self, const char *uid, struct kndRepo **repo);

    int (*open)(struct kndRepo *self);
    int (*restore)(struct kndRepo *self);

    int (*read)(struct kndRepo *self, const char *id);

    int (*sync)(struct kndRepo *self);

    int (*import)(struct kndRepo *self, char *rec, size_t *total_size);
    int (*update)(struct kndRepo *self, knd_format format);

    int (*export)(struct kndRepo *self, knd_format format);

    int (*liquid_select)(struct kndRepo *self, struct kndData *data);
    int (*select)(struct kndRepo *self, struct kndData *data);

    int (*get_obj)(struct kndRepo *self, struct kndData *data);
    int (*get_liquid_obj)(struct kndRepo *self, struct kndData *data);

    int (*flatten)(struct kndRepo *self, struct kndData *data);
    int (*update_flatten)(struct kndRepo *self, struct kndData *data);

    int (*match)(struct kndRepo *self, struct kndData *data);
    int (*liquid_match)(struct kndRepo *self, struct kndData *data);

    int (*get_cache)(struct kndRepo *self, struct kndDataClass *c,
                     struct kndRepoCache **cache);

    int (*get_guid)(struct kndRepo *self, struct kndDataClass *c,
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
#endif
