static int compare_set_by_size_ascend(const void *a, const void *b)
{
    struct kndSharedSet **obj1, **obj2;

    obj1 = (struct kndSharedSet**)a;
    obj2 = (struct kndSharedSet**)b;

    if ((*obj1)->num_elems == (*obj2)->num_elems) return 0;
    if ((*obj1)->num_elems > (*obj2)->num_elems) return 1;

    return -1;
}

int knd_shared_set_intersect(struct kndSharedSet *self, struct kndSharedSet **sets, size_t num_sets)
{
    struct kndSharedSetElemIdx *base_idx;
    struct kndSharedSetElemIdx *idxs[KND_MAX_CLAUSES];
    size_t num_idxs = num_sets - 1;
    int err;

    assert(num_sets > 1);

    /* sort sets by size */
    qsort(sets, num_sets, sizeof(struct kndSharedSet*), compare_set_by_size_ascend);

    /* the smallest set is taken as a base */
    base_idx = sets[0]->idx;
    sets++;

    for (size_t i = 0; i < num_idxs; i++)
        idxs[i] = sets[i]->idx;

    err = traverse(self, base_idx, idxs, num_idxs, self->idx);
    if (err) return err;

    return knd_OK;
}

int knd_shared_set_get(struct kndSharedSet *self, const char *key, size_t key_size, void **elem)
{
    int err;
    if (!self->idx) return knd_FAIL;

    err = get_elem(self, self->idx, elem, key, key_size);
    if (err) return err;

    return knd_OK;
}

static int traverse_idx(struct kndSharedSetElemIdx *parent_idx, map_cb_func cb, void *obj, size_t *count)
{
    char buf[KND_ID_SIZE];
    size_t buf_size = 0;
    struct kndSharedSetElemIdx *idx;
    void *elem;
    int err;

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        elem = parent_idx->elems[i];
        if (!elem) continue;
        buf_size = 0;
        buf[buf_size] = obj_id_seq[i];
        buf_size = 1;

        err = cb(obj, buf, buf_size, *count, elem);
        if (err) return err;
        (*count)++;
    }

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        idx = parent_idx->idxs[i];
        if (!idx) continue;

        err = traverse_idx(idx, cb, obj, count);
        if (err) return err;
    }

    return knd_OK;
}

int knd_shared_set_map(struct kndSharedSet *self, map_cb_func cb, void *obj)
{
    size_t count = 0;
    int err;

    if (!self->idx) {
        if (DEBUG_SHARED_SET_LEVEL_2)
            knd_log("NB: -- set has no root idx");
        return knd_OK;
    }
    err = traverse_idx(self->idx, cb, obj, &count);
    if (err) return err;
    return knd_OK;
}

static int get_elem(struct kndSharedSet *self, struct kndSharedSetElemIdx *parent_idx,
                    void **result, const char *id, size_t id_size)
{
    struct kndSharedSetElemIdx *idx;
    void *elem;
    int idx_pos;
    int err;

    if (DEBUG_SHARED_SET_LEVEL_2)
        knd_log(".. get elem by ID, remainder \"%.*s\"", id_size, id);

    assert(parent_idx != NULL);

    idx_pos = obj_id_base[(unsigned char)*id];
    if (idx_pos == -1) {
        knd_log("-- invalid elem id");
        return knd_FORMAT;
    }
    if (id_size > 1) {
        idx = atomic_load_explicit(&parent_idx->idxs[idx_pos], memory_order_relaxed);
        if (!idx) return knd_NO_MATCH;

        err = get_elem(self, idx, result, id + 1, id_size - 1);
        if (err) return err;
        return knd_OK;
    }
    elem = atomic_load_explicit(&parent_idx->elems[idx_pos], memory_order_relaxed);
    if (!elem) return knd_NO_MATCH;

    *result = elem;
    return knd_OK;
}
