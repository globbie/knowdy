struct kndAttrStmCtx
{
    struct kndAttrStm *parent_stm;
    struct kndAttrStm *attr_stm;
    struct kndTask *task;
};

struct kndAttrStmRef
{
    struct kndAttr *attr;
    struct kndState *states;
    struct kndAttrStmRef *children;

    struct kndAttrStmRef *next;
    struct kndAttrStmRef *tail;
};

struct kndAttrStm
{
    char id[KND_ID_SIZE];
    size_t id_size;
    struct kndAttr *attr;

    const char *name;
    size_t name_size;

    const char *val;
    size_t val_size;
    char val_id[KND_ID_SIZE];
    size_t val_id_size;

    struct kndCharSeq *seq;

    long numval;
    bool is_cached; // for computed fields
    knd_logic_t logic;

    struct kndAttr *implied_attr;

    struct kndClassBasePred *base_pred;

    struct kndText *text;

    struct kndAttrStm *parent;
    struct kndAttrStm *children;
    struct kndAttrStm *tail;
    size_t num_children;

    bool is_list_item;
    size_t list_count;

    struct kndState *states;
    size_t init_state;
    size_t num_states;

    /* siblings */
    struct kndAttrStm *list;
    struct kndAttrStm *list_tail;
    size_t num_list_elems;

    struct kndClassEntry *class_entry;

    const char *class_inst_name;
    size_t class_inst_name_size;
    struct kndClassInstEntry *class_inst_entry;

    struct kndProcEntry *proc_entry;

    struct kndAttr *ref_attr;

    struct kndAttrStm *next;
};

int knd_attr_stm_new(struct kndAttrStm **result, struct kndMemPool *mempool);

int knd_import_attr_stm(struct kndClassBasePred *self, const char *name, size_t name_size,
                        const char *rec, size_t *total_size, struct kndTask *task);
int knd_import_attr_stm_list(struct kndClassBasePred *self, const char *name, size_t name_size,
                             const char *rec, size_t *total_size, struct kndTask *task);

// knd_attr_stm.gsp.c
int knd_read_attr_stm(struct kndClassBasePred *self, const char *name, size_t name_size,
                      const char *rec, size_t *total_size, struct kndTask *task);
int knd_read_attr_stm_list(struct kndClassBasePred *self, const char *name, size_t name_size,
                           const char *rec, size_t *total_size, struct kndTask *task);

int knd_decode_attr_stms(struct kndClass *base, struct kndAttrStm *parent,
                         struct kndAttrStm *attr_stms, struct kndTask *task);

// knd_attr.select.c
int knd_attr_stm_match(struct kndAttrStm *self, struct kndAttrStm *template);


int knd_attr_stm_export_GSL(struct kndAttrStm *self, struct kndTask *task, size_t depth);
int knd_attr_stms_export_GSL(struct kndAttrStm *items, struct kndTask *task, bool is_concise, size_t depth);
int knd_attr_stm_export_JSON(struct kndAttrStm *stm, struct kndTask *task, size_t depth);
int knd_attr_stms_export_JSON(struct kndAttrStm *stms, struct kndTask *task, bool is_concise, size_t depth);



int knd_attr_stm_export_GSP(struct kndAttrStm *self, struct kndTask *task, struct kndOutput *out, size_t depth);
int knd_attr_stms_export_GSP(struct kndAttrStm *items, struct kndOutput *out,struct kndTask *task,
                             size_t depth, bool is_concise);

int knd_present_computed_inner_attrs(struct kndAttrStm *attr_stm, struct kndOutput *out);

int knd_compute_num_value(struct kndAttr *attr, struct kndAttrStm *attr_stm, long *result);


void knd_attr_stm_str(struct kndAttrStm *item, size_t depth);

extern gsl_err_t knd_select_attr_stm(struct kndClass *class, const char *name, size_t name_size,
                                     const char *rec, size_t *total_size,
                                     struct kndTask *task);

int knd_resolve_attr_stms(struct kndClass *self, struct kndClassBasePred *parent_item,
                          struct kndTask *task);

// knd_attr_stm.index.c
int knd_index_attr_stm(struct kndClassEntry *topic, struct kndAttr *attr,
                       struct kndAttrStm *stm, struct kndTask *task);
int knd_index_inst_attr_stm(struct kndClassInstEntry *topic_inst,
                            struct kndAttr *attr, struct kndAttrStm *stm, struct kndTask *task);

int knd_index_attr_stm_list(struct kndClassEntry *topic, struct kndAttr *attr,
                            struct kndAttrStm *stm, struct kndTask *task);
int knd_index_inst_attr_stm_list(struct kndClassInstEntry *topic_inst, struct kndAttr *attr,
                                 struct kndAttrStm *stm, struct kndTask *task);

int knd_attr_stm_inner_idx(struct kndClassEntry *topic, struct kndAttr *attr,
                           struct kndAttrStm *stm, struct kndTask *task);
