#include <knd_shard.h>

#include <check.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
} worker_t;

static void *worker_proc(void *ptr)
{
    worker_t *worker = (worker_t *)ptr;
    int err;

    fprintf(stdout, "worker:%zu..\n", worker->id);

    /*for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        const struct table_test *pcase = &cases[i];

        const char *result; size_t result_size;
        fprintf(stdout, "Checking #%zu: %s...\n", i, pcase->input);
    */
    worker->input = "{task{!class test}}";
    worker->input_size = strlen("{task{!class test}}");

    err = kndShard_run_task(worker->shard, worker->input, worker->input_size,
                            &worker->result, &worker->result_size, worker->id);
    //ck_assert_int_eq(err, knd_OK);

    //ASSERT_STR_EQ(worker->result, worker->result_size,
    //              worker->expect, worker->expect_size);
    
    return NULL;
}

START_TEST(shard_concurrent_import_test)
{
    /*    static const struct table_test cases[] = {
        {
            .input = "{task {class User {!inst Vasya}}}",
            .expect = "{\"result\":\"OK\"}"
        }
    };
    */
    struct kndShard *shard;
    worker_t **workers;
    worker_t *worker;
    int err;

    err = kndShard_new(&shard, shard_config, strlen(shard_config));
    ck_assert_int_eq(err, knd_OK);

    fprintf(stdout, ".. total num of workers: %zu\n", shard->num_workers);

    workers = calloc(sizeof(worker_t*), shard->num_workers);
    if (!workers) return;

    for (size_t i = 0; i < shard->num_workers; i++) {
        if ((worker = malloc(sizeof(worker_t))) == NULL) {
            perror("worker allocation failed");
            return;
        }
        memset(worker, 0, sizeof(*worker));
        worker->id = i;
        worker->shard = shard;
        workers[i] = worker;

        if (pthread_create(&worker->thread, NULL, worker_proc, (void*)worker)) {
            perror("worker thread creation failed");
            free(worker);
            return;
        }
    }

    for (size_t i = 0; i < shard->num_workers; i++) {
        worker = workers[i];
        pthread_join(worker->thread, NULL);
    }

    kndShard_del(shard);
}
END_TEST

int main(void) {
    Suite *s = suite_create("suite");

    TCase *tc_shard_concurrent = tcase_create("concurrent shard");
    tcase_add_test(tc_shard_concurrent, shard_concurrent_import_test);
    suite_add_tcase(s, tc_shard_concurrent);

    SRunner* sr = srunner_create(s);
    //srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr, CK_NORMAL);
    int num_failures = srunner_ntests_failed(sr);
    srunner_free(sr);

    return num_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
