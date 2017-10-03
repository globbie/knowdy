#include <knd_parser.h>
#include <knd_task.h>

#include <check.h>

#include <string.h>


// Common variables
int rc;
const char *rec;
size_t total_size;
struct { char name[KND_SHORT_NAME_SIZE]; size_t name_size; char sid[6]; size_t sid_size; } user;


// Common routines
static void setup(void) {
    user.name_size = 0;
    user.sid_size = 0;
}

static int run_set_name(void *obj, struct kndTaskArg *args, size_t num_args) {
    ck_assert(&user == obj);
    ck_assert(args && num_args == 1);
    ck_assert(args[0].name_size == strlen("_impl") && !memcmp(args[0].name, "_impl", args[0].name_size));
    ck_assert(args[0].val_size != 0);
    if (args[0].val_size > sizeof user.name)
        return knd_LIMIT;
    memcpy(user.name, args[0].val, args[0].val_size);
    user.name_size = args[0].val_size;
    return knd_OK;
}

//struct kndTaskSpec
//    name_spec = { .is_implied = true, .run = NULL, .obj = &user },
//    sid_spec = { .name = "sid", .name_size = sizeof "sid" - 1, .buf = user.sid, .buf_size = &user.sid_size, .max_buf_size = sizeof user.sid };


START_TEST(parse_task_empty)
    struct kndTaskSpec specs[] = {
        { .is_implied = true, .run = run_set_name, .obj = &user },
        { .name = "sid", .name_size = strlen("sid"), .buf = user.sid, .buf_size = &user.sid_size, .max_buf_size = sizeof user.sid }
    };

    rc = knd_parse_task(rec = "", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_OK);
    ck_assert(total_size == strlen(rec));
    ck_assert(user.name_size == 0 && user.sid_size == 0);

    rc = knd_parse_task(rec = "}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_OK);
    ck_assert(total_size == strlen(rec) - 1);  // shared brace
    ck_assert(user.name_size == 0 && user.sid_size == 0);
END_TEST

START_TEST(parse_task_empty_with_spaces)
    struct kndTaskSpec specs[] = {
        { .is_implied = true, .run = run_set_name, .obj = &user },
        { .name = "sid", .name_size = strlen("sid"), .buf = user.sid, .buf_size = &user.sid_size, .max_buf_size = sizeof user.sid }
    };

    rc = knd_parse_task(rec = "     ", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_OK);
    ck_assert(total_size == strlen(rec));
    ck_assert(user.name_size == 0 && user.sid_size == 0);

    rc = knd_parse_task(rec = "     }", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_OK);
    ck_assert(total_size == strlen(rec) - 1);  // shared brace
    ck_assert(user.name_size == 0 && user.sid_size == 0);
END_TEST

START_TEST(parse_task_empty_with_closing_brace)
    struct kndTaskSpec specs[] = {
        { .is_implied = true, .run = run_set_name, .obj = &user },
        { .name = "sid", .name_size = strlen("sid"), .buf = user.sid, .buf_size = &user.sid_size, .max_buf_size = sizeof user.sid }
    };

    rc = knd_parse_task(rec = " }     ", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_OK);
    ck_assert(total_size == (size_t)(strchr(rec, '}') - rec));  // shared brace
END_TEST

START_TEST(parse_implied_field_with_spaces)
    struct kndTaskSpec specs[] = {{ .is_implied = true, .run = run_set_name, .obj = &user }};

    rc = knd_parse_task(rec = " John Smith}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_OK);
    ck_assert(total_size == strlen(rec) - 1);  // shared brace
    ck_assert(user.name_size == strlen("John Smith") && !memcmp(user.name, "John Smith", user.name_size));

    rc = knd_parse_task(rec = " John Space }", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_OK);
    ck_assert(total_size == strlen(rec) - 1);  // shared brace
    ck_assert(user.name_size == strlen("John Space") && !memcmp(user.name, "John Space", user.name_size));
END_TEST

START_TEST(parse_implied_field_max_size)
    struct kndTaskSpec specs[] = {{ .is_implied = true, .run = run_set_name, .obj = &user }};
    const char buf[] = { [0 ... KND_SHORT_NAME_SIZE - 1] = 'a', [KND_SHORT_NAME_SIZE] = '}', [KND_SHORT_NAME_SIZE + 1] = '\0' };

    rc = knd_parse_task(rec = buf, &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_OK);
    ck_assert(total_size == strlen(rec) - 1);  // shared brace
    ck_assert(user.name_size == KND_SHORT_NAME_SIZE && !memcmp(user.name, buf, user.name_size));
END_TEST

START_TEST(parse_implied_field_max_size_plus_one)
    struct kndTaskSpec specs[] = {{ .is_implied = true, .run = run_set_name, .obj = &user }};
    const char buf[] = { [0 ... KND_SHORT_NAME_SIZE] = 'a', [KND_SHORT_NAME_SIZE + 1] = '}', [KND_SHORT_NAME_SIZE + 2] = '\0' };

    rc = knd_parse_task(rec = buf, &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_LIMIT);  // defined in run_set_name()
END_TEST

START_TEST(parse_implied_field_size_NAME_SIZE_plus_one)
    struct kndTaskSpec specs[] = {{ .is_implied = true, .run = run_set_name, .obj = &user }};
    const char buf[] = { [0 ... KND_NAME_SIZE] = 'a', [KND_NAME_SIZE + 1] = '}', [KND_NAME_SIZE + 2] = '\0' };

    rc = knd_parse_task(rec = buf, &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_LIMIT);
END_TEST

START_TEST(parse_tag_empty)
    struct kndTaskSpec specs[] = {{ .name = "sid", .name_size = strlen("sid"), .buf = user.sid, .buf_size = &user.sid_size, .max_buf_size = sizeof user.sid }};

    rc = knd_parse_task(rec = "{}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_FORMAT);
END_TEST

START_TEST(parse_tag_empty_with_spaces)
    struct kndTaskSpec specs[] = {{ .name = "sid", .name_size = strlen("sid"), .buf = user.sid, .buf_size = &user.sid_size, .max_buf_size = sizeof user.sid }};

    rc = knd_parse_task(rec = "{     }}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_FORMAT);
END_TEST

START_TEST(parse_tag_unknown)
    struct kndTaskSpec specs[] = {{ .name = "sid", .name_size = strlen("sid"), .buf = user.sid, .buf_size = &user.sid_size, .max_buf_size = sizeof user.sid }};

    rc = knd_parse_task(rec = "{ 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_NO_MATCH);

    rc = knd_parse_task(rec = "{s 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_NO_MATCH);

    rc = knd_parse_task(rec = "{si 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_NO_MATCH);

    rc = knd_parse_task(rec = "{sid 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_OK);

    rc = knd_parse_task(rec = "{sido 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_NO_MATCH);
END_TEST

START_TEST(parse_value_empty)
    struct kndTaskSpec specs[] = {{ .name = "sid", .name_size = strlen("sid"), .buf = user.sid, .buf_size = &user.sid_size, .max_buf_size = sizeof user.sid }};

    rc = knd_parse_task(rec = "{sid}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_FORMAT);  // TODO(ki.stfu): Call the default handler
END_TEST

START_TEST(parse_value_empty_with_spaces)
    struct kndTaskSpec specs[] = {{ .name = "sid", .name_size = strlen("sid"), .buf = user.sid, .buf_size = &user.sid_size, .max_buf_size = sizeof user.sid }};

    rc = knd_parse_task(rec = "{sid   }}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_FORMAT);
END_TEST

START_TEST(parse_value_max_size)
    struct kndTaskSpec specs[] = {{ .name = "sid", .name_size = strlen("sid"), .buf = user.sid, .buf_size = &user.sid_size, .max_buf_size = sizeof user.sid }};

    rc = knd_parse_task(rec = "{sid 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_OK);
    ck_assert(total_size == strlen(rec) - 1);  // shared brace
    ck_assert(user.sid_size == strlen("123456") && !memcmp(user.sid, "123456", user.sid_size));
END_TEST

START_TEST(parse_value_max_size_plus_one)
    struct kndTaskSpec specs[] = {{ .name = "sid", .name_size = strlen("sid"), .buf = user.sid, .buf_size = &user.sid_size, .max_buf_size = sizeof user.sid }};

    rc = knd_parse_task(rec = "{sid 1234567}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_LIMIT);
END_TEST

int main() {
   TCase* tc = tcase_create("all cases");
   tcase_add_checked_fixture(tc, setup, NULL);
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
   tcase_add_test(tc, parse_value_empty);
   tcase_add_test(tc, parse_value_empty_with_spaces);
   tcase_add_test(tc, parse_value_max_size);
   tcase_add_test(tc, parse_value_max_size_plus_one);

   Suite* s = suite_create("suite");
   suite_add_tcase(s, tc);
   SRunner* sr = srunner_create(s);
   srunner_run_all(sr, CK_NORMAL);
   srunner_free(sr);
   return 0;
}
