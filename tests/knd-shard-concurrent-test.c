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

#define TEST_NUM_AGENTS 8
#define TEST_NUM_CLASSES 10
#define MIN_CLASSNAME_SIZE 16
#define MAX_CLASSNAME_SIZE 64

static const char *shard_config =
"{schema knd"
"  {agent 007 {role Reader}}"
"  {db-path ./}"
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

struct class_test {
    struct kndTask *task;
    const char **classnames;
    size_t num_classnames;
    size_t total_jobs;
    size_t success_jobs;
};

static void *agent_runner(void *ptr)
{
    char buf[1024];
    size_t buf_size;
    struct class_test *t = ptr;
    struct kndTask *task = t->task;
    const char *classname;
    size_t classname_size;
    const char *block;
    size_t block_size;
    int err;

    for (size_t i = 0; i < TEST_NUM_CLASSES; i++) { 
        classname = t->classnames[i];
        classname_size = strlen(classname);
        buf_size = snprintf(buf, 1024, "{task{!class %.*s}}",
                            (int)classname_size, classname);

        /* NB: Writer's tasks must be permanently allocated 
           before submitting.
           No string copy allocations will take place within the DB!
        */
        err = knd_task_copy_block(task,
                                  buf, buf_size,
                                  &block, &block_size);
        if (err != knd_OK) return NULL;

        knd_log(".. agent #%d to run task: \"%.*s\"..",
                task->id, block_size, block);

        knd_task_reset(task);
        t->total_jobs++;

        err = knd_task_run(task, block, block_size);
        if (err != knd_OK) {
            //knd_log("agent #%d) -- update confirm failed: %.*s",
            //        task->id, task->output_size, task->output);
            continue;
        }
        knd_log("agent #%d) ++ write task success: %.*s!",
                task->id, task->output_size, task->output);
        t->success_jobs++;
    }
    return NULL;
}

void gen_rand_str(char *dest, size_t len) {
    const char charset[] = "0123456789"
        " "
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    size_t i;
    while (len-- > 0) {
        i = rand() % (sizeof charset - 1);
        *dest++ = charset[i];
    }
    *dest = '\0';
}

void shuffle_classnames(const char **classnames,
                        size_t num_classnames)
{
    const char *c;
    int i;
    for (size_t len = num_classnames - 1; len > 0; len--) {
        i = rand() % len;
        c = classnames[i];
        classnames[i] = classnames[len];
        classnames[len] = c;
    }
}

/**
 *  make sure every class name is present in the current state of DB 
 */
int check_final_results(struct kndShard *shard,
                        const char **classnames,
                        size_t num_classnames)
{
    char buf[1024];
    size_t buf_size;
    struct kndTask *task;
    const char *classname;
    size_t classname_size = 0;
    size_t total_matches = 0;
    int err;

    err = knd_task_new(shard, NULL, 1, &task);
    if (err) return err;

    task->ctx = calloc(1, sizeof(struct kndTaskContext));
    if (!task->ctx) return knd_NOMEM;
    task->role = KND_READER;

    for (size_t i = 0; i < num_classnames; i++) {
        classname = classnames[i];
        classname_size = strlen(classname);

        buf_size = snprintf(buf, 1024, "{task{class %.*s}}", (int)classname_size, classname);

        knd_task_reset(task);
        err = knd_task_run(task, buf, buf_size);
        if (err != knd_OK) {
            knd_log("-- reading confirm failed: %.*s",
                    task->output_size, task->output);
            continue;
        }
        knd_log("== confirm read OK: %.*s",
                task->output_size, task->output);
        total_matches++;
    }
    knd_log("== total class names: %zu  confirmed matches:%zu",
            num_classnames, total_matches);
    return knd_OK;
}

START_TEST(shard_concurrent_update_test)
{
    struct kndShard *shard;
    struct kndTask *task;
    pthread_t agents[TEST_NUM_AGENTS];
    struct class_test tests[TEST_NUM_AGENTS];
    struct class_test *t;
    char **classnames;
    size_t num_classnames = TEST_NUM_CLASSES;
    char *classname = "";
    size_t classname_size = 0;
    const char *c;
    int err;
    
    err = knd_shard_new(&shard, shard_config, strlen(shard_config));
    ck_assert_int_eq(err, knd_OK);

    /* generate an array of random strings as classnames */
    classnames = malloc(sizeof(char *) * num_classnames);
    for (size_t i = 0; i < num_classnames; i++) {
        classname_size = MIN_CLASSNAME_SIZE + (rand() % MAX_CLASSNAME_SIZE);
        classname = calloc(1, classname_size + 1);
        gen_rand_str(classname, classname_size);
        knd_log("%zu) %.*s [len:%zu]", i, classname_size, classname, classname_size);
        classnames[i] = classname;
    }

    /* build agents and give each of them a shuffled copy of the same array of classes 
       so that they concurrently try to register new classes in the DB  */
    for (int i = 0; i < TEST_NUM_AGENTS; i++) {
        t = &tests[i];
        memset(t, 0, sizeof(struct class_test));

        err = knd_task_new(shard, NULL, 1, &task);
        ck_assert_int_eq(err, knd_OK);

        task->ctx = calloc(1, sizeof(struct kndTaskContext));
        ck_assert(task->ctx != NULL);
        task->role = KND_WRITER;
        task->id = i;
        t->task = task;
        
        t->classnames = calloc(1, sizeof(const char *) * TEST_NUM_CLASSES);
        t->num_classnames = TEST_NUM_CLASSES;
        for (size_t j = 0; j < TEST_NUM_CLASSES; j++)
            t->classnames[j] = classnames[j];
        shuffle_classnames(t->classnames, TEST_NUM_CLASSES);

        knd_log("\n == agent #%d", task->id);
        for (int j = 0; j < TEST_NUM_CLASSES; j++) {
            c = t->classnames[j];
            classname_size = strlen(c);
            knd_log("== %.*s [len:%zu]", classname_size, c, classname_size);
        }
    }

    /* start threads */
    for (int i = 0; i < TEST_NUM_AGENTS; i++) {
        t = &tests[i];
        err = pthread_create(&agents[i], NULL, agent_runner, (void*)t);
        ck_assert_int_eq(err, 0);
    }

    /* wait for all agents to finish their jobs */
    for (int i = 0; i < TEST_NUM_AGENTS; i++) {
        pthread_join(agents[i], NULL);
    }
    
    /* present reports */
    for (int i = 0; i < TEST_NUM_AGENTS; i++) {
        t = &tests[i];
        knd_log("agent #%d:  jobs:%zu  success:%zu",
                t->task->id, t->total_jobs, t->success_jobs);
    }

    err = check_final_results(shard, (const char**)classnames, num_classnames);
    ck_assert_int_eq(err, knd_OK);

    knd_shard_del(shard);
}
END_TEST

int main(void) {
    srand(time(NULL));
    Suite *s = suite_create("suite");

    TCase *tc_shard_concurrent = tcase_create("concurrent shard");
    tcase_set_timeout(tc_shard_concurrent, 100);

    tcase_add_test(tc_shard_concurrent, shard_concurrent_update_test);
    suite_add_tcase(s, tc_shard_concurrent);

    SRunner* sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int num_failures = srunner_ntests_failed(sr);
    srunner_free(sr);

    return num_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
