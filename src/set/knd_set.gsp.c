static int traverse_sync(struct kndSetElemIdx *parent_idx,
                         map_cb_func cb, void *obj, struct kndSetDir **result_dir)
{
    struct kndTask *task = obj;
    struct kndSetElemIdx *idx;
    struct kndSetDir *dir, *subdir;
    struct kndSetDirEntry *entry;
    size_t num_empty_entries = 0;
    bool use_positional_indexing = true;
    void *elem;
    int err;

    dir = calloc(1, sizeof(struct kndSetDir));
    if (!dir) {
        err = knd_NOMEM;
        KND_TASK_ERR("failed to alloc kndSetDir");
    }

    /* sync subdirs */
    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        idx = parent_idx->idxs[i];
        if (!idx) continue;

        entry = &dir->entries[i];

        subdir = NULL;
        err = traverse_sync(idx, cb, obj, &subdir);
        if (err) return err;

        entry->subdir = subdir;

        dir->total_size += entry->subdir->total_size;
        dir->total_elems += subdir->total_elems;
        dir->num_subdirs++;
    }

    /* sync elem bodies */
    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        elem = parent_idx->elems[i];
        if (!elem) {
            if (!parent_idx->idxs[i])
                num_empty_entries++;
            continue;
        }

        err = cb(elem, obj);
        if (err) return err;

        entry = &dir->entries[i];
        entry->payload_size = task->out->buf_size;
        dir->total_size += entry->payload_size;

        dir->num_elems++;

        // TODO: sync entry payload to file
    }

    /* build footer */
    if (num_empty_entries > KND_RADIX_BASE / 2)
        use_positional_indexing = false;

    err = build_dir_footer(dir, use_positional_indexing, task);
    KND_TASK_ERR("failed to build set dir footer");

    *result_dir = dir;

    return knd_OK;
}

int knd_set_sync(struct kndSet *self, map_cb_func cb, size_t *total_size, struct kndTask *task)
{
    struct kndSetDir *root_dir;
    int err;

    err = traverse_sync(self->idx, cb, task, &root_dir);
    if (err) return err;

    knd_log("== total exported set size:%zu", root_dir->total_size);
    *total_size = root_dir->total_size;
    return knd_OK;
}
