#include <knd_shard.h>
#include <knd_queue.h>
#include <knd_task.h>
#include <knd_utils.h>
#include <knd_state.h>

#include <check.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>

// todo(n.rodionov): make paths configurable
static const char *shard_config =
"{schema knd"
"  {agent 007}"
"  {db-path .}"
"  {num-workers 4}"
"  {schema-path ../../tests/schemas/system"
"    {user User"
"       {base-repo test"
"         {schema-path ../../tests/schemas/test}}}}"
"  {memory"
"    {max_base_pages      20000}"
"    {max_small_x4_pages  4500}"
"    {max_small_x2_pages  150000}"
"    {max_small_pages     23000}"
"    {max_tiny_pages      200000}"
"  }"
"}";

#define MAX_TASKS 42
#define MAX_DEQUE_ATTEMPTS 100

#define ASSERT_STR_EQ(act, act_size, exp, exp_size) \
    do {                                            \
        const char *__act = (act);                  \
        size_t __act_size = (act_size);             \
        const char *__exp = (exp);                  \
        size_t __exp_size = ((size_t)(exp_size) == (size_t)-1 ? strlen(__exp) : (size_t)(exp_size)); \
        ck_assert_msg(__act_size == __exp_size && 0 == strncmp(__act, __exp, __act_size),            \
            "Assertion '%s' failed: %s == \"%.*s\" [len: %zu] but expected \"%.*s\" [len: %zu]",     \
            #act" == "#exp, #act, __act_size, __act, __act_size, __exp_size, __exp, __exp_size);     \
    } while (0)

struct table_test {
    const char *input;
    const char *expect;
    int err;
};

typedef struct worker {
    pthread_t thread;
    size_t id;
    struct kndShard *shard;
    const char *input;
    size_t input_size;

    const char *result;
    size_t result_size;

    const char *expect;
    size_t expect_size;

    size_t num_success;
    size_t num_failed;
} worker_t;

static void *worker_proc(void *ptr)
{
    worker_t *worker = (worker_t *)ptr;
    struct kndQueue *queue = worker->shard->task_context_queue;
    struct kndTaskContext *ctx;
    void *elem;
    size_t attempt_count = 0;
    int err;

    fprintf(stdout, "== worker: #%zu..\n", worker->id);

    while (1) {
        attempt_count++;
        err = knd_queue_pop(queue, &elem);
        if (err) {
            if (attempt_count > MAX_DEQUE_ATTEMPTS)
                usleep(500);
            continue;
        }
        ctx = elem;
        ctx->phase = KND_COMPLETE;
        attempt_count = 0;

        fprintf(stdout, "++ #%zu worker got task #%zu!\n",
                worker->id, ctx->numid);
        usleep(1000);
    }
    return NULL;
}

static void *task_assign_proc(void *ptr)
{
    worker_t *worker = (worker_t *)ptr;
    struct kndMemPool *mempool = worker->shard->mempool;
    struct kndQueue *queue = worker->shard->task_context_queue;
    struct kndTaskContext *ctx = NULL;
    int err;

    fprintf(stdout, "== task assigner: #%zu..\n", worker->id);

    for (size_t i = 0; i < MAX_TASKS; ++i) {
        if (!ctx) {
            err = knd_task_context_new(mempool, &ctx);
            if (err) return NULL;
            ctx->numid = i;
            ctx->next = worker->shard->contexts;
            worker->shard->contexts = ctx;

            fprintf(stdout, ".. create task #%zu..\n",
                    ctx->numid);
        }

    retry_push:
        err = knd_queue_push(queue, (void*)ctx);
        if (err) {
            knd_log("-- queue still full at task %zu", ctx->numid);
            sleep(3);
            goto retry_push;
        }

        ctx = NULL;
    }
    
    return NULL;
}

START_TEST(shard_concurrent_import_test)
{
    struct kndShard *shard;
    worker_t **workers;
    worker_t *worker;
    int err;

    err = knd_shard_new(&shard, shard_config, strlen(shard_config));
    ck_assert_int_eq(err, knd_OK);

    fprintf(stdout, "== total num of workers: %zu\n", shard->num_tasks);

    workers = calloc(sizeof(worker_t*), shard->num_tasks);
    if (!workers) return;

    for (size_t i = 0; i < shard->num_tasks; i++) {
        if ((worker = malloc(sizeof(worker_t))) == NULL) {
            perror("-- worker allocation failed");
            return;
        }
        memset(worker, 0, sizeof(*worker));
        worker->id = i;
        worker->shard = shard;
        workers[i] = worker;

        if (i == 0) {
            if (pthread_create(&worker->thread, NULL, task_assign_proc, (void*)worker)) {
                perror("-- task assigner thread creation failed");
                free(worker);
                return;
            }
            continue;
        }

        if (pthread_create(&worker->thread, NULL, worker_proc, (void*)worker)) {
            perror("-- worker thread creation failed");
            free(worker);
            return;
        }
    }

    for (size_t i = 0; i < shard->num_tasks; i++) {
        worker = workers[i];
        pthread_join(worker->thread, NULL);
    }

    // TODO: check expectations
    struct kndTaskContext *ctx;
    for (ctx = shard->contexts; ctx; ctx = ctx->next) {
        knd_log("== ctx #%zu", ctx->numid);
    }
    
    /* release resources */
    for (size_t i = 0; i < shard->num_tasks; i++)
        free(workers[i]);

    free(workers);
    knd_shard_del(shard);
}
END_TEST

int main(void) {
    Suite *s = suite_create("suite");

    TCase *tc_shard_concurrent = tcase_create("concurrent shard");
    tcase_set_timeout(tc_shard_concurrent, 100);

    tcase_add_test(tc_shard_concurrent, shard_concurrent_import_test);
    
    suite_add_tcase(s, tc_shard_concurrent);

    SRunner* sr = srunner_create(s);
    //srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr, CK_NORMAL);
    int num_failures = srunner_ntests_failed(sr);
    srunner_free(sr);

    return num_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
