#include <knd_shard.h>

#include <check.h>

#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OUTPUT_BUF_SIZE 1024 * 10

// todo(n.rodionov): make paths configurable
static const char *shard_config =
"{schema knd"
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

static const char *shard_inheritance_config =
"{schema knd"
"  {db-path .}"
"  {schema-path ../../tests/schemas/food"
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

static const char *shard_proc_config =
"{schema knd"
"  {db-path .}"
"  {schema-path ../../tests/schemas/proc"
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
        },
    };

    struct kndShard *shard;
    int err = 0;

    for (size_t i = 0; i < sizeof broken_configs / sizeof broken_configs[0]; ++i) {
        const struct table_test *config = &broken_configs[i];
        shard = NULL;
        err = knd_shard_new(&shard, config->input, strlen(config->input));
        ck_assert_int_eq(err, config->err);

        if (shard)
            knd_shard_del(shard);
    }

END_TEST


START_TEST(shard_table_test)
    static const struct table_test cases[] = {
        {   /* display all available classes */
            .input = "{task {class}}",
            .expect = "{set{total 1\\[class{User{_id 1}\\[attr{str guid}{str first-name}]}]{batch{max 10}{size 1}{from 0}}}"
        },
        {   /* get class by name */
            .input = "{task {class User}}",
            .expect = "{class User{_id 1}\\[attr{str guid}{str first-name}]}"
        },
        {   /* get class by numeric id */
            .input = "{task {class {_id 1}}}",
            .expect = "{class User{_id 1}\\[attr{str guid}{str first-name}]}"
        },
#if 0
        {   /* get the latest valid class state */
            .input = "{task {class User {_state}}}",
            .expect = "{state [0-9]*{time [0-9]*}}"
        },
        {    /* get some specific state */
            .input = "{task {class User {_state 42}}}",
            .expect = "not implemented: filter class state"
        },
        {    /* get states prior to a specific one */
            .input = "{task {class User {_state {lt 42}}}}",
            .expect = "not implemented: filter class state"
        },
        {
            .input = "{task {class User {_desc}}}",
            .expect = "not implemented: export empty class desc"
        },
        {
            .input = "{task {class User {_desc {_state}}}}",
            .expect = "not implemented: export class desc state"
        },
        {
            .input = "{task {class User {_desc {_state 123456}}}}",
            .expect = "not implemented: filter class desc state"
        },
        {
            .input = "{task {class User {_desc {_state {lt 123456}}}}}",
            .expect = "not implemented: filter class desc state"
        },
        {
            .input = "{task {class User {guid}}}",
            .expect = "{str guid}"
        },
        {
            .input = "{task {class User {guid {_state}}}}",
            .expect = "{state [0-9]*{time [0-9]*}}"
        },
        {
            .input = "{task {class User {guid {_state 123456}}}}",
            .expect = ".*"  // FIXME(k15tfu)
        },
        {
            .input = "{task {class User {guid {_state {lt 123456}}}}}",
            .expect = ".*"  // FIXME(k15tfu)
        },
        {
            .input = "{task {class Person}}",
            .expect = "{err 404{gloss Person class name not found}}"
        },
        {
            .input = "{task {!class Person {num age}}}",
            .expect = "{state [0-9]*{modif [0-9]*}}"
        },
        {
            .input = "{task {!class Person}}",
            .expect = "{err 409{gloss Person class name already exists}}"
        },
        {
            .input = "{task {!class Worker {is PersonUnknown}}}",
            .expect = "{err 404{gloss PersonUnknown class name not found}}"
        },
        {
            .input = "{task {!class Worker {is Person}}}",
            .expect = "{state [0-9]*{time [0-9]*}}"
        },
        {
            .input = "{task {class User {!inst Alice}}}",
            .expect = "{state [0-9]*{time [0-9]*}}"
        },
        {
            .input = "{task {class User {!inst {first-name Bob} {guid 4e99a114-d1eb-4ead-aa36-f5d3e825e311}}}}",
            .expect = "{state [0-9]*{time [0-9]*}}"
        },
        {
            .input = "{task {class User {!inst {unknown-field some value}}}}",
            .expect = "{err 500{_gloss unclassified server error}}"
        },
        {
            .input = "{task {class User {inst Alice}}}",
            .expect = "{\"_name\":\"Alice\",\"_id\":1,\"_state\":1,\"_class\":\"User\"}"
        },
        {
            .input = "{task {class User {inst {_id 2}}}}",
            .expect = "{\"_name\":\"2\",\"_id\":2,\"_state\":1,\"_phase\":\"new\",\"_class\":\"User\",\"first-name\":\"Bob\",\"guid\":\"4e99a114-d1eb-4ead-aa36-f5d3e825e311\"}"
        },
        // get class
        {
            .input = "{task {class Person}}",
            .expect = "{class Person{_id 2}{_repo /}{_state 1{phase new}}\\[attrs{num age}]{_subclasses {total 1} {num_terminals 1}\\[batch{Worker{_id 4}}]}}"
        },
        {
            .input = "{task {class {_id 2}}}",
            .expect = ".*"  // FIXME(k15tfu): Person is not in kndRepo.class_idx
        },
        // get class attribute
        {
            .input = "{task {class Person {age}}}",
            .expect = "{num age}"
        },
        //{
        //    .input = "{task {class Person {_desc}}}",
        //    .expect = "not implemented: export empty class desc"
        //},
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
            .expect = "{state [0-9]*{time [0-9]*}}"
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
        {
            .input = "{task {class Person {_desc {_state}}}}",
            .expect = "not implemented: export class desc state"
        },
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
        {
            .input = "{task {class Person {_state {gt 123456}}}}",
            .expect = "not implemented: filter class state"
        },
//        {
//            .input = "{task {class {_id 2} {_state {gt 123456}}}}",
//            .expect = "??"
//        },
//        {
//            .input = "{task {class Person {age {_state {gt 0}}}}}",
//            .expect = "??"
//        },
        {
            .input = "{task {class Person {_desc {_state {gt 123456}}}}}",
            .expect = "not implemented: filter class desc state"
        },
//        {
//            .input = "{task {class {_id 2} {age {_state {gt 0}}}}}",
//            .expect = "??"
//        },
//        {
//            .input = "{task {class {_id 2} {_desc {_state {gt 0}}}}}",
//            .expect = "??"
//        },
        /**
         **  class remove
         **/
        /*{
            .input  = "{task {user Alice {class Banana {!_rm}}}}",
            .expect = "not implemented: remove class"
        },
        {
            .input  = "{task {user Alice {class Banana {!_rm WRONG_FORMAT}}}}",
            .expect = "internal server error"  // FIXME(k15tfu)
        }*/
#endif
        };

    struct kndShard *shard;
    int err;

    err = knd_shard_new(&shard, shard_config, strlen(shard_config));
    ck_assert_int_eq(err, knd_OK);

    err = knd_shard_serve(shard);
    ck_assert_int_eq(err, knd_OK);

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        const struct table_test *pcase = &cases[i];

        fprintf(stdout, ".. checking input #%zu: %s...\n", i, pcase->input);

        char result[OUTPUT_BUF_SIZE + 1] = { 0 };
        size_t result_size = OUTPUT_BUF_SIZE;

        err = knd_shard_run_task(shard, pcase->input, strlen(pcase->input),
                                 result, &result_size);
        ck_assert_int_eq(err, knd_OK);
        *((char*)result + result_size) = '\0';

        regex_t reg;
        ck_assert(0 == regcomp(&reg, pcase->expect, 0));
        if (0 != regexec(&reg, result, 0, NULL, 0)) {
            ck_abort_msg("Assertion failed: \"%.*s\" doesn't match \"%s\"",
                         (int)result_size, result, pcase->expect);
        }
        regfree(&reg);
    }
    err = knd_shard_stop(shard);
    ck_assert_int_eq(err, knd_OK);

    knd_shard_del(shard);
END_TEST


/** CLASS INHERITANCE **/

START_TEST(shard_inheritance_test)
    static const struct table_test cases[] = {
        {   /* empty result set */
            .input  = "{task {class {_is Banana}}}",
            .expect = "{set{_is Banana}{total 0}}"
        },
        /* check an immediate parent */
        /*{  
            .input  = "{task {class {_is Fruit}}}",
            .expect =
                  "{set{is Fruit}{total 3}\\[batch"
                  "{class Banana{_id [0-9]*}{_repo /}\\[is{Fruit{_id [0-9]*}"
                              "\\[nutr{{source{class USDA{_id [0-9]*}{_repo /}}{energy 89}}]}]}"
                  "{class Apple{_id [0-9]*}{_repo /}\\[is{Fruit{_id [0-9]*}"
                              "\\[nutr{{source{class USDA{_id [0-9]*}{_repo /}}{energy 52}}]}]}"
                  "{class Orange{_id [0-9]*}{_repo /}\\[is{Fruit{_id [0-9]*}"
                              "\\[nutr{{source{class USDA{_id [0-9]*}{_repo /}}{energy 47}}]}]}]"
                  "{batch {max 10}{size 3}{from 0}}"
                  },*/
        /* check a distant ancestor (grandparent) */
        /*{   .input  = "{task {class {_is Edible Object}}}",
            .expect = "test"
            },*/
        {
            .input  = "{task {class {_is Dish {cuisine American Cuisine}}}}",
            .expect = "{set{_is Dish}{total 1\\[class{Apple Pie{_id 16}\\[is{Dish{_id 17}\\[ingr{{product{class Apple{_id 20}}{quant 5}}{{product{class Flour{_id 21}}{quant 200}}{{product{class Butter{_id 22}}{quant 100}}]\\[cuisine{{class American Cuisine{_id 24}}}]}]}]{batch{max 10}{size 1}{from 0}}}"
        },
        /*{
            .input  = "{task {class {_is Dish {ingr {product Milk}}}}}",
            .expect = "not implemented: filter baseclass attribute"
        },*/
        /*{
            .input  = "{task {class {_is Fruit {nutr}}}}",
            .expect = "not implemented: filter baseclass attribute"
        }*/
    };

    struct kndShard *shard;
    int err;

    err = knd_shard_new(&shard, shard_inheritance_config, strlen(shard_inheritance_config));
    ck_assert_int_eq(err, knd_OK);

    err = knd_shard_serve(shard);
    ck_assert_int_eq(err, knd_OK);

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        const struct table_test *pcase = &cases[i];
        fprintf(stdout, "Checking #%zu: %s...\n", i, pcase->input);

        char result[OUTPUT_BUF_SIZE + 1] = { 0 };
        size_t result_size = OUTPUT_BUF_SIZE;

        err = knd_shard_run_task(shard,
                                 pcase->input, strlen(pcase->input),
                                 result, &result_size);
        ck_assert_int_eq(err, knd_OK);
        *((char*)result + result_size) = '\0';

        regex_t reg;
        ck_assert(0 == regcomp(&reg, pcase->expect, 0));
        if (0 != regexec(&reg, result, 0, NULL, 0)) {
            ck_abort_msg("Assertion failed: \"%.*s\" doesn't match \"%s\"",
                         (int)result_size, result, pcase->expect);
        }
        regfree(&reg);
    }
    err = knd_shard_stop(shard);
    ck_assert_int_eq(err, knd_OK);
    knd_shard_del(shard);
END_TEST

/** PROC **/
START_TEST(shard_proc_test)
    static const struct table_test cases[] = {
        {   /* create a new proc */
            .input  = "{task {!proc test Process}}",
            .expect = "{state [0-9]*{time [0-9]*}}"
        },
        {   /* try to import the same proc */
            .input = "{task {!proc test Process}}",
            .expect = "{err 409{gloss test Process proc name already exists}}"
        },
        {   /* remove proc */
            .input = "{task {proc test Process {!_rm}}}",
            .expect = "{state [0-9]*{time [0-9]*}}",
        },
#if 0
        {   /* create a proc once more */
            .input  = "{task {!proc test Process}}",
            .expect = "{state [0-9]*{time [0-9]*}}",
        },
        {   /* create a proc with glosses */
            .input  = "{task {!proc another test Process"
                      "[_gloss {en {t gloss in English}}"
                      "{ru {t пояснение по-русски}}]}}",
            .expect = "{state [0-9]*{modif [0-9]*}}"
        },
        {   /* proc with a base */
            .input  = "{task {!proc press {is Physical Impact Process}}}",
            .expect = "{state [0-9]*{modif [0-9]*}}"
        },
        {   /* proc with args */
            .input  = "{task {!proc wash {is Physical Impact Process}"
                      "[arg {instr {_c Physical Object}}]}}",
            .expect = "{state [0-9]*{modif [0-9]*}}"
        },
        {   /* add an agent */
            .input = "{task {class Person {!inst Alice}}}",
            .expect = "{state [0-9]*{modif [0-9]*}}"
        },
        {   /* add an object */
            .input = "{task {class Window {!inst kitchen window}}}",
            .expect = "{state [0-9]*{modif [0-9]*}}"
        },
        {   /* register a proc inst */
            .input = "{task {proc wash {!inst Alice-to-wash-a-window"
                     "{agent Alice} {obj kitchen window} }}}",
            .expect = "{state [0-9]*{modif [0-9]*}}"
        },
        {   /* another proc inst */
            .input = "{task {proc wash {!inst Alice-to-wash-a-window-again"
                     "{agent Alice} {obj kitchen window} }}}",
            .expect = "{state [0-9]*{modif [0-9]*}}"
        },
        {   /* yet another proc inst */
            .input = "{task {proc wash {!inst Alice-to-wash-a-window-3"
                     "{agent Alice} {obj kitchen window} }}}",
            .expect = "{state [0-9]*{modif [0-9]*}}"
        }
#endif
    };
    struct kndShard *shard;
    int err;

    err = knd_shard_new(&shard, shard_proc_config, strlen(shard_proc_config));
    ck_assert_int_eq(err, knd_OK);

    err = knd_shard_serve(shard);
    ck_assert_int_eq(err, knd_OK);

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        const struct table_test *pcase = &cases[i];

        fprintf(stdout, ".. checking input #%zu: %s...\n", i, pcase->input);

        char result[OUTPUT_BUF_SIZE + 1] = { 0 };
        size_t result_size = OUTPUT_BUF_SIZE;

        err = knd_shard_run_task(shard,
                                 pcase->input, strlen(pcase->input),
                                 result, &result_size);
        ck_assert_int_eq(err, knd_OK);
        *((char*)result + result_size) = '\0';

        regex_t reg;
        ck_assert(0 == regcomp(&reg, pcase->expect, 0));
        if (0 != regexec(&reg, result, 0, NULL, 0)) {
            ck_abort_msg("Assertion failed: \"%.*s\" doesn't match \"%s\"",
                         (int)result_size, result, pcase->expect);
        }
        regfree(&reg);
    }
    err = knd_shard_stop(shard);
    ck_assert_int_eq(err, knd_OK);

    knd_shard_del(shard);
END_TEST

int main(void)
{
    Suite *s = suite_create("suite");

    TCase *tc_shard_basic = tcase_create("basic shard");
    tcase_add_test(tc_shard_basic, shard_config_test);
    tcase_add_test(tc_shard_basic, shard_table_test);
    tcase_add_test(tc_shard_basic, shard_inheritance_test);
    tcase_add_test(tc_shard_basic, shard_proc_test);
    suite_add_tcase(s, tc_shard_basic);

    SRunner* sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr, CK_NORMAL);
    int num_failures = srunner_ntests_failed(sr);
    srunner_free(sr);

    return num_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
