#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>

#include "knd_quant.h"
#include "knd_repo.h"
#include "knd_task.h"
#include "knd_user.h"
#include "knd_utils.h"

#define DEBUG_NUM_LEVEL_0 0
#define DEBUG_NUM_LEVEL_1 0
#define DEBUG_NUM_LEVEL_2 0
#define DEBUG_NUM_LEVEL_3 0
#define DEBUG_NUM_LEVEL_TMP 1

void append_quant_hash_spec(struct kndQuantAttr *attr, struct kndFacetHashSpec *spec)
{
    if (attr->hash_specs_tail) {
        attr->hash_specs_tail->next = spec;
        attr->hash_specs_tail = spec;
    } else {
        attr->hash_specs = spec;   
        attr->hash_specs_tail = spec;   
    }
    attr->num_hash_specs++;    
}

static gsl_err_t set_calc(void *obj, const char *unused_var(val), size_t val_size)
{
    struct kndQuantAttr *self = obj;
    if (!val_size) return make_gsl_err(gsl_FORMAT);
    if (val_size >= KND_VAL_SIZE) return make_gsl_err(gsl_LIMIT);

    self->is_calculated = true;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_calc_setting(void *obj, const char *rec, size_t *total_size)
{
    struct kndQuantAttr *self = obj;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_calc,
          .obj = self
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

int knd_quant_attr_setting_import(struct kndQuantAttr *self, const char *name, size_t name_size,
                                  const char *rec, size_t *total_size, struct kndTask *task)
{
    size_t num_settings = sizeof(knd_quant_attr_setting_names) /
        sizeof(knd_quant_attr_setting_names[0]);
    const char *c;
    gsl_err_t parser_err;
    knd_quant_attr_setting quant_setting_type = 0;

    if (DEBUG_NUM_LEVEL_2) {
        knd_log(".. import quant attr {setting %.*s}", name_size, name);
    }

    for (size_t i = 0; i < num_settings; i++) {
        c = knd_quant_attr_setting_names[i];
        if (name_size != strlen(c)) continue;
        if (!memcmp(c, name, name_size)) {
            quant_setting_type = (knd_quant_attr_setting)i;
            break;
        }
    }

    switch (quant_setting_type) {
    case KND_QUANT_ATTR_NONE:
        KND_TASK_LOG("{quant-attr-setting %.*s} is not supported for {quant-attr %.*s}",
                     name_size, name, self->name_size, self->name);
        return knd_NO_MATCH;
    case KND_QUANT_ATTR_CALC:
        parser_err = parse_calc_setting(self, rec, total_size);
        if (parser_err.code) {
            return gsl_err_to_knd_err_codes(parser_err);
        }
        break;
    }
    return knd_OK;
}

int knd_quant_parse_uint(const char *val, size_t val_size,
                         struct kndQuantUInt **result, struct kndTask *task)
{
    struct kndQuantUInt *uint;
    char buf[KND_UINT_DEC_SEQ_SIZE];
    size_t buf_size = 0;
    long numval;
    int err;

    if (val_size >= KND_UINT_DEC_SEQ_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("unsigned integer decimal representation limit exceeded");
    }
    memcpy(buf, val, val_size);
    buf_size = val_size;
    buf[buf_size] = '\0';

    err = knd_parse_int(buf, &numval);
    KND_TASK_ERR("failed to parse unsigned integer value");

    if (numval < 0) {
        err = knd_FORMAT;
        KND_TASK_ERR("unsigned integer can not be negative");
    }

    err = knd_quant_uint_new(&uint, task->mempool);
    KND_TASK_ERR("failed to alloc a uint");
    uint->numval = numval;

    knd_num_to_str(numval, uint->seq, &uint->seq_size,  KND_RADIX_BASE);

    if (DEBUG_NUM_LEVEL_3) {
        knd_log("{uint %lu} => {seq %.*s}", numval, uint->seq_size, uint->seq);
    }
    *result = uint;
    return knd_OK;
}

int knd_quant_parse_ureal(const char *val, size_t val_size,
                         struct kndQuantUReal **result, struct kndTask *task)
{
    struct kndQuantUReal *ureal;
    char buf[KND_UREAL_DEC_SEQ_SIZE];
    size_t buf_size = 0;
    long double numval;
    int err;

    if (val_size >= KND_UREAL_DEC_SEQ_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("unsigned real decimal representation limit exceeded");
    }
    memcpy(buf, val, val_size);
    buf_size = val_size;
    buf[buf_size] = '\0';

    err = knd_parse_real(buf, &numval);
    KND_TASK_ERR("failed to parse unsigned real value");

    if (numval < 0.0) {
        err = knd_FORMAT;
        KND_TASK_ERR("unsigned real value can not be negative");
    }

    err = knd_quant_ureal_new(&ureal, task->mempool);
    KND_TASK_ERR("failed to alloc a ureal");
    ureal->numval = numval;

    //knd_num_to_str(numval, ureal->seq, &ureal->seq_size,  KND_RADIX_BASE);

    if (DEBUG_NUM_LEVEL_2) {
        knd_log("{ureal %.2Lf} => {seq %.*s}", numval, ureal->seq_size, ureal->seq);
    }
    *result = ureal;
    return knd_OK;
}

int knd_quant_attr_new(struct kndQuantAttr **result, knd_quant_type type,
                       const char *name, size_t name_size, struct kndMemPool *mempool)
{
    void *page;
    struct kndQuantAttr *quant_attr;
    struct kndFacetHashSpec *spec;
    int err;

    assert(mempool->tiny_page_size >= sizeof(struct kndQuantAttr));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndQuantAttr));
    quant_attr = page;
    quant_attr->type = type;
    quant_attr->name = name;
    quant_attr->name_size = name_size;

    err = knd_facet_hash_spec_new(&spec, KND_FACET_LEN,
                                  knd_quant_seq_len_key_get,
                                  NULL,
                                  knd_quant_seq_len_key_str,
                                  knd_quant_seq_len_hash, mempool);
    if (err) return err;
    append_quant_hash_spec(quant_attr, spec);

    err = knd_facet_hash_spec_new(&spec, KND_FACET_SUM,
                                  knd_quant_seq_len_key_get,
                                  NULL,
                                  knd_quant_str,
                                  knd_quant_hash, mempool);
    if (err) return err;
    append_quant_hash_spec(quant_attr, spec);

    *result = quant_attr;
    return knd_OK;
}

int knd_quant_uint_new(struct kndQuantUInt **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->small_page_size >= sizeof(struct kndQuantUInt));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndQuantUInt));
    *result = page;
    return knd_OK;
}

int knd_quant_uint_range_new(struct kndQuantUIntRange **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndQuantUIntRange));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndQuantUIntRange));
    *result = page;
    return knd_OK;
}

int knd_quant_attr_stm_new(struct kndQuantAttrStm **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndQuantAttrStm));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndQuantAttrStm));
    *result = page;
    return knd_OK;
}

int knd_quant_uint_facet_new(struct kndQuantUIntFacet **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndQuantUIntFacet));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndQuantUIntFacet));
    *result = page;
    return knd_OK;
}

int knd_quant_ureal_new(struct kndQuantUReal **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->small_x2_page_size >= sizeof(struct kndQuantUReal));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL_X2, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndQuantUReal));
    *result = page;
    return knd_OK;
}

int knd_quant_new(struct kndQuant **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->small_page_size >= sizeof(struct kndQuant));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndQuant));
    *result = page;
    return knd_OK;
}
