
static int proc_call_arg_export_GSP(struct kndProc *unused_var(self),
                                    struct kndProcCallArg *call_arg,
                                    struct glbOutput *out)
{
    int err;
    err = out->write(out, "{", 1);                                                RET_ERR();
    err = out->write(out, call_arg->name, call_arg->name_size);                   RET_ERR();
    err = out->write(out, " ", 1);                                                RET_ERR();
    err = out->write(out, call_arg->val, call_arg->val_size);                     RET_ERR();
    err = out->write(out, "}", 1);                                                RET_ERR();
    return knd_OK;
}

static int export_GSP(struct kndProc *self,
                      struct kndTask *task,
                      struct glbOutput *out)
{
    struct kndProcArg *arg;
    struct kndProcCallArg *call_arg;
    struct kndTranslation *tr;
    int err;

    err = out->writec(out, '{');
    if (err) return err;
    err = out->write(out, self->entry->id, self->entry->id_size);
    if (err) return err;
    err = out->writec(out, ' ');
    if (err) return err;

    err = out->write(out, self->name, self->name_size);                           RET_ERR();
    if (self->tr) {
         err = out->write(out, "[_g", strlen("[_g"));                              RET_ERR();
    }

    tr = self->tr;
    while (tr) {
        err = out->write(out, "{", 1);                                            RET_ERR();
        err = out->write(out, tr->locale, tr->locale_size);                       RET_ERR();
        err = out->write(out, "{t ", 3);                                          RET_ERR();
        err = out->write(out, tr->val,  tr->val_size);                            RET_ERR();
        err = out->write(out, "}}", 2);                                           RET_ERR();
        tr = tr->next;
    }
    if (self->tr) {
        err = out->write(out, "]", 1);                                            RET_ERR();
    }


    /* if (self->summary) {
        err = out->write(out, "[_summary", strlen("[_summary"));                  RET_ERR();
    }

    tr = self->summary;
    while (tr) {
        err = out->write(out, "{", 1);                                            RET_ERR();
        err = out->write(out, tr->locale, tr->locale_size);                       RET_ERR();
        err = out->write(out, "{t ", 3);                                          RET_ERR();
        err = out->write(out, tr->val,  tr->val_size);                            RET_ERR();
        err = out->write(out, "}}", 2);                                           RET_ERR();
        tr = tr->next;
    }
    if (self->summary) {
        err = out->write(out, "]", 1);                                            RET_ERR();
    }
    */
    if (self->args) {
        err = out->write(out, "[arg ", strlen("[arg "));                          RET_ERR();
        for (arg = self->args; arg; arg = arg->next) {

            err = knd_proc_arg_export(arg, KND_FORMAT_GSP, task, out);                  RET_ERR();
        }
        err = out->writec(out, ']');                                              RET_ERR();
    }

    if (self->proc_call) {
        err = out->write(out, "{run ", strlen("{run "));                          RET_ERR();
        err = out->write(out, self->proc_call->name,
                         self->proc_call->name_size);   RET_ERR();

        for (call_arg = self->proc_call->args;
             call_arg;
             call_arg = call_arg->next) {
            err = proc_call_arg_export_GSP(self, call_arg, out);                  RET_ERR();
        }
        err = out->write(out, "}", 1);                                            RET_ERR();
    }

    err = out->write(out, "}", 1);                                                RET_ERR();
    return knd_OK;
}


static gsl_err_t set_proc_call_name(void *obj,
                                    const char *name, size_t name_size)
{
    struct kndProcCall *self = obj;
    if (!name_size) return make_gsl_err(gsl_FORMAT);
    self->name = name;
    self->name_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_proc_call(void *obj,
                                 const char *rec,
                                 size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndProc *proc = ctx->proc;
    struct kndProcCall *proc_call = proc->proc_call;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. Proc Call parsing: \"%.*s\"..",
                32, rec);

    if (!proc_call) {
        err = knd_proc_call_new(ctx->task->mempool, &proc->proc_call);
        if (err) return make_gsl_err_external(err);
        proc_call = proc->proc_call;
    }

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_proc_call_name,
          .obj = proc_call
        }/*,
        { .type = GSL_GET_ARRAY_STATE,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .parse = parse_gloss,
          .obj = proc_call
          }*/,
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .parse = parse_gloss,
          .obj = proc_call
        }/*,
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_summary",
          .name_size = strlen("_summary"),
          .parse = parse_summary,
          .obj = proc_call
          }*/,
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_g",
          .name_size = strlen("_g"),
          .parse = parse_gloss,
          .obj = proc_call
        },
        { .validate = parse_proc_call_arg,
          .obj = proc
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    // TODO: lookup table
    if (!strncmp("_mult", proc_call->name, proc_call->name_size))
        proc_call->type = KND_PROC_MULT;

    if (!strncmp("_sum", proc_call->name, proc_call->name_size))
        proc_call->type = KND_PROC_SUM;

    if (!strncmp("_mult_percent", proc_call->name, proc_call->name_size))
        proc_call->type = KND_PROC_MULT_PERCENT;

    if (!strncmp("_div_percent", proc_call->name, proc_call->name_size))
        proc_call->type = KND_PROC_DIV_PERCENT;

    return make_gsl_err(gsl_OK);
}


