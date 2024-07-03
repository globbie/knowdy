static gsl_err_t read_nested_attr_var(void *obj, const char *id, size_t id_size,
                                      const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrVar *self = ctx->attr_var;
    struct kndTask    *task = ctx->task;
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndAttrRef *ref;
    struct kndAttr *attr;
    gsl_err_t parser_err;
    int err;

    assert(ctx->class != NULL);

    err = knd_set_get(ctx->class->attr_idx, id, id_size, (void**)&ref);
    if (err) {
        KND_TASK_LOG("class \"%.*s\" failed to decode attr id \"%.*s\"",
                     ctx->class->name_size, ctx->class->name, id_size, id);
        return *total_size = 0, make_gsl_err_external(err);
    }
    assert(ref->attr != NULL);
    attr = ref->attr;

    if (DEBUG_ATTR_VAR_READ_LEVEL_2)
        knd_log(">> attr decoded: %.*s (type: %s)", attr->name_size, attr->name,
                knd_attr_names[attr->type]);

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_var->parent = self;

    //    attr_var->attr = attr;
    //attr_var->name = attr->name;
    //attr_var->name_size = attr->name_size;

    struct LocalContext attr_var_ctx = {
        .attr_var = attr_var,
        .task = task
    };

    switch (attr->type) {
    case KND_ATTR_INNER:
        assert(attr->ref_class_entry != NULL);
        err = knd_class_acquire(attr->ref_class_entry, &attr_var_ctx.class, task);
        if (err) {
            KND_TASK_LOG("failed to acquire class \"%.*s\"",
                         attr->ref_class_entry->name_size, attr->ref_class_entry->name);
            return *total_size = 0, make_gsl_err_external(err);
        }
        if (DEBUG_ATTR_VAR_READ_LEVEL_2)
            knd_log(">> attr var inner class: \"%.*s\"",
                    attr->ref_class_entry->name_size, attr->ref_class_entry->name);
        break;
    default:
        break;
    }

    if (DEBUG_ATTR_VAR_READ_LEVEL_2) {
        knd_log(".. read nested attr var: \"%.*s\" (parent item:%.*s)",
                attr_var->name_size, attr_var->name, self->name_size, self->name);
    }
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_var_val_id,
          .obj = &attr_var_ctx
        },
        { .name = "_t",
          .name_size = strlen("_t"),
          .parse = parse_text,
          .obj = &attr_var_ctx
        },
        { .name = "_p",
          .name_size = strlen("_p"),
          .parse = parse_proc_ref,
          .obj = &attr_var_ctx
        },
        { .validate = read_nested_attr_var,
          .obj = &attr_var_ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = read_nested_attr_var_list,
          .obj = &attr_var_ctx
        },
        { .is_default = true,
          .run = confirm_attr_var,
          .obj = attr_var
        }
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        KND_TASK_LOG("attr var reading failed: %d", parser_err.code);
        return parser_err;
    }

    if (DEBUG_ATTR_VAR_READ_LEVEL_2)
        knd_log("++ attr var: \"%.*s\" val:%.*s (parent item: %.*s)",
                attr_var->name_size, attr_var->name, attr_var->val_size, attr_var->val,
                self->name_size, self->name);

    attr_var->next = self->children;
    self->children = attr_var;
    self->num_children++;
    return make_gsl_err(gsl_OK);
}





static gsl_err_t set_attr_var_name(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrVar *self = ctx->attr_var;
    struct kndTask    *task = ctx->task;
    struct kndClass *c = ctx->class;
    struct kndCharSeq *seq;
    struct kndAttr *attr = self->parent ? self->parent->attr : self->attr;
    int err;

    assert(attr != NULL);

    if (DEBUG_ATTR_VAR_READ_LEVEL_2)
        knd_log(".. set \"%.*s\" (%d) attr var name \"%.*s\"",
                attr->name_size, attr->name, attr->type, name_size, name);

    if (!name_size) return make_gsl_err(gsl_FORMAT);

    if (c && c->implied_attr) {
        if (DEBUG_ATTR_VAR_READ_LEVEL_2)
            knd_log(">> implied attr: %.*s (type:%d)",
                    c->implied_attr->name_size, c->implied_attr->name, c->implied_attr->type);

        self->implied_attr = c->implied_attr;
        attr = c->implied_attr;
    }

    switch (attr->type) {
    case KND_ATTR_REL:
        // fall through
    case KND_ATTR_REF:
        err = knd_get_class_entry_by_id(task->repo, name, name_size, &self->class_entry, task);
        if (err) {
            KND_TASK_LOG("no such class entry: %.*s", name_size, name);
            if (err) return make_gsl_err_external(err);
        }
        if (DEBUG_ATTR_VAR_READ_LEVEL_3)
            knd_log("== REF: %.*s", self->class_entry->name_size, self->class_entry->name);
        break;
    case KND_ATTR_STR:
        err = knd_charseq_decode(name, name_size, &seq, task);
        if (err) {
            KND_TASK_LOG("failed to decode a charseq");
            if (err) return make_gsl_err_external(err);
        }
        self->name = seq->val;
        self->name_size = seq->val_size;
        break;
    default:
        self->name = name;
        self->name_size = name_size;
        break;
    }
    return make_gsl_err(gsl_OK);
}



static gsl_err_t set_attr_var_val_id(void *obj, const char *val_id, size_t val_id_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrVar *self = ctx->attr_var;
    struct kndTask    *task = ctx->task;
    struct kndRepo *repo = task->repo;
    struct kndClassEntry *entry;
    struct kndCharSeq *seq;
    struct kndClass *c = ctx->class;
    int err;

    if (DEBUG_ATTR_VAR_READ_LEVEL_2) {
        knd_log(".. set \"%.*s\" (%d) attr var value: \"%.*s\" => \"%.*s\"",
                self->attr->name_size, self->attr->name, self->attr->type,
                self->name_size, self->name, val_size, val);
    }
    if (!val_size) return make_gsl_err(gsl_FORMAT);
    if (val_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);
    
    memcpy(self->val_id, val_id, val_id_size);
    self->val_id_size = val_id_size;

    switch (self->attr->type) {
    case KND_ATTR_NUM:
    case KND_ATTR_FLOAT:
    case KND_ATTR_STR:
        err = knd_charseq_decode(val, val_size, &seq, task);
        if (err) {
            KND_TASK_LOG("failed to decode a charseq");
            if (err) return make_gsl_err_external(err);
        }
        if (DEBUG_ATTR_VAR_READ_LEVEL_2)
            knd_log(">> \"%.*s\" => decoded str val:%.*s",
                    self->name_size, self->name, seq->val_size, seq->val);
        self->val = seq->val;
        self->val_size = seq->val_size;
        break;
    case KND_ATTR_INNER:
        if (!c || !c->implied_attr) break;
        err = set_implied_attr_var(c, val, val_size, self, task);
        if (err) {
            KND_TASK_LOG("failed to set implied attr \"%.*s\" to %.*s",
                         c->implied_attr->name_size, c->implied_attr->name, val_size, val);
            return make_gsl_err(gsl_FAIL);
        }
        break;
    case KND_ATTR_REL:
        // fall through
    case KND_ATTR_REF:
        err = knd_shared_set_get(task->idxs->class_idx, val, val_size, (void**)&entry);
        if (err) {
            KND_TASK_LOG("class \"%.*s\" not found in repo %.*s",
                         val_size, val, repo->name_size, repo->name);
            return make_gsl_err(gsl_FAIL);
        }
        self->class_entry = entry;
        
        if (DEBUG_ATTR_VAR_READ_LEVEL_3)
            knd_log(">> set class ref: %.*s (id:%.*s)", entry->name_size, entry->name, val_size, val);
        break;
    default:
        break;
    }
    return make_gsl_err(gsl_OK);
}



static int build_attr_var(struct kndClassVar *self, const char *id, size_t id_size,
                          struct kndAttrVar **result, struct kndTask *task)
{
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndClassEntry *entry = self->entry;
    struct kndClass *c = self->parent;
    struct kndAttr *attr;
    struct kndAttrVar *var;
    struct kndAttrRef *ref;
    int err;

    err = knd_set_get(c->attr_idx, id, id_size, (void**)&ref);
    KND_TASK_ERR("no attr \"%.*s\" in class \"%.*s\"", id_size, id, c->name_size, c->name);
    attr = ref->attr;

    if (DEBUG_ATTR_VAR_READ_LEVEL_2)
        knd_log(">> class \"%.*s\" to read \"%.*s\" var (origin: %.*s) (id:%.*s, type: %s)",
                self->parent->name_size, self->parent->name,
                attr->name_size, attr->name, entry->name_size, entry->name,
                id_size, id, knd_attr_names[attr->type]);

    err = knd_attr_var_new(mempool, &var);
    KND_TASK_ERR("failed to alloc an attr var");
    var->class_var = self;
    var->name = attr->name;
    var->name_size = attr->name_size;
    var->attr = attr;
    // set inherited attr var
    ref->attr_var = var;
    append_attr_var(self, var);

    switch (attr->type) {
    case KND_ATTR_INNER:
        assert(attr->ref_class_entry != NULL);
        err = knd_class_acquire(attr->ref_class_entry, &attr->ref_class, task);
        KND_TASK_ERR("failed to acquire class \"%.*s\"",
                     attr->ref_class_entry->name_size, attr->ref_class_entry->name);
        if (DEBUG_ATTR_VAR_READ_LEVEL_2)
            knd_log(">> inner class: \"%.*s\"",
                    attr->ref_class_entry->name_size, attr->ref_class_entry->name);
        break;
    default:
        break;
    }
    *result = var;
    return knd_OK;
}
