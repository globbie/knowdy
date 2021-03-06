    task = self->tasks[0];
    ctx = malloc(sizeof(struct kndTaskContext));
    if (!ctx) return knd_NOMEM;
    memset(ctx, 0, (sizeof(struct kndTaskContext)));
    ctx->class_name_idx = repo->class_name_idx;
    ctx->attr_name_idx  = repo->attr_name_idx;
    ctx->proc_name_idx  = repo->proc_name_idx;
    ctx->proc_arg_name_idx  = repo->proc_arg_name_idx;


    err = knd_output_new(&ctx->out, NULL, KND_TEMP_BUF_SIZE);
    if (err) goto error;
    err = knd_output_new(&ctx->log, NULL, KND_TEMP_BUF_SIZE);
    if (err) goto error;


    /* init fields in tasks */
    for (size_t i = 0; i < self->num_tasks; i++) {
        task = self->tasks[i];
        task->user = user;
        task->storage = self->storage;
        task->path = self->path;
        task->path_size = self->path_size;

        /* NB: the same queue is used as input and output */
        task->input_queue = self->task_context_queue;
        task->output_queue = self->task_context_queue;

        task->system_repo = self->repo;
    }





/* IO service */
    err = knd_storage_new(&self->storage, task_queue_capacity);
    if (err != knd_OK) goto error;
    self->storage->output_queue = self->task_context_queue;
    memcpy(self->storage->path, self->path, self->path_size);
    self->storage->path_size = self->path_size;
    self->storage->ctx_idx = self->ctx_idx;

    /* global commit filename */
    c = self->storage->commit_filename;
    memcpy(c, self->path, self->path_size);
    c += self->path_size;
    chunk_size = self->path_size;
    *c = '/';
    c++;
    chunk_size++;

    memcpy(c, "commit.log", strlen("commit.log"));
    c++;
    chunk_size += strlen("commit.log");
    self->storage->commit_filename_size = chunk_size;






    if (!self->num_tasks) self->num_tasks = 1;

    self->tasks = calloc(self->num_tasks, sizeof(struct kndTask*));
    if (!self->tasks) goto error;

    for (size_t i = 0; i < self->num_tasks; i++) {
        err = knd_task_new(&task);
        if (err != knd_OK) goto error;
        self->tasks[i] = task;

        err = kndMemPool_new(&task->mempool);
        if (err != knd_OK) goto error;

        // TODO: clone function
        task->mempool->num_pages = mempool->num_pages;
        task->mempool->num_small_x4_pages = mempool->num_small_x4_pages;
        task->mempool->num_small_x2_pages = mempool->num_small_x2_pages;
        task->mempool->num_small_pages = mempool->num_small_pages;
        task->mempool->num_tiny_pages = mempool->num_tiny_pages;

        task->mempool->alloc(task->mempool);
    }

    // TODO
    size_t task_queue_capacity = TASK_QUEUE_CAPACITY; //self->num_tasks * mempool->num_small_pages;
    err = knd_queue_new(&self->task_context_queue, task_queue_capacity);
    if (err != knd_OK) goto error;






static void *task_runner(void *ptr)
{
    struct kndTask *task = ptr;
    struct kndQueue *queue = task->input_queue;
    struct kndTaskContext *ctx;
    struct timespec ts = {0, TASK_TIMEOUT_USECS * 1000L };
    void *elem;
    size_t attempt_count = 0;
    int err;

    knd_log("\n.. shard's task runner #%zu..", task->id);

    while (1) {
        attempt_count++;
        err = knd_queue_pop(queue, &elem);
        if (err) {
            if (attempt_count > MAX_DEQUE_ATTEMPTS)
                nanosleep(&ts, NULL);
            continue;
        }
        attempt_count = 0;
        ctx = elem;
        if (ctx->type == KND_STOP_STATE) {
            knd_log("\n-- shard's task runner #%zu received a stop signal..",
                    task->id);
            return NULL;
        }

        if (DEBUG_SHARD_LEVEL_1)
            knd_log("++ #%zu worker got task #%zu!",
                    task->id, ctx->numid);

        switch (ctx->phase) {
        case KND_SUBMIT:
            knd_task_reset(task);

            task->ctx = ctx;
            task->out = ctx->out;

            err = knd_task_run(task);
            if (err != knd_OK) {
                ctx->error = err;
                knd_log("-- task running failure: %d", err);
            }
            continue;
        case KND_COMPLETE:
            knd_log("\n-- task #%zu already complete",
                    ctx->numid);
            continue;
        case KND_CANCEL:
            knd_log("\n-- task #%zu was canceled",
                    ctx->numid);
            continue;
        default:
            break;
        }

        /* any other phase requires a callback execution */
        if (ctx->cb) {
            err = ctx->cb((void*)task, ctx->id, ctx->id_size, (void*)ctx);
            if (err) {
                // signal
            }
        }
    }
    return NULL;
}

/* non-blocking interface */
int knd_shard_push_task(struct kndShard *self,
                        const char *input, size_t input_size,
                        const char **task_id, size_t *task_id_size,
                        task_cb_func cb, void *obj)
{
    struct kndTaskContext *ctx;
    clockid_t clk_id = CLOCK_MONOTONIC;
    int err;

    ctx = malloc(sizeof(struct kndTaskContext));
    if (!ctx) return knd_NOMEM;
    memset(ctx, 0, (sizeof(struct kndTaskContext)));
    ctx->external_cb = cb;
    ctx->external_obj = obj;

    ctx->input_buf = malloc(input_size + 1);
    if (!ctx->input_buf) return knd_NOMEM; 
    memcpy(ctx->input_buf, input, input_size);
    ctx->input_buf[input_size] = '\0';
    ctx->input = ctx->input_buf;
    ctx->input_size = input_size;

    self->task_count++;
    ctx->numid = self->task_count;
    knd_uid_create(ctx->numid, ctx->id, &ctx->id_size);

    err = clock_gettime(clk_id, &ctx->start_ts);

    /*strftime(buf, sizeof buf, "%D %T", gmtime(&start_ts.tv_sec));
    knd_log("UTC %s.%09ld: new task curr storage size:%zu  capacity:%zu",
            buf, start_ts.tv_nsec,
            self->task_storage->buf_size, self->task_storage->capacity);
    */
    
    err = knd_queue_push(self->task_context_queue, (void*)ctx);
    if (err) return err;

    knd_log("++ enqueued task #%zu", ctx->numid);

    *task_id = ctx->id; 
    *task_id_size = ctx->id_size;
    return knd_OK;
}



/* blocking interface */
int knd_shard_run_task(struct kndShard *self,
                       const char *input, size_t input_size,
                       char *output, size_t *output_size)
{
    struct kndTaskContext *ctx;
    clockid_t clk_id = CLOCK_MONOTONIC;
    struct timespec ts = {0, TASK_TIMEOUT_USECS * 1000L };
    size_t num_attempts = 0;
    int err;

    ctx = malloc(sizeof(struct kndTaskContext));
    if (!ctx) return knd_NOMEM;
    memset(ctx, 0, (sizeof(struct kndTaskContext)));

    err = clock_gettime(clk_id, &ctx->start_ts);
    if (err) return err;

    ctx->input_buf = malloc(input_size + 1);
    if (!ctx->input_buf) return knd_NOMEM; 
    memcpy(ctx->input_buf, input, input_size);
    ctx->input_buf[input_size] = '\0';
    ctx->input = ctx->input_buf;
    ctx->input_size = input_size;

    ctx->batch_max = KND_RESULT_BATCH_SIZE;
    
    err = knd_output_new(&ctx->out, output, *output_size);
    if (err) goto final;
    *output_size = 0;

    err = knd_output_new(&ctx->log, NULL, KND_TEMP_BUF_SIZE);
    if (err) goto final;

    self->task_count++;
    ctx->numid = self->task_count;
    knd_uid_create(ctx->numid, ctx->id, &ctx->id_size);

    /*strftime(buf, sizeof buf, "%D %T", gmtime(&start_ts.tv_sec));
    knd_log("UTC %s.%09ld: new task curr storage size:%zu  capacity:%zu",
            buf, start_ts.tv_nsec,
            self->task_storage->buf_size, self->task_storage->capacity);
    */

    //err = knd_queue_push(self->storage->input_queue, (void*)ctx);
    //if (err) return err;
    ctx->phase = KND_SUBMIT;
    err = knd_queue_push(self->task_context_queue, (void*)ctx);
    if (err) return err;

    while (1) {
        err = clock_gettime(clk_id, &ctx->end_ts);
        if (err) goto final;

        if ((ctx->end_ts.tv_sec - ctx->start_ts.tv_sec) > TASK_MAX_TIMEOUT_SECS) {
            // signal timeout
            err = knd_task_err_export(ctx);
            if (err) goto final;
            *output_size = ctx->out->buf_size;
            break;
        }

        // TODO atomic load
        switch (ctx->phase) {
        case KND_COMPLETE:
            if (ctx->error) {
                err = knd_task_err_export(ctx);
                if (err) return err;
            }
            *output_size = ctx->out->buf_size;

            if (DEBUG_SHARD_LEVEL_1)
                knd_log("\n== RESULT: \"%.*s\" (size:%zu)\n"
                        "== task progress polling, num attempts: %zu\n",
                        ctx->out->buf_size, ctx->out->buf,
                        ctx->out->buf_size, num_attempts);

            return knd_OK;
        default:
            break;
        }
        nanosleep(&ts, NULL);
        num_attempts++;
    }

    return knd_OK;

 final:
    // TODO free ctx
    return err;
}

int knd_shard_serve(struct kndShard *self)
{
    struct kndTask *task;
    int err;

    for (size_t i = 0; i < self->num_tasks; i++) {
        task = self->tasks[i];
        task->id = i;

        if (pthread_create(&task->thread, NULL, task_runner, (void*)task)) {
            perror("-- kndTask thread creation failed");
            return knd_FAIL;
        }
    }

    err = knd_storage_serve(self->storage);
    if (err) return err;

    return knd_OK;
}

int knd_shard_stop(struct kndShard *self)
{
    struct kndTaskContext ctx;
    struct kndTask *task;
    int err;

    err = knd_storage_stop(self->storage);
    if (err) return err;

    memset(&ctx, 0, sizeof(struct kndTaskContext));
    ctx.type = KND_STOP_STATE;

    knd_log(".. scheduling shard stop tasks..");

    for (size_t i = 0; i < self->num_tasks * 2; i++) {
        err = knd_queue_push(self->task_context_queue, (void*)&ctx);
        if (err) return err;
    }

    for (size_t i = 0; i < self->num_tasks; i++) {
        task = self->tasks[i];
        pthread_join(task->thread, NULL);
    }

    knd_log(".. shard tasks stopped.");
    return knd_OK;
}
