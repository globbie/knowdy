#include <knd_parser.h>
#include <knd_task.h>

#include <check.h>

#include <string.h>

// --------------------------------------------------------------------------------
// User -- testable object
struct User { char name[KND_SHORT_NAME_SIZE]; size_t name_size; char sid[6]; size_t sid_size; };

// --------------------------------------------------------------------------------
// TaskSpecs -- for passing inner specs into |parse_user|
struct TaskSpecs { struct kndTaskSpec *specs; size_t num_specs; };

// --------------------------------------------------------------------------------
// Common routines
static void test_case_fixture_setup(void) {
    extern struct User user;
    user.name_size = 0;
    user.sid_size = 0;
}

static int parse_user(void *self, const char *rec, size_t *total_size) {
    struct TaskSpecs *args = (struct TaskSpecs *)self;
    return knd_parse_task(rec, total_size, args->specs, args->num_specs);
}

static int run_set_name(void *obj, struct kndTaskArg *args, size_t num_args) {
    struct User *user = (struct User *)obj;
    ck_assert(args); ck_assert_uint_eq(num_args, 1);
    ck_assert_uint_eq(args[0].name_size, strlen("_impl")); ck_assert_str_eq(args[0].name, "_impl");
    ck_assert_uint_ne(args[0].val_size, 0);
    if (args[0].val_size > sizeof user->name)
        return knd_LIMIT;
    memcpy(user->name, args[0].val, args[0].val_size);
    user->name_size = args[0].val_size;
    return knd_OK;
}

// --------------------------------------------------------------------------------
// Common variables
int rc;
const char *rec;
size_t total_size;
struct User user;  // Used only to specify predefined kndTaskSpec(s)
const struct kndTaskSpec
    name_spec = { .is_implied = true, .run = run_set_name, .obj = &user },
    sid_spec = { .name = "sid", .name_size = sizeof "sid" - 1, .buf = user.sid, .buf_size = &user.sid_size, .max_buf_size = sizeof user.sid };


START_TEST(parse_task_empty)
    struct kndTaskSpec inner_specs[] = { name_spec, sid_spec };
    struct TaskSpecs parse_args = { inner_specs, sizeof inner_specs / sizeof inner_specs[0] };
    struct kndTaskSpec specs[] = {{ .name = "user", .name_size = strlen("user"), .parse = parse_user, .obj = &parse_args }};

    rc = knd_parse_task(rec = "", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ck_assert_uint_eq(user.name_size, 0); ck_assert_uint_eq(user.sid_size, 0);
END_TEST

START_TEST(parse_task_empty_with_spaces)
    struct kndTaskSpec inner_specs[] = { name_spec, sid_spec };
    struct TaskSpecs parse_args = { inner_specs, sizeof inner_specs / sizeof inner_specs[0] };
    struct kndTaskSpec specs[] = {{ .name = "user", .name_size = strlen("user"), .parse = parse_user, .obj = &parse_args }};

    rc = knd_parse_task(rec = "     ", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ck_assert_uint_eq(user.name_size, 0); ck_assert_uint_eq(user.sid_size, 0);
END_TEST

START_TEST(parse_task_empty_with_closing_brace)
    struct kndTaskSpec inner_specs[] = { name_spec, sid_spec };
    struct TaskSpecs parse_args = { inner_specs, sizeof inner_specs / sizeof inner_specs[0] };
    struct kndTaskSpec specs[] = {{ .name = "user", .name_size = strlen("user"), .parse = parse_user, .obj = &parse_args }};

    rc = knd_parse_task(rec = " }     ", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strchr(rec, '}') - rec);  // shared brace
END_TEST

START_TEST(parse_implied_field_with_spaces)
    struct kndTaskSpec inner_specs[] = { name_spec };
    struct TaskSpecs parse_args = { inner_specs, sizeof inner_specs / sizeof inner_specs[0] };
    struct kndTaskSpec specs[] = {{ .name = "user", .name_size = strlen("user"), .parse = parse_user, .obj = &parse_args }};

    rc = knd_parse_task(rec = "{user John Smith}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ck_assert_uint_eq(user.name_size, strlen("John Smith")); ck_assert_str_eq(user.name, "John Smith");

    rc = knd_parse_task(rec = "{user  John Space }", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ck_assert_uint_eq(user.name_size, strlen("John Space")); ck_assert_str_eq(user.name, "John Space");
END_TEST

START_TEST(parse_implied_field_max_size)
    struct kndTaskSpec inner_specs[] = { name_spec };
    struct TaskSpecs parse_args = { inner_specs, sizeof inner_specs / sizeof inner_specs[0] };
    struct kndTaskSpec specs[] = {{ .name = "user", .name_size = strlen("user"), .parse = parse_user, .obj = &parse_args }};
    const char buf[] = { '{', 'u', 's', 'e', 'r', ' ', [6 ... KND_SHORT_NAME_SIZE + 5] = 'a', [KND_SHORT_NAME_SIZE + 6] = '}', [KND_SHORT_NAME_SIZE + 7] = '\0' };

    rc = knd_parse_task(rec = buf, &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ck_assert_uint_eq(user.name_size, KND_SHORT_NAME_SIZE); ck_assert(!memcmp(user.name, strchr(buf, 'a'), user.name_size));
END_TEST

START_TEST(parse_implied_field_max_size_plus_one)
    struct kndTaskSpec inner_specs[] = { name_spec };
    struct TaskSpecs parse_args = { inner_specs, sizeof inner_specs / sizeof inner_specs[0] };
    struct kndTaskSpec specs[] = {{ .name = "user", .name_size = strlen("user"), .parse = parse_user, .obj = &parse_args }};
    const char buf[] = { '{', 'u', 's', 'e', 'r', ' ', [6 ... KND_SHORT_NAME_SIZE + 6] = 'a', [KND_SHORT_NAME_SIZE + 7] = '}', [KND_SHORT_NAME_SIZE + 8] = '\0' };

    rc = knd_parse_task(rec = buf, &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_LIMIT);  // defined in run_set_name()
END_TEST

START_TEST(parse_implied_field_size_NAME_SIZE_plus_one)
    struct kndTaskSpec inner_specs[] = { name_spec };
    struct TaskSpecs parse_args = { inner_specs, sizeof inner_specs / sizeof inner_specs[0] };
    struct kndTaskSpec specs[] = {{ .name = "user", .name_size = strlen("user"), .parse = parse_user, .obj = &parse_args }};
    const char buf[] = { '{', 'u', 's', 'e', 'r', ' ', [6 ... KND_NAME_SIZE + 6] = 'a', [KND_NAME_SIZE + 7] = '}', [KND_NAME_SIZE + 8] = '\0' };

    rc = knd_parse_task(rec = buf, &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_LIMIT);
END_TEST

START_TEST(parse_tag_empty)
    struct kndTaskSpec inner_specs[] = { sid_spec };
    struct TaskSpecs parse_args = { inner_specs, sizeof inner_specs / sizeof inner_specs[0] };
    struct kndTaskSpec specs[] = {{ .name = "user", .name_size = strlen("user"), .parse = parse_user, .obj = &parse_args }};

    rc = knd_parse_task(rec = "{user{}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);

    rc = knd_parse_task(rec = "{user {}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);

    rc = knd_parse_task(rec = "{user{ 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);

    rc = knd_parse_task(rec = "{user { 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);
END_TEST

START_TEST(parse_tag_empty_with_spaces)
    struct kndTaskSpec inner_specs[] = { sid_spec };
    struct TaskSpecs parse_args = { inner_specs, sizeof inner_specs / sizeof inner_specs[0] };
    struct kndTaskSpec specs[] = {{ .name = "user", .name_size = strlen("user"), .parse = parse_user, .obj = &parse_args }};

    rc = knd_parse_task(rec = "{user{     }}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);

    rc = knd_parse_task(rec = "{user {     }}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);
END_TEST

START_TEST(parse_tag_unknown)
    struct kndTaskSpec inner_specs[] = { sid_spec };
    struct TaskSpecs parse_args = { inner_specs, sizeof inner_specs / sizeof inner_specs[0] };
    struct kndTaskSpec specs[] = {{ .name = "user", .name_size = strlen("user"), .parse = parse_user, .obj = &parse_args }};

    rc = knd_parse_task(rec = "{user{s 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_NO_MATCH);

    rc = knd_parse_task(rec = "{user {s 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_NO_MATCH);

    rc = knd_parse_task(rec = "{user{si 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_NO_MATCH);

    rc = knd_parse_task(rec = "{user {si 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_NO_MATCH);

    rc = knd_parse_task(rec = "{user{sid 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);

    rc = knd_parse_task(rec = "{user {sid 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);

    rc = knd_parse_task(rec = "{user{sido 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_NO_MATCH);

    rc = knd_parse_task(rec = "{user {sido 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_NO_MATCH);
END_TEST

START_TEST(parse_value_terminal_empty)
    struct kndTaskSpec inner_specs[] = { sid_spec };
    struct TaskSpecs parse_args = { inner_specs, sizeof inner_specs / sizeof inner_specs[0] };
    struct kndTaskSpec specs[] = {{ .name = "user", .name_size = strlen("user"), .parse = parse_user, .obj = &parse_args }};

    rc = knd_parse_task(rec = "{user{sid}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);  // TODO(ki.stfu): Call the default handler

    rc = knd_parse_task(rec = "{user {sid}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);  // TODO(ki.stfu): Call the default handler
END_TEST

START_TEST(parse_value_terminal_empty_with_spaces)
    struct kndTaskSpec inner_specs[] = { sid_spec };
    struct TaskSpecs parse_args = { inner_specs, sizeof inner_specs / sizeof inner_specs[0] };
    struct kndTaskSpec specs[] = {{ .name = "user", .name_size = strlen("user"), .parse = parse_user, .obj = &parse_args }};

    rc = knd_parse_task(rec = "{user{sid   }}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);

    rc = knd_parse_task(rec = "{user {sid   }}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);
END_TEST

START_TEST(parse_value_terminal_max_size)
    struct kndTaskSpec inner_specs[] = { sid_spec };
    struct TaskSpecs parse_args = { inner_specs, sizeof inner_specs / sizeof inner_specs[0] };
    struct kndTaskSpec specs[] = {{ .name = "user", .name_size = strlen("user"), .parse = parse_user, .obj = &parse_args }};

    rc = knd_parse_task(rec = "{user{sid 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ck_assert_uint_eq(user.sid_size, strlen("123456")); ck_assert_str_eq(user.sid, "123456");

    rc = knd_parse_task(rec = "{user {sid 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ck_assert_uint_eq(user.sid_size, strlen("123456")); ck_assert_str_eq(user.sid, "123456");
END_TEST

START_TEST(parse_value_terminal_max_size_plus_one)
    struct kndTaskSpec inner_specs[] = { sid_spec };
    struct TaskSpecs parse_args = { inner_specs, sizeof inner_specs / sizeof inner_specs[0] };
    struct kndTaskSpec specs[] = {{ .name = "user", .name_size = strlen("user"), .parse = parse_user, .obj = &parse_args }};

    rc = knd_parse_task(rec = "{user{sid 1234567}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_LIMIT);

    rc = knd_parse_task(rec = "{user {sid 1234567}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_LIMIT);
END_TEST

START_TEST(parse_value_terminal_with_braces)
    struct kndTaskSpec inner_specs[] = { sid_spec };
    struct TaskSpecs parse_args = { inner_specs, sizeof inner_specs / sizeof inner_specs[0] };
    struct kndTaskSpec specs[] = {{ .name = "user", .name_size = strlen("user"), .parse = parse_user, .obj = &parse_args }};

    rc = knd_parse_task(rec = "{user{sid {123456}}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);

    rc = knd_parse_task(rec = "{user {sid {123456}}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);

    rc = knd_parse_task(rec = "{user{sid{123456}}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);

    rc = knd_parse_task(rec = "{user {sid{123456}}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);

    rc = knd_parse_task(rec = "{user{sid 123{456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);

    rc = knd_parse_task(rec = "{user {sid 123{456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);
END_TEST


int main() {
    TCase* tc = tcase_create("all cases");
    tcase_add_checked_fixture(tc, test_case_fixture_setup, NULL);
    tcase_add_test(tc, parse_task_empty);
    tcase_add_test(tc, parse_task_empty_with_spaces);
    tcase_add_test(tc, parse_task_empty_with_closing_brace);
    tcase_add_test(tc, parse_implied_field_with_spaces);
    tcase_add_test(tc, parse_implied_field_max_size);
    tcase_add_test(tc, parse_implied_field_max_size_plus_one);
    tcase_add_test(tc, parse_implied_field_size_NAME_SIZE_plus_one);
    tcase_add_test(tc, parse_tag_empty);
    tcase_add_test(tc, parse_tag_empty_with_spaces);
    tcase_add_test(tc, parse_tag_unknown);
    tcase_add_test(tc, parse_value_terminal_empty);
    tcase_add_test(tc, parse_value_terminal_empty_with_spaces);
    tcase_add_test(tc, parse_value_terminal_max_size);
    tcase_add_test(tc, parse_value_terminal_max_size_plus_one);
    tcase_add_test(tc, parse_value_terminal_with_braces);

    Suite* s = suite_create("suite");
    suite_add_tcase(s, tc);
    SRunner* sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    srunner_free(sr);
    return 0;
}
