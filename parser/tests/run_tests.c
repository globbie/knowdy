#include <knd_parser.h>
#include <knd_task.h>

#include <check.h>

#include <string.h>


// Common variables
int rc;
const char *rec;
size_t total_size;

START_TEST(parse_task_empty)
    char sid[6]; size_t sid_size = 0;
    struct kndTaskSpec specs[] = {{ .name = "sid", .name_size = strlen("sid"), .buf = sid, .buf_size = &sid_size, .max_buf_size = sizeof sid }};

    rc = knd_parse_task(rec = "", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_OK);
    ck_assert(total_size == strlen(rec));
    ck_assert(sid_size == 0);
END_TEST

START_TEST(parse_task_empty_with_spaces)
    char sid[6]; size_t sid_size = 0;
    struct kndTaskSpec specs[] = {{ .name = "sid", .name_size = strlen("sid"), .buf = sid, .buf_size = &sid_size, .max_buf_size = sizeof sid }};

    rc = knd_parse_task(rec = "     ", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_OK);
    ck_assert(total_size == strlen(rec));
    ck_assert(sid_size == 0);
END_TEST

START_TEST(parse_tag_empty)
    char sid[6]; size_t sid_size = 0;
    struct kndTaskSpec specs[] = {{ .name = "sid", .name_size = strlen("sid"), .buf = sid, .buf_size = &sid_size, .max_buf_size = sizeof sid }};

    rc = knd_parse_task(rec = "{}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_FORMAT);
END_TEST

START_TEST(parse_tag_empty_with_spaces)
    char sid[6]; size_t sid_size = 0;
    struct kndTaskSpec specs[] = {{ .name = "sid", .name_size = strlen("sid"), .buf = sid, .buf_size = &sid_size, .max_buf_size = sizeof sid }};

    rc = knd_parse_task(rec = "{     }", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_FORMAT);
END_TEST

START_TEST(parse_tag_unknown)
    char sid[6]; size_t sid_size = 0;
    struct kndTaskSpec specs[] = {{ .name = "sid", .name_size = strlen("sid"), .buf = sid, .buf_size = &sid_size, .max_buf_size = sizeof sid }};

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
    char sid[6]; size_t sid_size = 0;
    struct kndTaskSpec specs[] = {{ .name = "sid", .name_size = strlen("sid"), .buf = sid, .buf_size = &sid_size, .max_buf_size = sizeof sid }};

    rc = knd_parse_task(rec = "{sid}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_FORMAT);
END_TEST

START_TEST(parse_value_empty_with_spaces)
    char sid[6]; size_t sid_size = 0;
    struct kndTaskSpec specs[] = {{ .name = "sid", .name_size = strlen("sid"), .buf = sid, .buf_size = &sid_size, .max_buf_size = sizeof sid }};

    rc = knd_parse_task(rec = "{sid   }", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_FORMAT);
END_TEST

START_TEST(parse_value_max_size)
    char sid[6]; size_t sid_size = 0;
    struct kndTaskSpec specs[] = {{ .name = "sid", .name_size = strlen("sid"), .buf = sid, .buf_size = &sid_size, .max_buf_size = sizeof sid }};

    rc = knd_parse_task(rec = "{sid 123456}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_OK);
    ck_assert(total_size == strlen(rec));
    ck_assert(sid_size == strlen("123456") && !memcmp(sid, "123456", sid_size));
END_TEST

START_TEST(parse_value_max_size_plus_one)
    char sid[6]; size_t sid_size = 0;
    struct kndTaskSpec specs[] = {{ .name = "sid", .name_size = strlen("sid"), .buf = sid, .buf_size = &sid_size, .max_buf_size = sizeof sid }};

    rc = knd_parse_task(rec = "{sid 1234567}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert(rc == knd_LIMIT);
END_TEST

int main() {
   TCase* tc = tcase_create("all cases");
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
