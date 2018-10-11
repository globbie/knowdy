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

START_TEST(shard_table_test)
    struct kndShard *shard = NULL;
    int err;

    err = kndShard_new(&shard, shard_config, strlen(shard_config));
    ck_assert_int_eq(err, knd_OK);

    const char *input = "{task {class User {!inst Vasya}}}";
    size_t input_len = strlen(input);

    const char *output = NULL;
    size_t output_len = 0;

    const char *expect = "{\"result\":\"OK\"}";
    size_t expect_len = strlen(expect);

    err = kndShard_run_task(shard, input, input_len, &output, &output_len);
    ck_assert_int_eq(err, knd_OK);

    ck_assert_uint_eq(output_len, expect_len);
    int ret = strncmp(output, expect, expect_len);
    ck_assert_int_eq(ret, 0);

    kndShard_del(shard);
END_TEST

int main(void) {
    Suite *s = suite_create("suite");

    TCase *tc_shard_basic = tcase_create("basic shard");
    tcase_add_test(tc_shard_basic, shard_table_test);
    suite_add_tcase(s, tc_shard_basic);

    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);

    int number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    if (number_failed != 0){
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

