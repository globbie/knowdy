#include <knd_shard.h>

#include <check.h>

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
        size_t __exp_size = ((exp_size) == -1 ? strlen(__exp) : (exp_size));                     \
        ck_assert_msg(__act_size == __exp_size && 0 == strncmp(__act, __exp, __act_size),        \
            "Assertion '%s' failed: %s == \"%.*s\" [len: %zu] but expected \"%.*s\" [len: %zu]", \
            #act" == "#exp, #act, __act_size, __act, __act_size, __exp_size, __exp, __exp_size); \
    } while (0)

static void check_run_task(struct kndShard* shard, const char *input, const char *expect) {
    const char *output;
    size_t output_len;
    int err = kndShard_run_task(shard, input, strlen(input), &output, &output_len);
    ck_assert_int_eq(err, knd_OK);
    ASSERT_STR_EQ(output, output_len, expect, -1);
}

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
        },
        //{
        //    .input = NULL
        //}
    };

    struct kndShard *shard = NULL;
    int err = 0;

    for (size_t i = 0; i < sizeof broken_configs / sizeof broken_configs[0]; ++i) {
        const struct table_test *config = &broken_configs[i];

        err = kndShard_new(&shard, config->input, strlen(config->input));
        ck_assert_int_eq(err, config->err);
    }
END_TEST

START_TEST(shard_table_test)
    struct kndShard *shard = NULL;
    int err;

    err = kndShard_new(&shard, shard_config, strlen(shard_config));
    ck_assert_int_eq(err, knd_OK);

    const char *input = "{task {class User {!inst Vasya}}}";
    const char *expect = "{\"result\":\"OK\"}";
    check_run_task(shard, input, expect);

    kndShard_del(shard);
END_TEST

//
// Class test cases:
//

// Global: Should be used only in class test cases.
int err;
struct kndShard *shard;
const char *input;
const char *output; size_t output_len;

static void tc_class_fixture_setup(void) {
    int err = kndShard_new(&shard, shard_config, strlen(shard_config));
    ck_assert_int_eq(err, knd_OK);
}

static void tc_class_fixture_teardown(void) { kndShard_del(shard); }

START_TEST(class_import)
    const char *input = "{task {!class NewClass}}";
    err = kndShard_run_task(shard, input, strlen(input), &output, &output_len);
    ck_assert_int_eq(err, knd_OK);
    ASSERT_STR_EQ(output, output_len, "{\"result\":\"OK\"}", -1);
END_TEST

START_TEST(class_import_if_already_exists)
    const char *input = "{task {!class NewClass}}";
    err = kndShard_run_task(shard, input, strlen(input), &output, &output_len);
    ck_assert_int_eq(err, knd_OK);
    ASSERT_STR_EQ(output, output_len, "{\"result\":\"OK\"}", -1);

    err = kndShard_run_task(shard, input, strlen(input), &output, &output_len);
    ck_assert_int_eq(err, knd_OK);
    ASSERT_STR_EQ(output, output_len, "{\"err\":\"NewClass class name already exists\",\"http_code\":409}", -1);
END_TEST

START_TEST(class_import_if_already_exists_system)
    const char *input = "{task {!class User}}";
    err = kndShard_run_task(shard, input, strlen(input), &output, &output_len);
    ck_assert_int_eq(err, knd_OK);
    ASSERT_STR_EQ(output, output_len, "{\"result\":\"OK\"}", -1);
END_TEST

//
// Class test cases: The End.
//

int main(void) {
    Suite *s = suite_create("suite");

    TCase *tc_shard_basic = tcase_create("basic shard");
    tcase_add_test(tc_shard_basic, shard_config_test);
    tcase_add_test(tc_shard_basic, shard_table_test);
    suite_add_tcase(s, tc_shard_basic);

    TCase *tc_class = tcase_create("class");
    tcase_add_checked_fixture(tc_class, tc_class_fixture_setup, tc_class_fixture_teardown);
    tcase_add_test(tc_class, class_import);
    tcase_add_test(tc_class, class_import_if_already_exists);
    tcase_add_test(tc_class, class_import_if_already_exists_system);
    suite_add_tcase(s, tc_class);

    SRunner* sr = srunner_create(s);
    //srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr, CK_NORMAL);
    int num_failures = srunner_ntests_failed(sr);
    srunner_free(sr);

    // List of expected failures:
    // * class:class_import_if_already_exists_system
    const int num_expected_failures = 1;

    return num_failures == num_expected_failures ? EXIT_SUCCESS : EXIT_FAILURE;
}
