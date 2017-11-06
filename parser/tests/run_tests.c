#include <knd_parser.h>
#include <knd_task.h>

#include <check.h>

#include <assert.h>
#include <string.h>

// --------------------------------------------------------------------------------
// User -- testable object
struct User {
    char name[KND_SHORT_NAME_SIZE]; size_t name_size;
    char sid[6]; size_t sid_size;
    char email[KND_SHORT_NAME_SIZE]; size_t email_size; enum { EMAIL_NONE, EMAIL_HOME, EMAIL_WORK } email_type;
};

// --------------------------------------------------------------------------------
// TaskSpecs -- for passing inner specs into |parse_user|
struct TaskSpecs { struct kndTaskSpec *specs; size_t num_specs; };

// --------------------------------------------------------------------------------
// Common routines
static void test_case_fixture_setup(void) {
    extern struct User user;
    user.name_size = 0;
    user.sid_size = 0;
    user.email_size = 0; user.email_type = EMAIL_NONE;
}

static int parse_user(void *obj, const char *rec, size_t *total_size) {
    struct TaskSpecs *args = (struct TaskSpecs *)obj;
    return knd_parse_task(rec, total_size, args->specs, args->num_specs);
}

static int run_set_name(void *obj, struct kndTaskArg *args, size_t num_args) {
    struct User *self = (struct User *)obj;
    ck_assert(args); ck_assert_uint_eq(num_args, 1);
    if (args[0].name_size == strlen("_impl"))
        ck_assert_str_eq(args[0].name, "_impl");
    else if (args[0].name_size == strlen("name"))
        ck_assert_str_eq(args[0].name, "name");
    else
        ck_assert(args[0].name_size == strlen("_impl") || args[0].name_size == strlen("name"));
    ck_assert_uint_ne(args[0].val_size, 0);
    if (args[0].val_size > sizeof self->name)
        return knd_LIMIT;
    memcpy(self->name, args[0].val, args[0].val_size);
    self->name[args[0].val_size] = '\0';
    self->name_size = args[0].val_size;
    return knd_OK;
}

static int run_set_sid(void *obj, struct kndTaskArg *args, size_t num_args) {
    struct User *self = (struct User *)obj;
    ck_assert(args); ck_assert_uint_eq(num_args, 1);
    ck_assert(args[0].name_size == strlen("sid")); ck_assert_str_eq(args[0].name, "sid");
    ck_assert_uint_ne(args[0].val_size, 0);
    if (args[0].val_size > sizeof self->sid)
        return knd_LIMIT;
    memcpy(self->sid, args[0].val, args[0].val_size);
    self->sid_size = args[0].val_size;
    return knd_OK;
}

static int run_set_default_email(void *obj,
                                 struct kndTaskArg *args,
                                 size_t num_args) {
    struct User *self = (struct User *)obj;
    ck_assert(self);
    ck_assert(!args); ck_assert_uint_eq(num_args, 0);

    self->email_type = EMAIL_NONE;
    self->email_size = 0;
    return knd_OK;  // ok: no email by default
}

static int parse_email_record(void *obj,
                              const char *name, size_t name_size,
                              const char *rec, size_t *total_size) {
    struct User *self = (struct User *)obj;
    struct kndTaskSpec specs[] = {
        {
          .is_implied = true,
          .buf = self->email,
          .buf_size = &self->email_size,
          .max_buf_size = sizeof self->email
        },
        {
          .is_default = true,
          .run = run_set_default_email,  // We are okay even if email is empty
          .obj = self
        }
    };
    int err;

    if (self->email_type != EMAIL_NONE)
        return knd_FAIL;  // error: only 1 email address can be specified

    if (name_size == strlen("home") && !memcmp(name, "home", name_size))
        self->email_type = EMAIL_HOME;
    else if (name_size == strlen("work") && !memcmp(name, "work", name_size))
        self->email_type = EMAIL_WORK;
    else
        return knd_FAIL;  // error: unknown type

    err = knd_parse_task(rec, total_size, specs, sizeof specs  / sizeof specs[0]);
    if (err) {
        self->email_type = EMAIL_NONE;
        return err;
    }

    return knd_OK;
}

static int parse_email(void *obj, const char *rec, size_t *total_size) {
    struct User *self = (struct User *)obj;
    char email_type_buf[KND_NAME_SIZE];  // TODO(ki.stfu): Don't use external buffer for passing name to |spec->validate|
    size_t email_type_buf_size;
    struct kndTaskSpec specs[] = {
        {
          .is_validator = true,
          .buf = email_type_buf,
          .buf_size = &email_type_buf_size,
          .max_buf_size = sizeof email_type_buf,
          .validate = parse_email_record,
          .obj = self
        },
        {
          .is_default = true,
          .run = run_set_default_email,  // We are okay even if email is empty
          .obj = self
        }
    };

    return knd_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

#define RESET_IS_COMPLETED(specs, num_specs)   \
    do {                                       \
        for (size_t i = 0; i < num_specs; ++i) \
          specs[i].is_completed = false;       \
    } while (0)

#define RESET_IS_COMPLETED_kndTaskSpec(specs) \
    RESET_IS_COMPLETED(specs, sizeof specs / sizeof specs[0])

#define RESET_IS_COMPLETED_TaskSpecs(task_specs) \
    RESET_IS_COMPLETED((task_specs)->specs, (task_specs)->num_specs)

#define DEFINE_TaskSpecs(name, ...)                 \
    struct kndTaskSpec __specs##__LINE__[] = { __VA_ARGS__ }; \
    struct TaskSpecs name = { __specs##__LINE__, sizeof __specs##__LINE__ / sizeof __specs##__LINE__[0] }

#define ASSERT_STR_EQ(act, act_size, exp, exp_size...)  \
    do {                                                \
        const char *__act = (act);                      \
        size_t __act_size = (act_size);                 \
        const char *__exp = (exp);                      \
        size_t __exp_size = (exp_size-0) == 0 ? strlen(__exp) : (exp_size-0);                    \
        ck_assert_msg(__act_size == __exp_size && 0 == strncmp(__act, __exp, __act_size),        \
            "Assertion '%s' failed: %s == \"%.*s\" [len: %zu] but expected \"%.*s\" [len: %zu]", \
            #act" == "#exp, #act, __act_size, __act, __act_size, __exp_size, __exp, __exp_size); \
    } while (0)

// --------------------------------------------------------------------------------
// Generators for specs

static struct kndTaskSpec gen_user_spec(struct TaskSpecs *args) {
    return (struct kndTaskSpec){ .name = "user", .name_size = strlen("user"), .parse = parse_user, .obj = args };
}

enum { SPEC_BUF = 0x0, SPEC_RUN = 0x1, SPEC_NAME = 0x2 };
static struct kndTaskSpec gen_name_spec(struct User *self, int flags) {
    // Valid flags: [SPEC_BUF | SPEC_RUN] [SPEC_NAME]
    if (flags & SPEC_RUN)
        return (struct kndTaskSpec){ .is_implied = true,
                                     .name = (flags & SPEC_NAME) ? "name" : NULL, .name_size = (flags & SPEC_NAME) ? strlen("name") : 0,
                                     .run = run_set_name, .obj = self };
    return (struct kndTaskSpec){ .is_implied = true,
                                 .name = (flags & SPEC_NAME) ? "name" : NULL, .name_size = (flags & SPEC_NAME) ? strlen("name") : 0,
                                 .buf = self->name, .buf_size = &self->name_size, .max_buf_size = sizeof self->name };
}

static struct kndTaskSpec gen_sid_spec(struct User *self, int flags) {
    // Valid flags: [SPEC_BUF | SPEC_RUN]
    if (flags & SPEC_RUN)
        return (struct kndTaskSpec){ .name = "sid", .name_size = strlen("sid"),
                                     .run = run_set_sid, .obj = self };
    return (struct kndTaskSpec){ .name = "sid", .name_size = strlen("sid"),
                                 .buf = self->sid, .buf_size = &self->sid_size, .max_buf_size = sizeof self->sid };
}

static struct kndTaskSpec gen_email_spec(struct User *self) {
    return (struct kndTaskSpec){ .name = "email", .name_size = strlen("email"), .parse = parse_email, .obj = self };
}

// --------------------------------------------------------------------------------
// Common variables
int rc;
const char *rec;
size_t total_size;
struct User user;


START_TEST(parse_task_empty)
    DEFINE_TaskSpecs(parse_user_args, gen_name_spec(&user, 0), gen_sid_spec(&user, 0));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

    rc = knd_parse_task(rec = "", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);  // TODO(ki.stfu): Call the default handler
    ck_assert_uint_eq(total_size, strlen(rec));
    ck_assert_uint_eq(user.name_size, 0); ck_assert_uint_eq(user.sid_size, 0);
END_TEST

START_TEST(parse_task_empty_with_spaces)
    DEFINE_TaskSpecs(parse_user_args, gen_name_spec(&user, 0), gen_sid_spec(&user, 0));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

    rc = knd_parse_task(rec = "     ", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);  // TODO(ki.stfu): Call the default handler
    ck_assert_uint_eq(total_size, strlen(rec));
    ck_assert_uint_eq(user.name_size, 0); ck_assert_uint_eq(user.sid_size, 0);
END_TEST

START_TEST(parse_task_empty_with_closing_brace)
    DEFINE_TaskSpecs(parse_user_args, gen_name_spec(&user, 0), gen_sid_spec(&user, 0));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

    rc = knd_parse_task(rec = " }     ", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);  // TODO(ki.stfu): Call the default handler
    ck_assert_uint_eq(total_size, strchr(rec, '}') - rec);  // shared brace
END_TEST

static void
check_parse_implied_field(struct kndTaskSpec *specs,
                          size_t num_specs,
                          struct TaskSpecs *parse_user_args) {
    rc = knd_parse_task(rec = "{user John Smith}", &total_size, specs, num_specs);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ASSERT_STR_EQ(user.name, user.name_size, "John Smith");
    RESET_IS_COMPLETED(specs, num_specs); RESET_IS_COMPLETED_TaskSpecs(parse_user_args);

    rc = knd_parse_task(rec = "{user John Smith{sid 123456}}", &total_size, specs, num_specs);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ASSERT_STR_EQ(user.name, user.name_size, "John Smith");
    ASSERT_STR_EQ(user.sid, user.sid_size, "123456");
    RESET_IS_COMPLETED(specs, num_specs); RESET_IS_COMPLETED_TaskSpecs(parse_user_args);

    rc = knd_parse_task(rec = "{user John Smith {sid 123456}}", &total_size, specs, num_specs);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ASSERT_STR_EQ(user.name, user.name_size, "John Smith");
    ASSERT_STR_EQ(user.sid, user.sid_size, "123456");
    RESET_IS_COMPLETED(specs, num_specs); RESET_IS_COMPLETED_TaskSpecs(parse_user_args);

    rc = knd_parse_task(rec = "{user   John Smith   {sid 123456}}", &total_size, specs, num_specs);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ASSERT_STR_EQ(user.name, user.name_size, "John Smith");
    ASSERT_STR_EQ(user.sid, user.sid_size, "123456");
    RESET_IS_COMPLETED(specs, num_specs); RESET_IS_COMPLETED_TaskSpecs(parse_user_args);

    rc = knd_parse_task(rec = "{user {sid 123456}John Smith}", &total_size, specs, num_specs);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ASSERT_STR_EQ(user.name, user.name_size, "John Smith");
    ASSERT_STR_EQ(user.sid, user.sid_size, "123456");
    RESET_IS_COMPLETED(specs, num_specs); RESET_IS_COMPLETED_TaskSpecs(parse_user_args);

    rc = knd_parse_task(rec = "{user {sid 123456} John Smith}", &total_size, specs, num_specs);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ASSERT_STR_EQ(user.name, user.name_size, "John Smith");
    ASSERT_STR_EQ(user.sid, user.sid_size, "123456");
    RESET_IS_COMPLETED(specs, num_specs); RESET_IS_COMPLETED_TaskSpecs(parse_user_args);

    rc = knd_parse_task(rec = "{user {sid 123456}   John Smith   }", &total_size, specs, num_specs);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ASSERT_STR_EQ(user.name, user.name_size, "John Smith");
    ASSERT_STR_EQ(user.sid, user.sid_size, "123456");
    RESET_IS_COMPLETED(specs, num_specs); RESET_IS_COMPLETED_TaskSpecs(parse_user_args);
}

START_TEST(parse_implied_field)
  // Check implied field with .buf
  {
    DEFINE_TaskSpecs(parse_user_args, gen_name_spec(&user, SPEC_BUF), gen_sid_spec(&user, 0));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

    check_parse_implied_field(specs, sizeof specs / sizeof specs[0], &parse_user_args);
  }

  // Check implied field with .run
  {
    DEFINE_TaskSpecs(parse_user_args, gen_name_spec(&user, SPEC_RUN), gen_sid_spec(&user, 0));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

    check_parse_implied_field(specs, sizeof specs / sizeof specs[0], &parse_user_args);
  }
END_TEST

START_TEST(parse_implied_field_with_name)
  // Check implied field with .buf & .name
  {
    DEFINE_TaskSpecs(parse_user_args, gen_name_spec(&user, SPEC_BUF | SPEC_NAME), gen_sid_spec(&user, 0));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

    check_parse_implied_field(specs, sizeof specs / sizeof specs[0], &parse_user_args);

    rc = knd_parse_task(rec = "{user {name John Smith}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ASSERT_STR_EQ(user.name, user.name_size, "John Smith");
  }

  // Check implied field with .run & .name
  {
    DEFINE_TaskSpecs(parse_user_args, gen_name_spec(&user, SPEC_RUN | SPEC_NAME), gen_sid_spec(&user, 0));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

    check_parse_implied_field(specs, sizeof specs / sizeof specs[0], &parse_user_args);

    rc = knd_parse_task(rec = "{user {name John Smith}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ASSERT_STR_EQ(user.name, user.name_size, "John Smith");
  }
END_TEST

START_TEST(parse_implied_field_unknown)
    DEFINE_TaskSpecs(parse_user_args, gen_sid_spec(&user, 0));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

    rc = knd_parse_task(rec = "{user John Smith}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_NO_MATCH);

    rc = knd_parse_task(rec = "{user John Smith {sid 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_NO_MATCH);
END_TEST

static void
check_parse_implied_field_max_size(struct kndTaskSpec *specs,
                                   size_t num_specs,
                                   struct TaskSpecs *parse_user_args) {
  {
    const char buf[] = { '{', 'u', 's', 'e', 'r', ' ', [6 ... KND_SHORT_NAME_SIZE + 5] = 'a', '}', '\0' };
    rc = knd_parse_task(rec = buf, &total_size, specs, num_specs);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ASSERT_STR_EQ(user.name, user.name_size, strchr(buf, 'a'), KND_SHORT_NAME_SIZE);
  }
    RESET_IS_COMPLETED(specs, num_specs); RESET_IS_COMPLETED_TaskSpecs(parse_user_args);

  {
    const char buf[] = { '{', 'u', 's', 'e', 'r', ' ', [6 ... KND_SHORT_NAME_SIZE + 5] = 'a', ' ', '{', 's', 'i', 'd', ' ', '1', '2', '3', '4', '5', '6', '}', '}', '\0' };
    rc = knd_parse_task(rec = buf, &total_size, specs, num_specs);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ASSERT_STR_EQ(user.name, user.name_size, strchr(buf, 'a'), KND_SHORT_NAME_SIZE);
    ASSERT_STR_EQ(user.sid, user.sid_size, "123456");
  }
    RESET_IS_COMPLETED(specs, num_specs); RESET_IS_COMPLETED_TaskSpecs(parse_user_args);
}

START_TEST(parse_implied_field_max_size)
  // Check implied field with .buf
  {
    DEFINE_TaskSpecs(parse_user_args, gen_name_spec(&user, SPEC_BUF), gen_sid_spec(&user, 0));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

    check_parse_implied_field_max_size(specs, sizeof specs / sizeof specs[0], &parse_user_args);
  }

  // Check implied field with .run
  {
    DEFINE_TaskSpecs(parse_user_args, gen_name_spec(&user, SPEC_RUN), gen_sid_spec(&user, 0));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

    check_parse_implied_field_max_size(specs, sizeof specs / sizeof specs[0], &parse_user_args);
  }
END_TEST

static void
check_parse_implied_field_max_size_plus_one(struct kndTaskSpec *specs,
                                            size_t num_specs,
                                            struct TaskSpecs *parse_user_args) {
  {
    const char buf[] = { '{', 'u', 's', 'e', 'r', ' ', [6 ... KND_SHORT_NAME_SIZE + 6] = 'a', '}', '\0' };
    rc = knd_parse_task(rec = buf, &total_size, specs, num_specs);
    ck_assert_int_eq(rc, knd_LIMIT);
  }

  {
    const char buf[] = { '{', 'u', 's', 'e', 'r', ' ', [6 ... KND_SHORT_NAME_SIZE + 6] = 'a', ' ', '{', 's', 'i', 'd', ' ', '1', '2', '3', '4', '5', '6', '}', '}', '\0' };
    rc = knd_parse_task(rec = buf, &total_size, specs, num_specs);
    ck_assert_int_eq(rc, knd_LIMIT);
  }
}

START_TEST(parse_implied_field_max_size_plus_one)
  // Check implied field with .buf
  {
    DEFINE_TaskSpecs(parse_user_args, gen_name_spec(&user, SPEC_BUF));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

    check_parse_implied_field_max_size_plus_one(specs, sizeof specs / sizeof specs[0], &parse_user_args);
  }

  // Check implied field with .run
  {
    DEFINE_TaskSpecs(parse_user_args, gen_name_spec(&user, SPEC_RUN));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

    check_parse_implied_field_max_size_plus_one(specs, sizeof specs / sizeof specs[0], &parse_user_args);
  }
END_TEST

static void
check_parse_implied_field_size_NAME_SIZE_plus_one(struct kndTaskSpec *specs,
                                                  size_t num_specs,
                                                  struct TaskSpecs *parse_user_args) {
  {
    const char buf[] = { '{', 'u', 's', 'e', 'r', ' ', [6 ... KND_NAME_SIZE + 6] = 'a', '}', '\0' };
    rc = knd_parse_task(rec = buf, &total_size, specs, num_specs);
    ck_assert_int_eq(rc, knd_LIMIT);
  }

  {
    const char buf[] = { '{', 'u', 's', 'e', 'r', ' ', [6 ... KND_NAME_SIZE + 6] = 'a', ' ', '{', 's', 'i', 'd', ' ', '1', '2', '3', '4', '5', '6', '}', '}', '\0' };
    rc = knd_parse_task(rec = buf, &total_size, specs, num_specs);
    ck_assert_int_eq(rc, knd_LIMIT);
  }
}

START_TEST(parse_implied_field_size_NAME_SIZE_plus_one)
  // Check implied field with .buf
  {
    DEFINE_TaskSpecs(parse_user_args, gen_name_spec(&user, SPEC_BUF));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

    check_parse_implied_field_size_NAME_SIZE_plus_one(specs, sizeof specs / sizeof specs[0], &parse_user_args);
  }

  // Check implied field with .run
  {
    DEFINE_TaskSpecs(parse_user_args, gen_name_spec(&user, SPEC_RUN));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

    check_parse_implied_field_size_NAME_SIZE_plus_one(specs, sizeof specs / sizeof specs[0], &parse_user_args);
  }
END_TEST

START_TEST(parse_tag_empty)
    DEFINE_TaskSpecs(parse_user_args, gen_sid_spec(&user, 0));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

    rc = knd_parse_task(rec = "{user{}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);

    rc = knd_parse_task(rec = "{user{{home john@imloud.com}}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);

    rc = knd_parse_task(rec = "{user {}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);

    rc = knd_parse_task(rec = "{user {{home john@imloud.com}}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);
END_TEST

START_TEST(parse_tag_empty_with_spaces)
    DEFINE_TaskSpecs(parse_user_args, gen_sid_spec(&user, 0));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

    rc = knd_parse_task(rec = "{user{   }}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);

    rc = knd_parse_task(rec = "{user{   123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);

    rc = knd_parse_task(rec = "{user{   {home john@imloud.com}}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);

    rc = knd_parse_task(rec = "{user {   }}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);

    rc = knd_parse_task(rec = "{user {   123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);

    rc = knd_parse_task(rec = "{user {   {home john@imloud.com}}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);
END_TEST

START_TEST(parse_tag_unknown)
    DEFINE_TaskSpecs(parse_user_args, gen_sid_spec(&user, 0));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

    rc = knd_parse_task(rec = "{user{s 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_NO_MATCH);

    rc = knd_parse_task(rec = "{user{si 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_NO_MATCH);

    rc = knd_parse_task(rec = "{user{sid 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    RESET_IS_COMPLETED_kndTaskSpec(specs); RESET_IS_COMPLETED_TaskSpecs(&parse_user_args);

    rc = knd_parse_task(rec = "{user{sido 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_NO_MATCH);

    rc = knd_parse_task(rec = "{user {s 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_NO_MATCH);

    rc = knd_parse_task(rec = "{user {si 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_NO_MATCH);

    rc = knd_parse_task(rec = "{user {sid 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    RESET_IS_COMPLETED_kndTaskSpec(specs); RESET_IS_COMPLETED_TaskSpecs(&parse_user_args);

    rc = knd_parse_task(rec = "{user {sido 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_NO_MATCH);
END_TEST

static void
check_parse_value_terminal_empty(struct kndTaskSpec *specs, size_t num_specs) {
    rc = knd_parse_task(rec = "{user{sid}}", &total_size, specs, num_specs);
    ck_assert_int_eq(rc, knd_FORMAT);

    rc = knd_parse_task(rec = "{user {sid}}", &total_size, specs, num_specs);
    ck_assert_int_eq(rc, knd_FORMAT);
}

START_TEST(parse_value_terminal_empty)
  // Check field with terminal value with .buf
  {
    DEFINE_TaskSpecs(parse_user_args, gen_sid_spec(&user, SPEC_BUF));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

    check_parse_value_terminal_empty(specs, sizeof specs / sizeof specs[0]);
  }

  // Check field with terminal value with .run
  {
    DEFINE_TaskSpecs(parse_user_args, gen_sid_spec(&user, SPEC_RUN));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

    check_parse_value_terminal_empty(specs, sizeof specs / sizeof specs[0]);
  }
END_TEST

START_TEST(parse_value_terminal_empty_with_spaces)
    DEFINE_TaskSpecs(parse_user_args, gen_sid_spec(&user, 0));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

    rc = knd_parse_task(rec = "{user{sid   }}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);

    rc = knd_parse_task(rec = "{user {sid   }}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);
END_TEST

START_TEST(parse_value_terminal_max_size)
    DEFINE_TaskSpecs(parse_user_args, gen_sid_spec(&user, 0));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

    rc = knd_parse_task(rec = "{user{sid 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ASSERT_STR_EQ(user.sid, user.sid_size, "123456");
    RESET_IS_COMPLETED_kndTaskSpec(specs); RESET_IS_COMPLETED_TaskSpecs(&parse_user_args);

    rc = knd_parse_task(rec = "{user {sid 123456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ASSERT_STR_EQ(user.sid, user.sid_size, "123456");
END_TEST

START_TEST(parse_value_terminal_max_size_plus_one)
    DEFINE_TaskSpecs(parse_user_args, gen_sid_spec(&user, 0));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

    rc = knd_parse_task(rec = "{user{sid 1234567}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_LIMIT);

    rc = knd_parse_task(rec = "{user {sid 1234567}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_LIMIT);
END_TEST

START_TEST(parse_value_terminal_NAME_SIZE_plus_one)
    DEFINE_TaskSpecs(parse_user_args, gen_sid_spec(&user, 0));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

  {
    const char buf[] = { '{', 'u', 's', 'e', 'r', '{', 's', 'i', 'd', ' ', [10 ... KND_NAME_SIZE + 10] = '1', '}', '}', '\0' };
    rc = knd_parse_task(rec = buf, &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_LIMIT);
  }

  {
    const char buf[] = { '{', 'u', 's', 'e', 'r', ' ', '{', 's', 'i', 'd', ' ', [11 ... KND_NAME_SIZE + 11] = '1', '}', '}', '\0' };
    rc = knd_parse_task(rec = buf, &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_LIMIT);
  }
END_TEST

START_TEST(parse_value_terminal_with_braces)
    DEFINE_TaskSpecs(parse_user_args, gen_sid_spec(&user, 0));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

    rc = knd_parse_task(rec = "{user{sid{123456}}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);

    rc = knd_parse_task(rec = "{user{sid {123456}}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);

    rc = knd_parse_task(rec = "{user{sid 123{456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);

    rc = knd_parse_task(rec = "{user {sid{123456}}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);

    rc = knd_parse_task(rec = "{user {sid {123456}}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);

    rc = knd_parse_task(rec = "{user {sid 123{456}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FORMAT);
END_TEST

START_TEST(parse_value_validate_empty)
    DEFINE_TaskSpecs(parse_user_args, gen_email_spec(&user));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

    rc = knd_parse_task(rec = "{user{email}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ck_assert_int_eq(user.email_type, EMAIL_NONE); ck_assert_uint_eq(user.email_size, 0);
    RESET_IS_COMPLETED_kndTaskSpec(specs); RESET_IS_COMPLETED_TaskSpecs(&parse_user_args);

    rc = knd_parse_task(rec = "{user {email}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ck_assert_int_eq(user.email_type, EMAIL_NONE); ck_assert_uint_eq(user.email_size, 0);
    RESET_IS_COMPLETED_kndTaskSpec(specs); RESET_IS_COMPLETED_TaskSpecs(&parse_user_args);

    rc = knd_parse_task(rec = "{user {email{home}}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ck_assert_int_eq(user.email_type, EMAIL_NONE); ck_assert_uint_eq(user.email_size, 0);
    RESET_IS_COMPLETED_kndTaskSpec(specs); RESET_IS_COMPLETED_TaskSpecs(&parse_user_args);

    rc = knd_parse_task(rec = "{user {email {home}}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ck_assert_int_eq(user.email_type, EMAIL_NONE); ck_assert_uint_eq(user.email_size, 0);
    RESET_IS_COMPLETED_kndTaskSpec(specs); RESET_IS_COMPLETED_TaskSpecs(&parse_user_args);

    rc = knd_parse_task(rec = "{user {email{home}{work}}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ck_assert_int_eq(user.email_type, EMAIL_NONE); ck_assert_uint_eq(user.email_size, 0);
    RESET_IS_COMPLETED_kndTaskSpec(specs); RESET_IS_COMPLETED_TaskSpecs(&parse_user_args);

    rc = knd_parse_task(rec = "{user {email{home} {work}}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ck_assert_int_eq(user.email_type, EMAIL_NONE); ck_assert_uint_eq(user.email_size, 0);
    RESET_IS_COMPLETED_kndTaskSpec(specs); RESET_IS_COMPLETED_TaskSpecs(&parse_user_args);

    rc = knd_parse_task(rec = "{user {email {home}{work}}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ck_assert_int_eq(user.email_type, EMAIL_NONE); ck_assert_uint_eq(user.email_size, 0);
    RESET_IS_COMPLETED_kndTaskSpec(specs); RESET_IS_COMPLETED_TaskSpecs(&parse_user_args);

    rc = knd_parse_task(rec = "{user {email {home} {work}}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ck_assert_int_eq(user.email_type, EMAIL_NONE); ck_assert_uint_eq(user.email_size, 0);
    RESET_IS_COMPLETED_kndTaskSpec(specs); RESET_IS_COMPLETED_TaskSpecs(&parse_user_args);
END_TEST

START_TEST(parse_value_validate_single)
    DEFINE_TaskSpecs(parse_user_args, gen_email_spec(&user));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

    rc = knd_parse_task(rec = "{user {email{home john@iserver.com}}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ck_assert_int_eq(user.email_type, EMAIL_HOME); ASSERT_STR_EQ(user.email, user.email_size, "john@iserver.com");
    user.email_type = EMAIL_NONE; RESET_IS_COMPLETED_kndTaskSpec(specs); RESET_IS_COMPLETED_TaskSpecs(&parse_user_args);

    rc = knd_parse_task(rec = "{user {email{work j.smith@gogel.com}}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ck_assert_int_eq(user.email_type, EMAIL_WORK); ASSERT_STR_EQ(user.email, user.email_size, "j.smith@gogel.com");
    user.email_type = EMAIL_NONE; RESET_IS_COMPLETED_kndTaskSpec(specs); RESET_IS_COMPLETED_TaskSpecs(&parse_user_args);

    rc = knd_parse_task(rec = "{user {email {home john@iserver.com}}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ck_assert_int_eq(user.email_type, EMAIL_HOME); ASSERT_STR_EQ(user.email, user.email_size, "john@iserver.com");
    user.email_type = EMAIL_NONE; RESET_IS_COMPLETED_kndTaskSpec(specs); RESET_IS_COMPLETED_TaskSpecs(&parse_user_args);

    rc = knd_parse_task(rec = "{user {email {work j.smith@gogel.com}}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ck_assert_int_eq(user.email_type, EMAIL_WORK); ASSERT_STR_EQ(user.email, user.email_size, "j.smith@gogel.com");
    user.email_type = EMAIL_NONE; RESET_IS_COMPLETED_kndTaskSpec(specs); RESET_IS_COMPLETED_TaskSpecs(&parse_user_args);
END_TEST

START_TEST(parse_value_validate_several)
    DEFINE_TaskSpecs(parse_user_args, gen_email_spec(&user));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

    rc = knd_parse_task(rec = "{user {email{home john@iserver.com}{work j.smith@gogel.com}}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FAIL);  // defined in parse_email_record()
    user.email_type = EMAIL_NONE; RESET_IS_COMPLETED_TaskSpecs(&parse_user_args);

    rc = knd_parse_task(rec = "{user {email{home john@iserver.com} {work j.smith@gogel.com}}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FAIL);  // defined in parse_email_record()
    user.email_type = EMAIL_NONE; RESET_IS_COMPLETED_TaskSpecs(&parse_user_args);

    rc = knd_parse_task(rec = "{user {email {home john@iserver.com}{work j.smith@gogel.com}}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FAIL);  // defined in parse_email_record()
    user.email_type = EMAIL_NONE; RESET_IS_COMPLETED_TaskSpecs(&parse_user_args);

    rc = knd_parse_task(rec = "{user {email {home john@iserver.com} {work j.smith@gogel.com}}}", &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_FAIL);  // defined in parse_email_record()
    user.email_type = EMAIL_NONE; RESET_IS_COMPLETED_TaskSpecs(&parse_user_args);
END_TEST

START_TEST(parse_value_validate_max_size)
    DEFINE_TaskSpecs(parse_user_args, gen_email_spec(&user));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

  {
    const char buf[] = { '{', 'u', 's', 'e', 'r', ' ', '{', 'e', 'm', 'a', 'i', 'l', '{', 'h', 'o', 'm', 'e', ' ', [18 ... KND_SHORT_NAME_SIZE + 17] = 'b', '}', '}', '}', '\0' };
    rc = knd_parse_task(rec = buf, &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ck_assert_int_eq(user.email_type, EMAIL_HOME); ASSERT_STR_EQ(user.email, user.email_size, strchr(buf, 'b'), KND_SHORT_NAME_SIZE);
  }
  user.email_type = EMAIL_NONE; RESET_IS_COMPLETED_kndTaskSpec(specs); RESET_IS_COMPLETED_TaskSpecs(&parse_user_args);

  {
    const char buf[] = { '{', 'u', 's', 'e', 'r', ' ', '{', 'e', 'm', 'a', 'i', 'l', ' ', '{', 'w', 'o', 'r', 'k', ' ', [19 ... KND_SHORT_NAME_SIZE + 18] = 'b', '}', '}', '}', '\0' };
    rc = knd_parse_task(rec = buf, &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_OK);
    ck_assert_uint_eq(total_size, strlen(rec));
    ck_assert_int_eq(user.email_type, EMAIL_WORK); ASSERT_STR_EQ(user.email, user.email_size, strchr(buf, 'b'), KND_SHORT_NAME_SIZE);
  }
END_TEST

START_TEST(parse_value_validate_max_size_plus_one)
    DEFINE_TaskSpecs(parse_user_args, gen_email_spec(&user));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

  {
    const char buf[] = { '{', 'u', 's', 'e', 'r', ' ', '{', 'e', 'm', 'a', 'i', 'l', '{', 'h', 'o', 'm', 'e', ' ', [18 ... KND_SHORT_NAME_SIZE + 18] = 'b', '}', '}', '}', '\0' };
    rc = knd_parse_task(rec = buf, &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_LIMIT);
  }

  {
    const char buf[] = { '{', 'u', 's', 'e', 'r', ' ', '{', 'e', 'm', 'a', 'i', 'l', ' ', '{', 'w', 'o', 'r', 'k', ' ', [19 ... KND_SHORT_NAME_SIZE + 19] = 'b', '}', '}', '}', '\0' };
    rc = knd_parse_task(rec = buf, &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_LIMIT);
  }
END_TEST

START_TEST(parse_value_validate_NAME_SIZE_plus_one)
    DEFINE_TaskSpecs(parse_user_args, gen_email_spec(&user));
    struct kndTaskSpec specs[] = { gen_user_spec(&parse_user_args) };

  {
    const char buf[] = { '{', 'u', 's', 'e', 'r', ' ', '{', 'e', 'm', 'a', 'i', 'l', '{', 'h', 'o', 'm', 'e', ' ', [18 ... KND_NAME_SIZE + 18] = 'b', '}', '}', '}', '\0' };
    rc = knd_parse_task(rec = buf, &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_LIMIT);
  }

  {
    const char buf[] = { '{', 'u', 's', 'e', 'r', ' ', '{', 'e', 'm', 'a', 'i', 'l', ' ', '{', 'w', 'o', 'r', 'k', ' ', [19 ... KND_NAME_SIZE + 19] = 'b', '}', '}', '}', '\0' };
    rc = knd_parse_task(rec = buf, &total_size, specs, sizeof specs / sizeof specs[0]);
    ck_assert_int_eq(rc, knd_LIMIT);
  }
END_TEST


int main() {
    TCase* tc = tcase_create("all cases");
    tcase_add_checked_fixture(tc, test_case_fixture_setup, NULL);
    tcase_add_test(tc, parse_task_empty);
    tcase_add_test(tc, parse_task_empty_with_spaces);
    tcase_add_test(tc, parse_task_empty_with_closing_brace);
    tcase_add_test(tc, parse_implied_field);
    tcase_add_test(tc, parse_implied_field_with_name);
    tcase_add_test(tc, parse_implied_field_unknown);
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
    tcase_add_test(tc, parse_value_terminal_NAME_SIZE_plus_one);
    tcase_add_test(tc, parse_value_terminal_with_braces);
    tcase_add_test(tc, parse_value_validate_empty);
    tcase_add_test(tc, parse_value_validate_single);
    tcase_add_test(tc, parse_value_validate_several);
    tcase_add_test(tc, parse_value_validate_max_size);
    tcase_add_test(tc, parse_value_validate_max_size_plus_one);
    tcase_add_test(tc, parse_value_validate_NAME_SIZE_plus_one);

    Suite* s = suite_create("suite");
    suite_add_tcase(s, tc);
    SRunner* sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    srunner_free(sr);
    return 0;
}
