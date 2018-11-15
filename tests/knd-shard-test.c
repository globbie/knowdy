#include <knd_shard.h>

#include <check.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// todo(n.rodionov): make paths configurable
static const char *shard_config =
"{schema knd"
"  {agent 007}"
"  {db-path .}"
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

START_TEST(shard_config_test)
    static const struct table_test broken_configs[] = {
        {
            .input = "{schema knd}",
            .expect = NULL,
            .err = knd_FAIL
        }
        //{
        //    .input = NULL
        //}
    };

    struct kndShard *shard;
    int err = 0;

    for (size_t i = 0; i < sizeof broken_configs / sizeof broken_configs[0]; ++i) {
        const struct table_test *config = &broken_configs[i];
        shard = NULL;
        err = kndShard_new(&shard, config->input, strlen(config->input));
        ck_assert_int_eq(err, config->err);
        if (shard)
            kndShard_del(shard);
    }

END_TEST


START_TEST(shard_table_test)
    static const struct table_test cases[] = {
        {
            .input = "{task {class Person}}",
            .expect = "{\"err\":\"Person class name not found\",\"http_code\":404}"
        },
        //{
        //    .input = "{task {!class Person {num age}}}",
        //    .expect = "{\"result\":\"OK\"}"
        //},
        {
            .input = "{task {!class Person}}",
            .expect = "{\"err\":\"Person class name already exists\",\"http_code\":409}"
        },
//        {
//            .input = "{task {!class Worker {is PersonUnknown}}}",
//            .expect = "{\"err\":\"internal server error\",\"http_code\":404}"  // TODO(k15tfu): fix this
//        },
        //{
        //    .input = "{task {!class Worker {is Person}}}",
        //    .expect = "{\"result\":\"OK\"}"
        //},
        //{
        //    .input = "{task {class User {!inst Vasya}}}",
        //    .expect = "{\"result\":\"OK\"}"
        //},
        // get class
        {
            .input = "{task {class Person}}",
            .expect = "{class Person{_id 2} {_repo /}{_state 1{phase new}}[attrs{num age}]{_subclasses {total 1} {num_terminals 1}[batch{Worker {_id 3}}]}}"
        },
// FIXME(k15tfu): _id doesn't work
//        {
//            .input = "{task {class {_id 2}}}",
//            .expect = "??"
//        },
        // get class attribute
// FIXME(k15tfu): no attr to present
//        {
//            .input = "{task {class Person {age}}}",
//            .expect = "??"
//        },
// FIXME(k15tfu): no such attribute
//        {
//            .input = "{task {class Person {_desc}}}",
//            .expect = "??"
//        },
// FIXME(k15tfu): _id doesn't work
//        {
//            .input = "{task {class {_id 2} {age}}}",
//            .expect = "??"
//        },
//        {
//            .input = "{task {class {_id 2} {_desc}}}",
//            .expect = "??"
//        },
        // get the latest state
        {
            .input = "{task {class Person {_state}}}",
            .expect = "{\"_state\":1,\"_phase\":\"new\",\"descendants\":{\"_state\":1,\"total\":1}}"
        },
// FIXME(k15tfu): _id doesn't work
//        {
//            .input = "{task {class {_id 2} {_state}}}",
//            .expect = "??"
//        },
// FIXME(k15tfu): class parse failure
//        {
//            .input = "{task {class Person {age {_state}}}}",
//            .expect = "??"
//        },
// FIXME(k15tfu): no such attribute
//        {
//            .input = "{task {class Person {_desc {_state}}}}",
//            .expect = "??"
//        },
// FIXME(k15tfu): _id doesn't work
//        {
//            .input = "{task {class {_id 2} {age {_state}}}}",
//            .expect = "??"
//        },
//        {
//            .input = "{task {class {_id 2} {_desc {_state}}}}",
//            .expect = "??"
//        },
        // get list of all states
// FIXME(k15tfu): fix them:
//        {
//            .input = "{task {class Person {_state {gt 0}}}}",
//            .expect = "??"
//        },
//        {
//            .input = "{task {class {_id 2} {_state {gt 0}}}}",
//            .expect = "??"
//        },
//        {
//            .input = "{task {class Person {age {_state {gt 0}}}}}",
//            .expect = "??"
//        },
//        {
//            .input = "{task {class Person {_desc {_state {gt 0}}}}}",
//            .expect = "??"
//        },
//        {
//            .input = "{task {class {_id 2} {age {_state {gt 0}}}}}",
//            .expect = "??"
//        },
//        {
//            .input = "{task {class {_id 2} {_desc {_state {gt 0}}}}}",
//            .expect = "??"
//        },
    };

    struct kndShard *shard;
    int err;

    err = kndShard_new(&shard, shard_config, strlen(shard_config));
    ck_assert_int_eq(err, knd_OK);
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        const struct table_test *pcase = &cases[i];

        const char *result; size_t result_size;
        fprintf(stdout, "Checking #%zu: %s...\n", i, pcase->input);
        err = kndShard_run_task(shard, pcase->input, strlen(pcase->input), &result, &result_size, 0);
        ck_assert_int_eq(err, knd_OK);
        ASSERT_STR_EQ(result, result_size, pcase->expect, -1);
    }

    kndShard_del(shard);
END_TEST

int main(void) {
    Suite *s = suite_create("suite");

    TCase *tc_shard_basic = tcase_create("basic shard");
    tcase_add_test(tc_shard_basic, shard_config_test);
    tcase_add_test(tc_shard_basic, shard_table_test);
    suite_add_tcase(s, tc_shard_basic);

    SRunner* sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr, CK_NORMAL);
    int num_failures = srunner_ntests_failed(sr);
    srunner_free(sr);

    return num_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
