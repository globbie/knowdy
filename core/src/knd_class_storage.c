

static int restore(struct kndClass *self)
{
    char state_buf[KND_STATE_SIZE];
    char last_state_buf[KND_STATE_SIZE];
    struct glbOutput *out = self->entry->repo->out;

    const char *inbox_dir = "/schema/inbox";
    size_t inbox_dir_size = strlen(inbox_dir);
    int err;

    memset(state_buf, '0', KND_STATE_SIZE);
    if (DEBUG_CONC_LEVEL_TMP)
        knd_log(".. conc \"%s\" restoring DB state in: %s",
                self->entry->name, self->entry->repo->path, KND_STATE_SIZE);

    out->reset(out);
    err = out->write(out, self->entry->repo->path, self->entry->repo->path_size);
    if (err) return err;
    err = out->write(out, "/schema/class_state.id", strlen("/schema/class_state.id"));
    if (err) return err;

    err = out->write_file_content(out,
                         (const char*)out->buf);
    if (err) {
        knd_log("-- no class_state.id file found, assuming initial state ..");
        return knd_OK;
    }

    /* check if state content is a valid state id */
    err = knd_state_is_valid(out->buf, out->buf_size);
    if (err) {
        knd_log("-- state id is not valid: \"%.*s\"",
                out->buf_size, out->buf);
        return err;
    }

    memcpy(last_state_buf, out->buf, KND_STATE_SIZE);
    if (DEBUG_CONC_LEVEL_TMP)
        knd_log(".. last DB state: \"%.*s\"",
                out->buf_size, out->buf);

    out->rtrim(out, strlen("/schema/class_state.id"));
    err = out->write(out, inbox_dir, inbox_dir_size);
    if (err) return err;

    while (1) {
        knd_next_state(state_buf);

        //err = out->write_state_path(out, state_buf);
        //if (err) return err;

        err = out->write(out, "/spec.gsl", strlen("/spec.gsl"));
        if (err) return err;


        err = out->write_file_content(out, (const char*)out->buf);
        if (err) {
            knd_log("-- couldn't read GSL spec \"%s\" :(", out->buf);
            return err;
        }

        if (DEBUG_CONC_LEVEL_TMP)
            knd_log(".. state update spec file: \"%.*s\" SPEC: %.*s\n\n",
                    out->buf_size, out->buf, out->buf_size, out->buf);

        /* last update */
        if (!memcmp(state_buf, last_state_buf, KND_STATE_SIZE)) break;

        /* cut the tail */
        out->rtrim(out, strlen("/spec.gsl") + (KND_STATE_SIZE * 2));
    }
    return knd_OK;
}


static int freeze_objs(struct kndClass *self,
                       size_t *total_frozen_size,
                       char *output,
                       size_t *total_size)
{
    char buf[KND_SHORT_NAME_SIZE];
    size_t buf_size;
    char *curr_dir = output;
    size_t init_dir_size = 0;
    size_t curr_dir_size = 0;
    struct kndObject *obj;
    struct kndObjEntry *entry;
    struct glbOutput *out;
    struct glbOutput *dir_out;
    const char *key;
    void *val;
    size_t chunk_size;
    size_t num_size;
    size_t obj_block_size = 0;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. freezing objs of class \"%.*s\", total:%zu  valid:%zu",
                self->entry->name_size, self->entry->name,
                self->entry->obj_name_idx->size, self->entry->num_objs);

    out = self->entry->repo->out;
    out->reset(out);
    dir_out = self->entry->repo->dir_out;

    dir_out->reset(dir_out);

    err = dir_out->write(dir_out, "[o", 2);                           RET_ERR();
    init_dir_size = dir_out->buf_size;

    key = NULL;
    self->entry->obj_name_idx->rewind(self->entry->obj_name_idx);
    do {
        self->entry->obj_name_idx->next_item(self->entry->obj_name_idx, &key, &val);
        if (!key) break;
        entry = (struct kndObjEntry*)val;
        obj = entry->obj;

        if (obj->states->phase != KND_CREATED) {
            knd_log("NB: skip freezing \"%.*s\"   phase: %d",
                    obj->name_size, obj->name, obj->states->phase);
            continue;
        }
        obj->depth = self->depth + 1;
        if (DEBUG_CONC_LEVEL_2) {
            obj->str(obj);
        }

        err = obj->export(obj);
        if (err) {
            knd_log("-- couldn't export GSP of the \"%.*s\" obj :(",
                    obj->name_size, obj->name);
            return err;
        }

        err = dir_out->writec(dir_out, ' ');
        if (err) return err;

        buf_size = 0;
        knd_num_to_str(obj->frozen_size, buf, &buf_size, KND_RADIX_BASE);
        err = dir_out->write(dir_out, buf, buf_size);
        if (err) return err;

        if (DEBUG_CONC_LEVEL_2)
            knd_log("OBJ body size: %zu [%.*s]",
                    obj->frozen_size, buf_size, buf);

        /* OBJ persistent write */
        if (out->buf_size > out->threshold) {
            err = knd_append_file(self->entry->repo->frozen_output_file_name,
                                  out->buf, out->buf_size);
            if (err) return err;

            *total_frozen_size += out->buf_size;
            obj_block_size += out->buf_size;
            out->reset(out);
        }
    } while (key);

    /* no objs written? */
    if (dir_out->buf_size == init_dir_size) {
        *total_frozen_size = 0;
        *total_size = 0;
        return knd_OK;
    }

    /* final chunk to write */
    if (self->entry->repo->out->buf_size) {
        err = knd_append_file(self->entry->repo->frozen_output_file_name,
                              out->buf, out->buf_size);                           RET_ERR();
        *total_frozen_size += out->buf_size;
        obj_block_size += out->buf_size;
        out->reset(out);
    }

    /* close directory */
    err = dir_out->write(dir_out, "]", 1);                            RET_ERR();

    /* obj directory size */
    buf_size = sprintf(buf, "%lu", (unsigned long)dir_out->buf_size);

    err = dir_out->write(dir_out, "{L ", strlen("{L "));
    if (err) return err;
    err = dir_out->write(dir_out, buf, buf_size);
    if (err) return err;
    err = dir_out->write(dir_out, "}", 1);
    if (err) return err;

    /* persistent write of directory */
    err = knd_append_file(self->entry->repo->frozen_output_file_name,
                          dir_out->buf, dir_out->buf_size);
    if (err) return err;

    *total_frozen_size += dir_out->buf_size;
    obj_block_size += dir_out->buf_size;

    /* update class dir entry */
    chunk_size = strlen("{O");
    memcpy(curr_dir, "{O", chunk_size);
    curr_dir += chunk_size;
    curr_dir_size += chunk_size;

    num_size = sprintf(curr_dir, "{size %lu}",
                       (unsigned long)obj_block_size);
    curr_dir +=      num_size;
    curr_dir_size += num_size;

    num_size = sprintf(curr_dir, "{tot %lu}",
                       (unsigned long)self->entry->num_objs);
    curr_dir +=      num_size;
    curr_dir_size += num_size;

    memcpy(curr_dir, "}", 1);
    curr_dir++;
    curr_dir_size++;


    *total_size = curr_dir_size;

    return knd_OK;
}

static int freeze_subclasses(struct kndClass *self,
                             size_t *total_frozen_size,
                             char *output,
                             size_t *total_size)
{
    char buf[KND_SHORT_NAME_SIZE] = {0};
    size_t buf_size;
    struct kndClass *c;
    char *curr_dir = output;
    size_t curr_dir_size = 0;
    size_t chunk_size;
    int err;

    chunk_size = strlen("[c");
    memcpy(curr_dir, "[c", chunk_size);
    curr_dir += chunk_size;
    curr_dir_size += chunk_size;

    for (size_t i = 0; i < self->entry->num_children; i++) {
        c = self->entry->children[i]->class;
        err = c->freeze(c);
        if (err) return err;

        if (!c->entry->frozen_size) {
            knd_log("-- empty GSP in %.*s?", c->name_size, c->name);
            continue;
        }

        /* terminal class */
//        if (c->is_terminal) {
//            self->num_terminals++;
//        } else {
//            self->num_terminals += c->num_terminals;
//        }

        memcpy(curr_dir, " ", 1);
        curr_dir++;
        curr_dir_size++;

        buf_size = 0;
        knd_num_to_str(c->entry->frozen_size, buf, &buf_size, KND_RADIX_BASE);
        memcpy(curr_dir, buf, buf_size);
        curr_dir      += buf_size;
        curr_dir_size += buf_size;

        *total_frozen_size += c->entry->frozen_size;
    }

    /* close the list of children */
    memcpy(curr_dir, "]", 1);
    curr_dir++;
    curr_dir_size++;

    *total_size = curr_dir_size;
    return knd_OK;
}

static int freeze_rels(struct kndRel *self,
                       size_t *total_frozen_size,
                       char *output,
                       size_t *total_size)
{
    struct kndRel *rel;
    struct kndRelEntry *entry;
    const char *key;
    void *val;
    char *curr_dir = output;
    size_t curr_dir_size = 0;
    size_t chunk_size;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. freezing rels..");

    key = NULL;
    self->rel_name_idx->rewind(self->rel_name_idx);
    do {
        self->rel_name_idx->next_item(self->rel_name_idx, &key, &val);
        if (!key) break;

        entry = (struct kndRelEntry*)val;
        rel = entry->rel;


        err = rel->freeze(rel, total_frozen_size, curr_dir, &chunk_size);
        if (err) {
            knd_log("-- couldn't freeze the \"%s\" rel :(", rel->name);
            return err;
        }
        curr_dir +=      chunk_size;
        curr_dir_size += chunk_size;
    } while (key);

    *total_size = curr_dir_size;

    return knd_OK;
}

static int freeze(struct kndClass *self)
{
    // TODO
    char dir_buf[KND_MAX_CONC_CHILDREN * KND_DIR_ENTRY_SIZE];// = self->dir_buf;
    char *curr_dir = dir_buf;

    size_t curr_dir_size = 0;
    size_t total_frozen_size = 0;
    size_t num_size;
    size_t chunk_size;
    int err;

    self->entry->repo->out->reset(self->entry->repo->out);

    /* class self presentation */
    err = export_GSP(self);
    if (err) {
        knd_log("-- GSP export failed :(");
        return err;
    }

    /* persistent write */
    err = knd_append_file(self->entry->repo->frozen_output_file_name,
                          self->entry->repo->out->buf,
                          self->entry->repo->out->buf_size);                      RET_ERR();

    total_frozen_size = self->entry->repo->out->buf_size;

    /* TODO: no dir entry necessary */
    /*if (!self->entry->num_children) {
        if (!self->entry) {
            self->frozen_size = total_frozen_size;
            return knd_OK;
        }
        if (!self->entry->obj_name_idx) {
            self->frozen_size = total_frozen_size;
            return knd_OK;
        }
        }*/

    /* class dir entry */
    chunk_size = strlen("{C ");
    memcpy(curr_dir, "{C ", chunk_size);
    curr_dir += chunk_size;
    curr_dir_size += chunk_size;

    num_size = sprintf(curr_dir, "%zu}", total_frozen_size);
    curr_dir +=      num_size;
    curr_dir_size += num_size;

    if (self->entry->repo->next_class_numid) {
        num_size = sprintf(curr_dir, "{tot %zu}", self->entry->repo->next_class_numid);
        curr_dir +=      num_size;
        curr_dir_size += num_size;
    }

    /* any instances to freeze? */
    if (self->entry && self->entry->num_objs) {
        err = freeze_objs(self, &total_frozen_size, curr_dir, &chunk_size);       RET_ERR();
        curr_dir +=      chunk_size;
        curr_dir_size += chunk_size;
    }

    if (self->entry->num_children) {
        err = freeze_subclasses(self, &total_frozen_size,
                                curr_dir, &chunk_size);                           RET_ERR();
        curr_dir +=      chunk_size;
        curr_dir_size += chunk_size;
    }

    /* rels */
    if (self->rel && self->rel->rel_name_idx->size) {
        chunk_size = strlen("[R");
        memcpy(curr_dir, "[R", chunk_size);
        curr_dir += chunk_size;
        curr_dir_size += chunk_size;

        chunk_size = 0;

        err = freeze_rels(self->rel, &total_frozen_size,
                          curr_dir, &chunk_size);                                  RET_ERR();
        curr_dir +=      chunk_size;
        curr_dir_size += chunk_size;

        memcpy(curr_dir, "]", 1);
        curr_dir++;
        curr_dir_size++;
    }

    /* procs */
    if (self->proc && self->proc->proc_name_idx->size) {
        //self->proc->out = self->entry->repo->out;
        //self->proc->dir_out = dir_out;
        //self->proc->frozen_output_file_name = self->entry->repo->frozen_output_file_name;

        err = self->proc->freeze_procs(self->proc,
                                       &total_frozen_size,
                                       curr_dir, &chunk_size);                    RET_ERR();
        curr_dir +=      chunk_size;
        curr_dir_size += chunk_size;
    }

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== %.*s (%.*s)   DIR: \"%.*s\"   [%lu]",
                self->entry->name_size, self->entry->name, self->entry->id_size, self->entry->id,
                curr_dir_size,
                dir_buf, (unsigned long)curr_dir_size);

    num_size = sprintf(curr_dir, "{L %lu}",
                       (unsigned long)curr_dir_size);
    curr_dir_size += num_size;

    err = knd_append_file(self->entry->repo->frozen_output_file_name,
                          dir_buf, curr_dir_size);
    if (err) return err;

    total_frozen_size += curr_dir_size;

    self->entry->frozen_size = total_frozen_size;

    return knd_OK;
}


static int unfreeze_class(struct kndClass *self,
                          struct kndClassEntry *entry,
                          struct kndClass **result)
{
    char buf[KND_MED_BUF_SIZE];
    size_t buf_size = 0;
    struct kndClass *c;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    size_t chunk_size;
    const char *filename;
    size_t filename_size;
    const char *b;
    struct stat st;
    int fd;
    size_t file_size = 0;
    struct stat file_info;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. unfreezing class: \"%.*s\".. global offset:%zu  block size:%zu",
                entry->name_size, entry->name, entry->global_offset, entry->block_size);

    /* parse DB rec */
    filename = self->entry->repo->frozen_output_file_name;
    filename_size = self->entry->repo->frozen_output_file_name_size;
    if (stat(filename, &st)) {
        knd_log("-- no such file: %.*s", filename_size, filename);
        return knd_NO_MATCH;
    }

    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        knd_log("-- error reading FILE \"%.*s\": %d", filename_size, filename, fd);
        return knd_IO_FAIL;
    }

    fstat(fd, &file_info);
    file_size = file_info.st_size;
    if (file_size <= KND_DIR_ENTRY_SIZE) {
        err = knd_LIMIT;
        goto final;
    }

    if (lseek(fd, entry->global_offset, SEEK_SET) == -1) {
        err = knd_IO_FAIL;
        goto final;
    }

    buf_size = entry->block_size;
    if (buf_size >= KND_MED_BUF_SIZE) {
        knd_log("-- memory limit exceeded :( buf size:%zu", buf_size);
        return knd_LIMIT;
    }
    err = read(fd, buf, buf_size);
    if (err == -1) {
        err = knd_IO_FAIL;
        goto final;
    }
    buf[buf_size] = '\0';

    if (DEBUG_CONC_LEVEL_2)
        knd_log("\n== frozen Conc REC: \"%.*s\"",
                buf_size, buf);
    /* done reading */
    close(fd);

    err = mempool->new_class(mempool, &c);
    if (err) goto final;
    c->name = entry->name;
    c->name_size = entry->name_size;
    c->entry = entry;
    entry->class = c;

    b = buf + 1;
    bool got_separ = false;
    /* ff the name */
    while (*b) {
        switch (*b) {
        case '{':
        case '}':
        case '[':
        case ']':
            got_separ = true;
            break;
        default:
            break;
        }
        if (got_separ) break;
        b++;
    }

    if (!got_separ) {
        knd_log("-- conc name not found in %.*s :(",
                buf_size, buf);
        err = knd_FAIL;
        goto final;
    }

    parser_err = c->read(c, b, &chunk_size);
    if (parser_err.code) {
        knd_log("-- failed to parse a rec for \"%.*s\" :(",
                c->name_size, c->name);
        err = gsl_err_to_knd_err_codes(parser_err);
        goto final;
    }

    /* inherit attrs */
    err = build_attr_name_idx(c);
    if (err) {
        knd_log("-- failed to build attr idx for %.*s :(",
                c->name_size, c->name);
        goto final;
    }


    err = expand_refs(c);
    if (err) {
        knd_log("-- failed to expand refs of %.*s :(",
                c->name_size, c->name);
        goto final;
    }

    if (DEBUG_CONC_LEVEL_2) {
        c->depth = 1;
        c->str(c);
    }
    c->is_resolved = true;

    *result = c;
    return knd_OK;

 final:
    close(fd);
    return err;
}

static int knd_get_dir_size(struct kndClass *self,
                            size_t *dir_size,
                            size_t *chunk_size,
                            unsigned int encode_base)
{
    char buf[KND_DIR_ENTRY_SIZE + 1] = {0};
    size_t buf_size = 0;
    const char *rec = self->entry->repo->out->buf;
    size_t rec_size = self->entry->repo->out->buf_size;
    char *invalid_num_char = NULL;

    bool in_field = false;
    bool got_separ = false;
    bool got_tag = false;
    bool got_size = false;
    long numval;
    const char *c, *s = NULL;
    int i = 0;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. get size of DIR in %.*s", self->entry->name_size, self->entry->name);

    for (i = rec_size - 1; i >= 0; i--) {
        c = rec + i;
        switch (*c) {
        case '\n':
        case '\r':
            break;
        case '}':
            if (in_field) return knd_FAIL;
            in_field = true;
            break;
        case '{':
            if (!in_field) return knd_FAIL;
            if (got_tag) got_size = true;
            break;
        case ' ':
            got_separ = true;
            break;
        case 'L':
            got_tag = true;
            break;
        default:
            if (!in_field) return knd_FAIL;
            if (got_tag) return knd_FAIL;
            if (!isalnum(*c)) return knd_FAIL;

            buf[i] = *c;
            buf_size++;
            s = buf + i;
            break;
        }
        if (got_size) {
            if (DEBUG_CONC_LEVEL_2)
                knd_log("  ++ got size value to parse: %.*s!", buf_size, s);
            break;
        }
    }

    if (!got_size) return knd_FAIL;

    numval = strtol(s, &invalid_num_char, encode_base);
    if (*invalid_num_char) {
        knd_log("-- invalid char: %.*s", 1, invalid_num_char);
        return knd_FAIL;
    }

    /* check for various numeric decoding errors */
    if ((errno == ERANGE && (numval == LONG_MAX || numval == LONG_MIN)) ||
            (errno != 0 && numval == 0)) {
        return knd_FAIL;
    }

    if (numval <= 0) return knd_FAIL;
    if (numval >= KND_DIR_TRAILER_MAX_SIZE) return knd_LIMIT;
    if (DEBUG_CONC_LEVEL_3)
        knd_log("  == DIR size: %lu    CHUNK SIZE: %lu",
                (unsigned long)numval, (unsigned long)rec_size - i);

    *dir_size = numval;
    *chunk_size = rec_size - i;

    return knd_OK;
}

static gsl_err_t run_set_dir_size(void *obj, const char *val, size_t val_size)
{
    char buf[KND_SHORT_NAME_SIZE] = {0};
    struct kndClassEntry *self = obj;
    char *invalid_num_char = NULL;
    long numval;

    if (!val_size) return make_gsl_err(gsl_FORMAT);
    if (val_size >= KND_SHORT_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    /* null terminated string is needed for strtol */
    memcpy(buf, val, val_size);

    numval = strtol(buf, &invalid_num_char, KND_NUM_ENCODE_BASE);
    if (*invalid_num_char) {
        knd_log("-- invalid char: %.*s in \"%.*s\"",
                1, invalid_num_char, val_size, val);
        return make_gsl_err(gsl_FORMAT);
    }

    /* check for various numeric decoding errors */
    if ((errno == ERANGE && (numval == LONG_MAX || numval == LONG_MIN)) ||
            (errno != 0 && numval == 0))
    {
        return make_gsl_err(gsl_LIMIT);
    }

    if (numval <= 0) return make_gsl_err(gsl_LIMIT);
    if (DEBUG_CONC_LEVEL_2)
        knd_log("== DIR size: %lu", (unsigned long)numval);

    self->block_size = numval;

    return make_gsl_err(gsl_OK);
}


static gsl_err_t parse_parent_dir_size(void *obj,
                                       const char *rec,
                                       size_t *total_size)
{
    struct kndClassEntry *self = obj;
    gsl_err_t err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parsing parent dir size: \"%.*s\"", 16, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_dir_size,
          .obj = self
        }
    };

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    self->curr_offset += self->block_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_obj_dir_size(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndClassEntry *self = obj;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parsing obj dir size: \"%.*s\"", 16, rec);

    struct gslTaskSpec specs[] = {
        {  .name = "size",
           .name_size = strlen("size"),
           .parse = gsl_parse_size_t,
           .obj = &self->obj_block_size
        },
        {  .name = "tot",
           .name_size = strlen("tot"),
           .parse = gsl_parse_size_t,
           .obj = &self->num_objs
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static int parse_dir_trailer(struct kndClass *self,
                             struct kndClassEntry *parent_entry,
                             int fd,
                             int encode_base)
{
    char *dir_buf = self->entry->repo->out->buf;
    size_t dir_buf_size = self->entry->repo->out->buf_size;
    struct kndClassEntry *entry;
    struct kndRelEntry *rel_entry;
    struct kndProcEntry *proc_entry;
    size_t parsed_size = 0;
    int err;
    gsl_err_t parser_err;

    struct gslTaskSpec class_dir_spec = {
        .is_list_item = true,
        .accu = parent_entry,
        .alloc = dir_entry_alloc,
        .append = dir_entry_append,
    };

    struct gslTaskSpec rel_dir_spec = {
        .is_list_item = true,
        .accu = parent_entry,
        .alloc = reldir_entry_alloc,
        .append = reldir_entry_append,
    };

    struct gslTaskSpec proc_dir_spec = {
        .is_list_item = true,
        .accu = parent_entry,
        .alloc = procdir_entry_alloc,
        .append = procdir_entry_append
    };

    struct gslTaskSpec specs[] = {
        { .name = "C",
          .name_size = strlen("C"),
          .parse = parse_parent_dir_size,
          .obj = parent_entry
        },
        {  .name = "tot",
           .name_size = strlen("tot"),
           .parse = gsl_parse_size_t,
           .obj = &self->entry->repo->next_class_numid
        },
        { .name = "O",
          .name_size = strlen("O"),
          .parse = parse_obj_dir_size,
          .obj = parent_entry
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "c",
          .name_size = strlen("c"),
          .parse = gsl_parse_array,
          .obj = &class_dir_spec
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "R",
          .name_size = strlen("R"),
          .parse = gsl_parse_array,
          .obj = &rel_dir_spec
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "P",
          .name_size = strlen("P"),
          .parse = gsl_parse_array,
          .obj = &proc_dir_spec
        }
    };

    parent_entry->curr_offset = parent_entry->global_offset;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("  .. parsing \"%.*s\" DIR REC: \"%.*s\"  curr offset: %zu   [dir size:%zu]",
                KND_ID_SIZE, parent_entry->id, dir_buf_size, dir_buf,
                parent_entry->curr_offset, dir_buf_size);

    parser_err = gsl_parse_task(dir_buf, &parsed_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    /* get conc name */
    if (parent_entry->block_size) {
        if (!parent_entry->is_indexed) {
            err = idx_class_name(self, parent_entry, fd);
            if (err) return err;
            parent_entry->is_indexed = true;
        }
    }

    /* try reading the objs */
    if (parent_entry->obj_block_size) {
        err = get_obj_dir_trailer(self, parent_entry, fd, encode_base);
        if (err) {
            knd_log("-- no obj dir trailer loaded :(");
            return err;
        }
    }

    if (DEBUG_CONC_LEVEL_2)
        knd_log("DIR: %.*s   num children: %zu obj block:%zu",
                parent_entry->id_size, parent_entry->id, parent_entry->num_children,
                parent_entry->obj_block_size);

    /* try reading each dir */
    for (size_t i = 0; i < parent_entry->num_children; i++) {
        entry = parent_entry->children[i];
        if (!entry) continue;

        err = idx_class_name(self, entry, fd);
        if (err) return err;

        if (DEBUG_CONC_LEVEL_2)
            knd_log(".. read DIR  ID:%.*s NAME:%.*s"
                    " block size: %zu  num terminals:%zu",
                    entry->id_size, entry->id,
                    entry->name_size, entry->name,
                    entry->block_size, entry->num_terminals);

        err = get_dir_trailer(self, entry, fd, encode_base);
        if (err) {
            if (err != knd_NO_MATCH) {
                knd_log("-- error reading trailer of \"%.*s\" DIR: %d",
                        entry->name_size, entry->name, err);
                return err;
            } else {
                if (DEBUG_CONC_LEVEL_2)
                    knd_log(".. terminal class: %.*s", entry->name_size, entry->name);
                parent_entry->num_terminals++;
            }
        } else {
            parent_entry->num_terminals += entry->num_terminals;

            if (DEBUG_CONC_LEVEL_2)
                knd_log(".. class:%.*s num_terminals:%zu",
                        parent_entry->name_size,
                        parent_entry->name, parent_entry->num_terminals);

        }
    }

    /* read rels */
    for (size_t i = 0; i < parent_entry->num_rels; i++) {
        rel_entry = parent_entry->rels[i];
        rel_entry->repo = self->entry->repo;
        err = self->rel->read_rel(self->rel, rel_entry, fd);
    }

    /* read procs */
    for (size_t i = 0; i < parent_entry->num_procs; i++) {
        proc_entry = parent_entry->procs[i];
        proc_entry->repo = self->entry->repo;
        err = self->proc->read_proc(self->proc, proc_entry, fd);
    }

    return knd_OK;
}



static int get_dir_trailer(struct kndClass *self,
                           struct kndClassEntry *parent_entry,
                           int fd,
                           int encode_base)
{
    size_t block_size = parent_entry->block_size;
    struct glbOutput *out = self->entry->repo->out;
    off_t offset = 0;
    size_t dir_size = 0;
    size_t chunk_size = 0;
    int err;

    if (block_size <= KND_DIR_ENTRY_SIZE)
        return knd_NO_MATCH;

    offset = (parent_entry->global_offset + block_size) - KND_DIR_ENTRY_SIZE;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        return knd_IO_FAIL;
    }
    out->reset(out);
    out->buf_size = KND_DIR_ENTRY_SIZE;
    err = read(fd, out->buf, out->buf_size);
    if (err == -1) return knd_IO_FAIL;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("  .. DIR size field read: \"%.*s\" [%zu]",
                out->buf_size, out->buf, out->buf_size);

    err =  knd_get_dir_size(self, &dir_size, &chunk_size, encode_base);
    if (err) {
        if (DEBUG_CONC_LEVEL_2)
            knd_log("-- couldn't find the ConcDir size field in \"%.*s\" :(",
                    out->buf_size, out->buf);
        return knd_NO_MATCH;
    }

    parent_entry->body_size = block_size - dir_size - chunk_size;
    parent_entry->dir_size = dir_size;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("  .. DIR: offset: %lu  block: %lu  body: %lu  dir: %lu",
                (unsigned long)parent_entry->global_offset,
                (unsigned long)parent_entry->block_size,
                (unsigned long)parent_entry->body_size,
                (unsigned long)parent_entry->dir_size);

    offset = (parent_entry->global_offset + block_size) - chunk_size - dir_size;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        return knd_IO_FAIL;
    }

    if (dir_size >= out->capacity) return knd_LIMIT;

    out->reset(out);
    out->buf_size = dir_size;
    err = read(fd, out->buf, out->buf_size);
    if (err == -1) return knd_IO_FAIL;
    out->buf[out->buf_size] = '\0';

    if (DEBUG_CONC_LEVEL_2) {
        chunk_size = out->buf_size > KND_MAX_DEBUG_CHUNK_SIZE ?\
            KND_MAX_DEBUG_CHUNK_SIZE :  out->buf_size;
        knd_log(".. parsing DIR: \"%.*s\" [size:%zu]",
                chunk_size, out->buf, out->buf_size);
    }

    err = parse_dir_trailer(self, parent_entry, fd, encode_base);
    if (err) {
        chunk_size =  out->buf_size > KND_MAX_DEBUG_CHUNK_SIZE ?\
                KND_MAX_DEBUG_CHUNK_SIZE :  out->buf_size;

        knd_log("-- failed to parse dir trailer: \"%.*s\" [size:%zu]",
                chunk_size, out->buf, out->buf_size);
        return err;
    }

    return knd_OK;
}


static int open_frozen_DB(struct kndClass *self)
{
    const char *filename;
    size_t filename_size;
    struct stat st;
    int fd;
    size_t file_size = 0;
    struct stat file_info;
    int err;

    filename = self->entry->repo->frozen_output_file_name;
    filename_size = self->entry->repo->frozen_output_file_name_size;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. open \"%.*s\" ..", filename_size, filename);

    if (stat(filename, &st)) {
        knd_log("-- no such file: %.*s", filename_size, filename);
        return knd_NO_MATCH;
    }

    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        knd_log("-- error reading FILE \"%.*s\": %d", filename_size, filename, fd);
        return knd_IO_FAIL;
    }

    fstat(fd, &file_info);
    file_size = file_info.st_size;
    if (file_size <= KND_DIR_ENTRY_SIZE) {
        err = knd_LIMIT;
        goto final;
    }

    self->entry->block_size = file_size;
    self->entry->id[0] = '/';

    /* TODO: get encode base from config */
    err = get_dir_trailer(self, self->entry, fd, KND_DIR_SIZE_ENCODE_BASE);
    if (err) {
        knd_log("-- error reading dir trailer in \"%.*s\"", filename_size, filename);
        goto final;
    }

    err = knd_OK;

    knd_log("++ frozen DB opened! total classes: %zu", self->entry->repo->next_class_numid);

 final:
    if (err) {
        knd_log("-- failed to open the frozen DB :(");
    }
    close(fd);
    return err;
}


static int read_obj_entry(struct kndClass *self,
                          struct kndObjEntry *entry,
                          struct kndObject **result)
{
    struct kndObject *obj;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    const char *filename;
    size_t filename_size;
    const char *c, *b, *e;
    size_t chunk_size;
    struct stat st;
    int fd;
    size_t file_size = 0;
    struct stat file_info;
    int err;
    gsl_err_t parser_err;

    /* parse DB rec */
    filename = self->entry->repo->frozen_output_file_name;
    filename_size = self->entry->repo->frozen_output_file_name_size;
    if (!filename_size) {
        knd_log("-- no file name to read in conc %.*s :(",
                self->entry->name_size, self->entry->name);
        return knd_FAIL;
    }

    if (stat(filename, &st)) {
        knd_log("-- no such file: \"%.*s\"", filename_size, filename);
        return knd_NO_MATCH;
    }

    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        knd_log("-- error reading FILE \"%.*s\": %d", filename_size, filename, fd);
        return knd_IO_FAIL;
    }
    fstat(fd, &file_info);
    file_size = file_info.st_size;
    if (file_size <= KND_DIR_ENTRY_SIZE) {
        err = knd_LIMIT;
        goto final;
    }

    if (lseek(fd, entry->offset, SEEK_SET) == -1) {
        err = knd_IO_FAIL;
        goto final;
    }

    entry->block = malloc(entry->block_size + 1);
    if (!entry->block) return knd_NOMEM;

    err = read(fd, entry->block, entry->block_size);
    if (err == -1) {
        err = knd_IO_FAIL;
        goto final;
    }
    /* NB: current parser expects a null terminated string */
    entry->block[entry->block_size] = '\0';

    if (DEBUG_CONC_LEVEL_2)
        knd_log("   == OBJ REC: \"%.*s\"",
                entry->block_size, entry->block);

    /* done reading */
    close(fd);

    err = mempool->new_obj(mempool, &obj);                                        RET_ERR();
    err = mempool->new_state(mempool, &obj->states);                              RET_ERR();
    obj->states->phase = KND_FROZEN;

    obj->base = self;
    entry->obj = obj;
    obj->entry = entry;

    /* skip over initial brace '{' */
    c = entry->block + 1;
    b = c;
    bool got_separ = false;
    /* ff the name */
    while (*c) {
        switch (*c) {
        case '{':
        case '}':
        case '[':
        case ']':
            got_separ = true;
            e = c;
            break;
        default:
            break;
        }
        if (got_separ) break;
        c++;
    }

    if (!got_separ) {
        knd_log("-- obj name not found in \"%.*s\" :(",
                entry->block_size, entry->block);
        obj->del(obj);
        return knd_FAIL;
    }

    obj->name = entry->name;
    obj->name_size = entry->name_size;

    parser_err = obj->read(obj, c, &chunk_size);
    if (parser_err.code) {
        knd_log("-- failed to parse obj %.*s :(",
                obj->name_size, obj->name);
        obj->del(obj);
        return gsl_err_to_knd_err_codes(parser_err);
    }

    if (DEBUG_CONC_LEVEL_2)
        obj->str(obj);

    *result = obj;
    return knd_OK;

 final:
    close(fd);
    return err;
}



static int parse_obj_dir_trailer(struct kndClass *self,
                                 struct kndClassEntry *parent_entry,
                                 int fd)
{
    struct gslTaskSpec obj_dir_spec = {
        .is_list_item = true,
        .accu = parent_entry,
        .alloc = obj_entry_alloc,
        .append = obj_entry_append
    };

    struct gslTaskSpec specs[] = {
        { .type = GSL_SET_ARRAY_STATE,
          .name = "o",
          .name_size = strlen("o"),
          .parse = gsl_parse_array,
          .obj = &obj_dir_spec
        }
    };
    size_t parsed_size = 0;
    size_t *total_size = &parsed_size;
    char *obj_dir_buf = self->entry->repo->out->buf;
    size_t obj_dir_buf_size = self->entry->repo->out->buf_size;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CONC_LEVEL_1)
        knd_log(".. parsing OBJ DIR REC: %.*s [size %zu]",
                128, obj_dir_buf, obj_dir_buf_size);

    if (!parent_entry->obj_dir) {
        err = self->entry->repo->mempool->new_obj_dir(self->entry->repo->mempool, &parent_entry->obj_dir);
        if (err) return err;
    }
    parent_entry->fd = fd;

    if (!parent_entry->obj_name_idx) {
        err = ooDict_new(&parent_entry->obj_name_idx, parent_entry->num_objs);            RET_ERR();

        err = self->entry->repo->mempool->new_set(self->entry->repo->mempool, &parent_entry->obj_idx);            RET_ERR();
        parent_entry->obj_idx->type = KND_SET_CLASS;
    }

    parser_err = gsl_parse_task(obj_dir_buf, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== \"%.*s\" total objs: %zu",
                parent_entry->name_size, parent_entry->name,
                parent_entry->num_objs);

    return knd_OK;
}

static int get_obj_dir_trailer(struct kndClass *self,
                                struct kndClassEntry *parent_entry,
                                int fd,
                                int encode_base)
{
    off_t offset = 0;
    size_t dir_size = 0;
    size_t chunk_size = 0;
    size_t block_size = parent_entry->block_size;
    struct glbOutput *out = self->entry->repo->out;
    int err;

    offset = parent_entry->global_offset + block_size +\
        parent_entry->obj_block_size - KND_DIR_ENTRY_SIZE;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        return knd_IO_FAIL;
    }
    out->reset(out);
    out->buf_size = KND_DIR_ENTRY_SIZE;
    err = read(fd, out->buf, out->buf_size);
    if (err == -1) return knd_IO_FAIL;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log("\n  .. OBJ DIR ENTRY SIZE REC: \"%.*s\"",
                out->buf_size, out->buf);
    }

    err =  knd_get_dir_size(self, &dir_size, &chunk_size, encode_base);
    if (err) {
        knd_log("-- failed to read dir size :(");
        return err;
    }

    if (DEBUG_CONC_LEVEL_2)
        knd_log("\n  .. OBJ DIR REC SIZE: %lu [size field size: %lu]",
                dir_size, (unsigned long)chunk_size);

    offset = (parent_entry->global_offset + block_size +\
              parent_entry->obj_block_size) - chunk_size - dir_size;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        return knd_IO_FAIL;
    }

    out->reset(out);
    if (dir_size >= out->capacity) {
        knd_log("-- DIR size %zu exceeds current capacity: %zu",
                dir_size, out->capacity);
        return knd_LIMIT;
    }
    out->buf_size = dir_size;
    err = read(fd, out->buf, out->buf_size);
    if (err == -1) return knd_IO_FAIL;
    out->buf[out->buf_size] = '\0';

    if (DEBUG_CONC_LEVEL_2) {
        chunk_size = out->buf_size > KND_MAX_DEBUG_CHUNK_SIZE ? \
            KND_MAX_DEBUG_CHUNK_SIZE :  out->buf_size;

        knd_log("== OBJ DIR REC: %.*s [size:%zu]",
                chunk_size, out->buf, out->buf_size);
    }

    err = parse_obj_dir_trailer(self, parent_entry, fd);
    if (err) return err;

    return knd_OK;
}

static gsl_err_t obj_entry_append(void *accu,
                                  void *item)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndClassEntry *parent_entry = accu;
    struct kndObjEntry *entry = item;
    struct kndSet *set;
    off_t offset = 0;
    int fd = parent_entry->fd;
    int err;

    entry->offset = parent_entry->curr_offset;

    if (DEBUG_CONC_LEVEL_1)
        knd_log("\n.. ConcDir: %.*s to append atomic obj entry"
                " (block size: %zu) offset:%zu",
                parent_entry->name_size, parent_entry->name,
                entry->block_size, entry->offset);

    buf_size = entry->block_size;
    if (entry->block_size > KND_NAME_SIZE)
        buf_size = KND_NAME_SIZE;

    offset = entry->offset;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        return make_gsl_err_external(knd_IO_FAIL);
    }

    err = read(fd, buf, buf_size);
    if (err == -1) return make_gsl_err_external(knd_IO_FAIL);

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. OBJ BODY INCIPIT: \"%.*s\"",
                buf_size, buf);

    entry->id_size = KND_ID_SIZE;
    entry->name_size = KND_NAME_SIZE;
    err = knd_parse_incipit(buf, buf_size,
                                   entry->id, &entry->id_size,
                                   entry->name, &entry->name_size);
    if (err)  return make_gsl_err_external(err);

    parent_entry->curr_offset += entry->block_size;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== OBJ id:%.*s name:%.*s",
                entry->id_size, entry->id,
                entry->name_size, entry->name);

    set = parent_entry->obj_idx;
    err = set->add(set, entry->id, entry->id_size, (void*)entry);
    if (err) {
        knd_log("-- failed to update the obj idx :(");
        return make_gsl_err_external(err);
    }
    /* update name idx */
    err = parent_entry->obj_name_idx->set(parent_entry->obj_name_idx,
                                        entry->name, entry->name_size,
                                        entry);
    if (err) {
        knd_log("-- failed to update the obj name idx entry :(");
        return make_gsl_err_external(err);
    }

    return make_gsl_err(gsl_OK);
}



static int idx_class_name(struct kndClass *self,
                          struct kndClassEntry *entry,
                          int fd)
{
    char buf[KND_NAME_SIZE + 1];
    size_t buf_size;
    off_t offset = 0;
    void *result;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("  .. get conc name in DIR: \"%.*s\""
                " global off:%zu  block size:%zu",
                entry->name_size, entry->name,
                entry->global_offset, entry->block_size);

    buf_size = entry->block_size;
    if (entry->block_size > KND_NAME_SIZE)
        buf_size = KND_NAME_SIZE;

    offset = entry->global_offset;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        return knd_IO_FAIL;
    }
    err = read(fd, buf, buf_size);
    if (err == -1) return knd_IO_FAIL;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("\n  .. CONC BODY: %.*s",
                buf_size, buf);
    buf[buf_size] = '\0';

    entry->id_size = KND_ID_SIZE;
    entry->name_size = KND_NAME_SIZE;
    err = knd_parse_incipit(buf, buf_size,
                            entry->id, &entry->id_size,
                            entry->name, &entry->name_size);
    if (err) return err;

    knd_calc_num_id(entry->id, entry->id_size, &entry->numid);

    err = self->class_idx->add(self->class_idx,
                               entry->id, entry->id_size, (void*)entry);                RET_ERR();

    err = self->class_name_idx->set(self->class_name_idx,
                                    entry->name, entry->name_size, entry);              RET_ERR();


    err = self->class_idx->get(self->class_idx,
                               entry->id, entry->id_size, &result);                   RET_ERR();
    entry = result;

    return knd_OK;
}

static gsl_err_t obj_entry_alloc(void *obj,
                                 const char *val,
                                 size_t val_size,
                                 size_t count,
                                 void **item)
{
    struct kndClassEntry *parent_entry = obj;
    struct kndObjEntry *entry = NULL;
    struct kndMemPool *mempool = parent_entry->repo->mempool;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. create OBJ ENTRY: %.*s  count: %zu",
                val_size, val, count);

    err = mempool->new_obj_entry(mempool, &entry);
    if (err) return make_gsl_err_external(err);

    knd_calc_num_id(val, val_size, &entry->block_size);

    *item = entry;

    return make_gsl_err(gsl_OK);
}



static gsl_err_t dir_entry_append(void *accu,
                                  void *item)
{
    struct kndClassEntry *parent_entry = accu;
    struct kndClassEntry *entry = item;
    struct kndMemPool *mempool = parent_entry->repo->mempool;
    int err;

    if (!parent_entry->child_idx) {
        err = mempool->new_set(mempool,
                               &parent_entry->child_idx);
        if (err) return make_gsl_err_external(err);
    }

    if (!parent_entry->children) {
        parent_entry->children = calloc(KND_MAX_CONC_CHILDREN,
                                      sizeof(struct kndClassEntry*));
        if (!parent_entry->children) {
            knd_log("-- no memory :(");
            return make_gsl_err_external(knd_NOMEM);
        }
    }

    if (parent_entry->num_children >= KND_MAX_CONC_CHILDREN) {
        knd_log("-- warning: num of subclasses of \"%.*s\" exceeded :(",
                parent_entry->name_size, parent_entry->name);
        return make_gsl_err(gsl_OK);
    }

    parent_entry->children[parent_entry->num_children] = entry;
    parent_entry->num_children++;

    entry->global_offset += parent_entry->curr_offset;
    parent_entry->curr_offset += entry->block_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t dir_entry_alloc(void *self,
                                 const char *name,
                                 size_t name_size,
                                 size_t count,
                                 void **item)
{
    struct kndClassEntry *parent_entry = self;
    struct kndClassEntry *entry;
    struct kndMemPool *mempool = parent_entry->repo->mempool;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. %.*s to add list item: %.*s count: %zu"
                " [total children: %zu]",
                parent_entry->id_size, parent_entry->id, name_size, name,
                count, parent_entry->num_children);

    if (name_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    err = mempool->new_class_entry(mempool, &entry);
    if (err) return make_gsl_err_external(err);

    knd_calc_num_id(name, name_size, &entry->block_size);

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== block size: %zu", entry->block_size);

    *item = entry;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t reldir_entry_alloc(void *self,
                                    const char *name,
                                    size_t name_size,
                                    size_t count,
                                    void **item)
{
    struct kndClassEntry *parent_entry = self;
    struct kndRelEntry *entry;
    struct kndMemPool *mempool = parent_entry->repo->mempool;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. create REL DIR ENTRY: %.*s count: %zu",
                name_size, name, count);

    if (name_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    err = mempool->new_rel_entry(mempool, &entry);
    if (err) return make_gsl_err_external(err);
    knd_calc_num_id(name, name_size, &entry->block_size);

    *item = entry;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t reldir_entry_append(void *accu,
                                     void *item)
{
    struct kndClassEntry *parent_entry = accu;
    struct kndRelEntry *entry = item;

    if (!parent_entry->rels) {
        parent_entry->rels = calloc(KND_MAX_RELS,
                                  sizeof(struct kndRelEntry*));
        if (!parent_entry->rels) return make_gsl_err_external(knd_NOMEM);
    }

    if (parent_entry->num_rels + 1 > KND_MAX_RELS) {
        knd_log("-- warning: max rels of \"%.*s\" exceeded :(",
                parent_entry->name_size, parent_entry->name);
        return make_gsl_err(gsl_OK);
    }

    parent_entry->rels[parent_entry->num_rels] = entry;
    parent_entry->num_rels++;

    entry->global_offset += parent_entry->curr_offset;
    parent_entry->curr_offset += entry->block_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t procdir_entry_alloc(void *self,
                                     const char *name,
                                     size_t name_size,
                                     size_t count,
                                     void **item)
{
    struct kndClassEntry *parent_entry = self;
    struct kndProcEntry *entry;
    struct kndMemPool *mempool = parent_entry->repo->mempool;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. create PROC DIR ENTRY: %.*s count: %zu",
                name_size, name, count);

    if (name_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    err = mempool->new_proc_entry(mempool, &entry);
    if (err) return make_gsl_err_external(err);

    knd_calc_num_id(name, name_size, &entry->block_size);

    *item = entry;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t procdir_entry_append(void *accu,
                                      void *item)
{
    struct kndClassEntry *parent_entry = accu;
    struct kndProcEntry *entry = item;

    if (!parent_entry->procs) {
        parent_entry->procs = calloc(KND_MAX_PROCS,
                                   sizeof(struct kndProcEntry*));
        if (!parent_entry->procs) return make_gsl_err_external(knd_NOMEM);
    }

    if (parent_entry->num_procs + 1 > KND_MAX_PROCS) {
        knd_log("-- warning: max procs of \"%.*s\" exceeded :(",
                parent_entry->name_size, parent_entry->name);
        return make_gsl_err(gsl_OK);
    }

    parent_entry->procs[parent_entry->num_procs] = entry;
    parent_entry->num_procs++;

    entry->global_offset += parent_entry->curr_offset;
    parent_entry->curr_offset += entry->block_size;

    return make_gsl_err(gsl_OK);
}
