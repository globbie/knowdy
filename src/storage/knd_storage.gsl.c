#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>

#include "knd_task.h"
#include "knd_output.h"
#include "knd_utils.h"
#include "knd_steward.h"
#include "knd_storage.h"

void append_storage(struct kndSteward *s, struct kndStorage *store)
{
    if (s->storage_tail) {
        s->storage_tail->next = store;
        s->storage_tail = store;
    } else {
        s->storages = store;   
        s->storage_tail = store;   
    }
    s->num_storages++;
}

static gsl_err_t set_storage_quota_unit(void *obj, const char *name, size_t name_size)
{
    struct kndStorage *self = obj;

    for (size_t i = 0; i < sizeof knd_storage_unit_names / sizeof knd_storage_unit_names[0]; i++) {
        const char *unit_str = knd_storage_unit_names[i];
        assert(unit_str != NULL);

        size_t unit_str_size = strlen(unit_str);
        if (name_size != unit_str_size) continue;

        if (!memcmp(unit_str, name, name_size)) {
            self->quota_unit = (knd_storage_unit_type)i;
            return make_gsl_err(gsl_OK);
        }
    }
    return make_gsl_err(gsl_FORMAT);
}

static gsl_err_t parse_storage_quota(void *obj, const char *rec, size_t *total_size)
{
    struct kndStorage *self = obj;

    struct gslTaskSpec specs[] = {
        {   .name = "unit",
            .name_size = strlen("unit"),
            .run = set_storage_quota_unit,
            .obj = obj
        },
        {   .name = "total",
            .name_size = strlen("total"),
            .parse = gsl_parse_size_t,
            .obj = &self->quota_total
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static int set_storage_limits(struct kndStorage *s)
{
    size_t unit_num_bytes = 1;
    size_t leaf_min_size;
    size_t leaf_max_size;

    switch (s->leaf_storage_unit) {
    case KND_STORAGE_UNIT_KB:
        unit_num_bytes = (size_t)1000;
        break;
    case KND_STORAGE_UNIT_MB:
        unit_num_bytes = (size_t)1000 * 1000;
        break;
    case KND_STORAGE_UNIT_GB:
        unit_num_bytes = (size_t)1000 * 1000 * 1000;
        break;
    case KND_STORAGE_UNIT_TB:
        unit_num_bytes = (size_t)1000 * 1000 * 1000 * 1000;
        break;
    default:
        break;
    }

    if (!s->leaf_min_units_size) {
        s->leaf_min_size = unit_num_bytes * KND_SNAPSHOT_LEAF_MIN_THRESHOLD; 
    } else {
        leaf_min_size = unit_num_bytes * s->leaf_min_units_size;

        /* reverse overflow check */
        if (s->leaf_min_units_size != (leaf_min_size / unit_num_bytes))
            return knd_LIMIT;

        s->leaf_min_size = leaf_min_size;
    }

    if (!s->leaf_max_units_size) {
        s->leaf_max_size = unit_num_bytes * KND_SNAPSHOT_LEAF_MAX_THRESHOLD; 
    } else {
        leaf_max_size = unit_num_bytes * s->leaf_max_units_size;

        /* reverse overflow check */
        if (s->leaf_max_units_size != (leaf_max_size / unit_num_bytes))
            return knd_LIMIT;

        s->leaf_max_size = leaf_max_size;
    }
    return knd_OK;
}

static gsl_err_t set_storage_leaf_unit(void *obj, const char *name, size_t name_size)
{
    struct kndStorage *s = obj;

    for (size_t i = 0; i < sizeof knd_storage_unit_names / sizeof knd_storage_unit_names[0]; i++) {
        const char *unit_str = knd_storage_unit_names[i];
        assert(unit_str != NULL);

        size_t unit_str_size = strlen(unit_str);
        if (name_size != unit_str_size) continue;

        if (!memcmp(unit_str, name, name_size)) {
            s->leaf_storage_unit = (knd_storage_unit_type)i;
            return make_gsl_err(gsl_OK);
        }
    }
    return make_gsl_err(gsl_FORMAT);
}

static gsl_err_t parse_storage_leaf(void *obj, const char *rec, size_t *total_size)
{
    struct kndStorage *s = obj;
    gsl_err_t parser_err;
    int err;

    struct gslTaskSpec specs[] = {
        {   .name = "unit",
            .name_size = strlen("unit"),
            .run = set_storage_leaf_unit,
            .obj = obj
        },
        {   .name = "min",
            .name_size = strlen("min"),
            .parse = gsl_parse_size_t,
            .obj = &s->leaf_min_units_size
        },
        {   .name = "max",
            .name_size = strlen("max"),
            .parse = gsl_parse_size_t,
            .obj = &s->leaf_max_units_size
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- storage config parse {error %d} {tag %.*s}", parser_err.code,
                 parser_err.val_size, parser_err.val);
        return parser_err;
    }

    err = set_storage_limits(s);
    if (err) {
        return make_gsl_err(gsl_LIMIT);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_storage_snapshot_threshold(void *obj, const char *val, size_t val_size)
{
    struct kndStorage *store = obj;
    char buf[KND_NUMFIELD_MAX_SIZE + 1] = { 0 };
    long double numval;
    int err;

    if (val_size > KND_NUMFIELD_MAX_SIZE) {
        knd_log("threshold value exceeds current num field limit");
        return make_gsl_err(gsl_FORMAT);
    }
    memcpy(buf, val, val_size);

    err = knd_parse_real(buf, &numval);
    if (err) return make_gsl_err(gsl_FORMAT);

    if (numval <= 0) return make_gsl_err(gsl_FORMAT);
    if (numval > 1) return make_gsl_err(gsl_FORMAT);

    store->snapshot_threshold_ratio = numval;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_storage_snapshot(void *obj, const char *rec, size_t *total_size)
{
    struct gslTaskSpec specs[] = {
        {   .name = "threshold",
            .name_size = strlen("threshold"),
            .run = set_storage_snapshot_threshold,
            .obj = obj
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t set_storage_mode(void *obj, const char *name, size_t name_size)
{
    struct kndStorage *s = obj;

    for (size_t i = 0; i < sizeof knd_storage_mode_names / sizeof knd_storage_mode_names[0]; i++) {
        const char *str = knd_storage_mode_names[i];
        assert(str != NULL);

        size_t str_size = strlen(str);
        if (name_size != str_size) continue;

        if (!memcmp(str, name, name_size)) {
            s->mode = (knd_storage_mode)i;
            return make_gsl_err(gsl_OK);
        }
    }
    return make_gsl_err(gsl_FORMAT);
}

static gsl_err_t set_storage_type(void *obj, const char *name, size_t name_size)
{
    struct kndStorage *s = obj;

    for (size_t i = 0; i < sizeof knd_storage_type_names / sizeof knd_storage_type_names[0]; i++) {
        const char *str = knd_storage_type_names[i];
        assert(str != NULL);

        size_t str_size = strlen(str);
        if (name_size != str_size) continue;

        if (!memcmp(str, name, name_size)) {
            s->type = (knd_storage_type)i;
            return make_gsl_err(gsl_OK);
        }
    }
    return make_gsl_err(gsl_FORMAT);
}

static gsl_err_t parse_storage_item(void *obj, const char *rec, size_t *total_size)
{
    struct kndSteward *steward = obj;
    struct kndStorage *s;
    gsl_err_t parser_err;
    int err;

    err = knd_storage_new(&s);
    if (err) return make_gsl_err_external(err);

    struct gslTaskSpec specs[] = {
       {   .is_implied = true,
           .buf = s->name,
           .buf_size = &s->name_size,
           .max_buf_size = KND_SHORT_NAME_SIZE
       },
       {   .name = "type",
           .name_size = strlen("type"),
           .run = set_storage_type,
           .obj = s
       },
       {   .name = "mode",
           .name_size = strlen("mode"),
           .run = set_storage_mode,
           .obj = s
       },
       {   .name = "quota",
           .name_size = strlen("quota"),
           .parse = parse_storage_quota,
           .obj = s
       },
       {   .name = "leaf",
           .name_size = strlen("leaf"),
           .parse = parse_storage_leaf,
           .obj = s
       },
       {   .name = "snapshot",
           .name_size = strlen("snapshot"),
           .parse = parse_storage_snapshot,
           .obj = s
       }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- config parse {error %d} {tag %.*s}", parser_err.code,
                parser_err.val_size, parser_err.val);
        return parser_err;
    }

    // TODO check storage path

    append_storage(steward, s);

    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_storage_parse_conf(void *obj, const char *rec, size_t *total_size)
{
    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .parse = parse_storage_item,
        .obj = obj
    };
    return gsl_parse_array(&item_spec, rec, total_size);
}

int knd_storage_leaf_export_GSL(struct kndStorageLeaf *leaf, struct kndOutput *out,
                                size_t indent_size, size_t depth,
                                struct kndTask *unused_var(task))
{
    int err;

    OUT("{ ", strlen("{ "));
    OUT(leaf->name, leaf->name_size);
    OUT(" ", 1);
        
    if (leaf->range_from_addr_size) {
        OUT("{from-addr ", strlen("{from-addr "));
        OUT(leaf->range_from_addr, leaf->range_from_addr_size);
        OUT("}", 1);
    }

    if (leaf->range_to_addr_size) {
        OUT("{to-addr ", strlen("{to-addr "));
        OUT(leaf->range_to_addr, leaf->range_to_addr_size);
        OUT("}", 1);
    }

    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_indent(out, (depth + 1) * indent_size);
        RET_ERR();
    }
    OUTF("{num-elems %zu}", leaf->num_elems);

    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_indent(out, (depth + 1) * indent_size);
        RET_ERR();
    }
    OUTF("{file-size %zu}", leaf->curr_size);

    OUT("}", 1);
    return knd_OK;
}
