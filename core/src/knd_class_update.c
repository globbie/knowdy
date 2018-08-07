
static int build_class_updates(struct kndClass *self,
                               struct kndUpdate *update)
{
    char buf[KND_SHORT_NAME_SIZE];
    size_t buf_size;
    struct kndTask *task = self->entry->repo->task;
    struct glbOutput *out = task->update;
    struct kndClass *c;
    struct kndObject *obj;
    struct kndClassUpdate *class_update;
    int err;

    for (size_t i = 0; i < update->num_classes; i++) {
        class_update = update->classes[i];
        c = class_update->conc;

        err = out->write(out, "{class ", strlen("{class "));   RET_ERR();
        err = out->write(out, c->name, c->name_size);

        err = out->write(out, "(id ", strlen("(id "));         RET_ERR();
        buf_size = sprintf(buf, "%zu", c->entry->numid);
        err = out->write(out, buf, buf_size);                  RET_ERR();

        /* export obj updates */
        for (size_t j = 0; j < class_update->num_insts; j++) {
            obj = class_update->insts[j];

            if (DEBUG_CONC_LEVEL_2)
                knd_log(".. export update of OBJ %.*s", obj->name_size, obj->name);

            err = out->write(out, "{obj ", strlen("{obj "));   RET_ERR();
            err = out->write(out, obj->name, obj->name_size);  RET_ERR();

            if (obj->states->phase == KND_REMOVED) {
                err = out->write(out, "{!rm}", strlen("{!rm}"));
                if (err) return err;
            }
            err = out->write(out, "}", 1);                     RET_ERR();
        }
        /* close class out */
        err = out->write(out, ")}", 2);                        RET_ERR();
    }
    return knd_OK;
}

static int export_updates(struct kndClass *self,
                          struct kndUpdate *update)
{
    char buf[KND_SHORT_NAME_SIZE];
    size_t buf_size;
    struct tm tm_info;
    struct kndTask *task = self->entry->repo->task;
    struct glbOutput *out = task->update;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. export updates in \"%.*s\"..",
                self->entry->name_size, self->entry->name);

    out->reset(out);
    err = out->write(out, "{task{update", strlen("{task{update"));               RET_ERR();

    localtime_r(&update->timestamp, &tm_info);
    buf_size = strftime(buf, KND_NAME_SIZE,
                        "{_ts %Y-%m-%d %H:%M:%S}", &tm_info);
    err = out->write(out, buf, buf_size);                                        RET_ERR();

    err = out->write(out, "{user", strlen("{user"));                             RET_ERR();

    /* TODO: spec body */
    /* err = out->write(out,
                     task->update_spec,
                     task->update_spec_size);                  RET_ERR();
    */

    /* state information */
    err = out->write(out, "{state ", strlen("{state "));                          RET_ERR();
    buf_size = sprintf(buf, "%zu", update->numid);
    err = out->write(out, buf, buf_size);                                         RET_ERR();

    /* TODO
       if (update->num_classes) {
        err = build_class_updates(self, update);                                  RET_ERR();
    }

    if (self->rel && self->rel->inbox_size) {
        self->rel->out = out;
        err = self->rel->export_updates(self->rel);                               RET_ERR();
    }
    */

    err = out->write(out, "}}}}}", strlen("}}}}}"));                              RET_ERR();
    return knd_OK;
}

static gsl_err_t set_liquid_class_id(void *obj, const char *val, size_t val_size)
{
    struct kndClass *self = (struct kndClass*)obj;
    struct kndClass *c;
    long numval = 0;
    int err;

    if (!val_size) return make_gsl_err(gsl_FORMAT);

    if (!self->curr_class) return make_gsl_err(gsl_FAIL);
    c = self->curr_class;

    err = knd_parse_num((const char*)val, &numval);
    if (err) return make_gsl_err_external(err);

    c->entry->numid = numval;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. set curr liquid class id: %zu  update id: %zu",
                c->entry->numid, c->entry->numid);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_get_liquid_class(void *obj, const char *name, size_t name_size)
{
    struct kndClass *self = obj;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    err = get_class(self, name, name_size, &self->curr_class);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_liquid_class_id(void *obj,
                                       const char *rec,
                                       size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndUpdate *update = self->curr_update;
    struct kndClass *c;
    struct kndClassUpdate *class_update;
    struct kndClassUpdateRef *class_update_ref;
    int err;
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_liquid_class_id,
          .obj = self
        }
    };

    if (!self->curr_class) return *total_size = 0, make_gsl_err(gsl_FAIL);

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    c = self->curr_class;

    /* register class update */
    err = self->entry->repo->mempool->new_class_update(self->entry->repo->mempool, &class_update);
    if (err) return make_gsl_err_external(err);
    class_update->conc = c;

    update->classes[update->num_classes] = class_update;
    update->num_classes++;

    /*    err = self->entry->repo->mempool->new_class_update_ref(self->entry->repo->mempool, &class_update_ref);
    if (err) return make_gsl_err_external(err);
    class_update_ref->update = update;

    class_update_ref->next = c->states;
    c->states =  class_update_ref;
    c->num_updates++;
    */
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_liquid_class_update(void *obj,
                                           const char *rec, size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndClassUpdate **class_updates;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log("..  liquid class update REC: \"%.*s\"..", 32, rec); }

    if (!self->curr_update) return *total_size = 0, make_gsl_err(gsl_FAIL);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_get_liquid_class,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "id",
          .name_size = strlen("id"),
          .parse = parse_liquid_class_id,
          .obj = self
        }
    };

    /* create index of class updates */
    class_updates = realloc(self->curr_update->classes,
                            (self->inbox_size * sizeof(struct kndClassUpdate*)));
    if (!class_updates) return *total_size = 0, make_gsl_err_external(knd_NOMEM);
    self->curr_update->classes = class_updates;

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_liquid_rel_update(void *obj,
                                         const char *rec, size_t *total_size)
{
    struct kndClass *self = obj;

    if (!self->curr_update) return *total_size = 0, make_gsl_err_external(knd_FAIL);

    self->rel->curr_update = self->curr_update;
    return self->rel->parse_liquid_updates(self->rel, rec, total_size);
}

static gsl_err_t new_liquid_update(void *obj, const char *val, size_t val_size)
{
    struct kndClass *self = obj;
    struct kndUpdate *update;
    long numval = 0;
    int err;

    assert(val[val_size] == '\0');

    err = knd_parse_num((const char*)val, &numval);
    if (err) return make_gsl_err_external(err);

    err = self->entry->repo->mempool->new_update(self->entry->repo->mempool, &update);
    if (err) return make_gsl_err_external(err);

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== new class update: %zu", update->id);

    self->curr_update = update;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t apply_liquid_updates(struct kndClass *self,
                                      const char *rec,
                                      size_t *total_size)
{
    struct kndClass *c;
    struct kndClassEntry *entry;
    struct kndRel *rel;
    struct kndStateControl *state_ctrl = self->entry->repo->state_ctrl;
    struct kndMemPool *mempool = self->entry->repo->mempool;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = new_liquid_update,
          .obj = self
        },
        { .name = "class",
          .name_size = strlen("class"),
          .parse = parse_liquid_class_update,
          .obj = self
        },
        { .name = "rel",
          .name_size = strlen("rel"),
          .parse = parse_liquid_rel_update,
          .obj = self
        }
    };
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CONC_LEVEL_1)
        knd_log("..apply liquid updates..");

    if (self->inbox_size) {
        for (c = self->inbox; c; c = c->next) {

            err = c->resolve(c, NULL);
            if (err) return *total_size = 0, make_gsl_err_external(err);

            err = mempool->new_class_entry(mempool, &entry);
            if (err) return *total_size = 0, make_gsl_err_external(err);
            entry->class = c;

            err = self->class_name_idx->set(self->class_name_idx,
                                            c->entry->name, c->name_size,
                                            (void*)entry);
            if (err) return *total_size = 0, make_gsl_err_external(err);
        }
    }

    /*if (self->rel->inbox_size) {
        for (rel = self->rel->inbox; rel; rel = rel->next) {
            err = rel->reso<lve(rel);
            if (err) return *total_size = 0, make_gsl_err_external(err);
        }
        }*/

    parser_err = gsl_parse_task(rec, total_size, specs,
                                sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (!self->curr_update) return make_gsl_err_external(knd_FAIL);

    err = state_ctrl->confirm(state_ctrl, self->curr_update);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

