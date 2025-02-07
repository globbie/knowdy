
int knd_commit_export_JSON(struct kndCommit *self,
                           struct kndTask *task)
{
    struct kndClassEntry *entry = self->entry;
    struct kndClassEntry *orig_entry = entry->base;
    struct kndOutput *out = task->out;
    struct kndState *state = self->states;
    size_t num_children;
    int err;

    if (DEBUG_JSON_LEVEL_2) {
        knd_log("\n.. JSON export: \"%.*s\" (repo:%.*s)  depth:%zu max depth:%zu",
                entry->name_size, entry->name,
                entry->repo->name_size, entry->repo->name,
                task->depth, task->ctx->max_depth);
    }

    err = out->write(out, "{", 1);                                                RET_ERR();
    err = out->write(out, "\"_name\":\"", strlen("\"_name\":\""));                RET_ERR();
    err = out->write_escaped(out, entry->name, entry->name_size);                 RET_ERR();
    err = out->writec(out, '"');                                                  RET_ERR();

    err = out->write(out, ",\"_id\":", strlen(",\"_id\":"));                      RET_ERR();
    err = out->writef(out, "%zu", entry->numid);                                  RET_ERR();

    return knd_OK;
}
