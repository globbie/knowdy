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

START_TEST(shard_concurrent_service_test)
{
    struct kndShard *shard;
    int err;

    err = knd_shard_new(&shard, shard_config, strlen(shard_config));
    ck_assert_int_eq(err, knd_OK);

    err = knd_shard_serve(shard);
    ck_assert_int_eq(err, knd_OK);

    err = knd_shard_stop(shard);
    ck_assert_int_eq(err, knd_OK);
    
    knd_shard_del(shard);
}
END_TEST

int main(void) {
    Suite *s = suite_create("suite");

    TCase *tc_shard_concurrent = tcase_create("concurrent shard");
    tcase_set_timeout(tc_shard_concurrent, 100);

    tcase_add_test(tc_shard_concurrent, shard_concurrent_service_test);
    
    suite_add_tcase(s, tc_shard_concurrent);

    SRunner* sr = srunner_create(s);
    //srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr, CK_NORMAL);
    int num_failures = srunner_ntests_failed(sr);
    srunner_free(sr);

    return num_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
