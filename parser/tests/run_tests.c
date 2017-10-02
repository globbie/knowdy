#include <knd_parser.h>
#include <knd_task.h>

#include <check.h>

#include <string.h>


// Common variables
int rc;
const char *rec;
size_t total_size;
struct { char sid[6]; size_t sid_size; } user;

void setup(void) {
    user.sid_size = 0;
}

START_TEST(parse_task_empty)
    struct kndTaskSpec specs[] = {{ .name = "sid", .name_size = strlen("sid"), .buf = user.sid, .buf_size = &user.sid_size, .max_buf_size = sizeof user.sid }};

    rc = knd_parse_task(rec = "", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_OK);
    ck_assert(total_size == strlen(rec));
    ck_assert(user.sid_size == 0);
END_TEST

START_TEST(parse_task_empty_with_spaces)
    struct kndTaskSpec specs[] = {{ .name = "sid", .name_size = strlen("sid"), .buf = user.sid, .buf_size = &user.sid_size, .max_buf_size = sizeof user.sid }};

    rc = knd_parse_task(rec = "     ", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_OK);
    ck_assert(total_size == strlen(rec));
    ck_assert(user.sid_size == 0);
END_TEST

START_TEST(parse_tag_empty)
    struct kndTaskSpec specs[] = {{ .name = "sid", .name_size = strlen("sid"), .buf = user.sid, .buf_size = &user.sid_size, .max_buf_size = sizeof user.sid }};

    rc = knd_parse_task(rec = "{}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_FORMAT);
END_TEST

START_TEST(parse_tag_empty_with_spaces)
    struct kndTaskSpec specs[] = {{ .name = "sid", .name_size = strlen("sid"), .buf = user.sid, .buf_size = &user.sid_size, .max_buf_size = sizeof user.sid }};

    rc = knd_parse_task(rec = "{     }", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_FORMAT);
END_TEST

START_TEST(parse_tag_unknown)
    struct kndTaskSpec specs[] = {{ .name = "sid", .name_size = strlen("sid"), .buf = user.sid, .buf_size = &user.sid_size, .max_buf_size = sizeof user.sid }};

    rc = knd_parse_task(rec = "{ 123456}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_NO_MATCH);

    rc = knd_parse_task(rec = "{s 123456}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_NO_MATCH);

    rc = knd_parse_task(rec = "{si 123456}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_NO_MATCH);

    rc = knd_parse_task(rec = "{sid 123456}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_OK);

    rc = knd_parse_task(rec = "{sido 123456}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_NO_MATCH);
END_TEST

START_TEST(parse_value_empty)
    struct kndTaskSpec specs[] = {{ .name = "sid", .name_size = strlen("sid"), .buf = user.sid, .buf_size = &user.sid_size, .max_buf_size = sizeof user.sid }};

    rc = knd_parse_task(rec = "{sid}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_FORMAT);  // TODO(ki.stfu): Call the default handler
END_TEST

START_TEST(parse_value_empty_with_spaces)
    struct kndTaskSpec specs[] = {{ .name = "sid", .name_size = strlen("sid"), .buf = user.sid, .buf_size = &user.sid_size, .max_buf_size = sizeof user.sid }};

    rc = knd_parse_task(rec = "{sid   }", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_FORMAT);
END_TEST

START_TEST(parse_value_max_size)
    struct kndTaskSpec specs[] = {{ .name = "sid", .name_size = strlen("sid"), .buf = user.sid, .buf_size = &user.sid_size, .max_buf_size = sizeof user.sid }};

    rc = knd_parse_task(rec = "{sid 123456}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_OK);
    ck_assert(total_size == strlen(rec));
    ck_assert(user.sid_size == strlen("123456") && !memcmp(user.sid, "123456", user.sid_size));
END_TEST

START_TEST(parse_value_max_size_plus_one)
    struct kndTaskSpec specs[] = {{ .name = "sid", .name_size = strlen("sid"), .buf = user.sid, .buf_size = &user.sid_size, .max_buf_size = sizeof user.sid }};

    rc = knd_parse_task(rec = "{sid 1234567}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_LIMIT);
END_TEST

int main() {
   TCase* tc = tcase_create("all cases");
   tcase_add_checked_fixture(tc, setup, NULL);
   tcase_add_test(tc, parse_task_empty);
   tcase_add_test(tc, parse_task_empty_with_spaces);
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
