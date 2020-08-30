#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_mempool.h"
#include "knd_utils.h"
#include "knd_output.h"

void knd_mempool_del(struct kndMemPool *self)
{
    if (self->pages)
        free(self->pages);
    if (self->small_x4_pages)
        free(self->small_x4_pages);
    if (self->small_x2_pages)
        free(self->small_x2_pages);
    if (self->small_pages)
        free(self->small_pages);
    if (self->tiny_pages)
        free(self->tiny_pages);
    free(self);
}

static int present_status(struct kndMemPool *self, struct kndOutput *out)
{
    int err;
    err = out->writef(out, "{base-pages     %zu of %zu {used %.2f%%}}\n",
                      self->pages_used, self->num_pages,
                      (double)self->pages_used / self->num_pages * 100);                      RET_ERR();
    err = out->writef(out, "{small-x4-pages %zu of %zu {used %.2f%%}}\n",
                      self->small_x4_pages_used, self->num_small_x4_pages,
                      (double)self->small_x4_pages_used / self->num_small_x4_pages * 100);    RET_ERR();
    err = out->writef(out, "{small-x2-pages %zu of %zu {used %.2f%%}}\n",
                      self->small_x2_pages_used, self->num_small_x2_pages,
                      (double)self->small_x2_pages_used / self->num_small_x2_pages * 100);    RET_ERR();
    err = out->writef(out, "{small-pages    %zu of %zu {used %.2f%%}}\n",
                      self->small_pages_used, self->num_small_pages,
                      (double)self->small_pages_used / self->num_small_pages * 100);          RET_ERR();
    err = out->writef(out, "{tiny-pages     %zu of %zu {used %.2f%%}}\n",
                      self->tiny_pages_used, self->num_tiny_pages,
                      (double)self->tiny_pages_used / self->num_tiny_pages * 100);            RET_ERR();
    return knd_OK;
}

int knd_mempool_alloc(struct kndMemPool *self, knd_mempage_t page_type, size_t obj_size, void **result)
{
    struct kndMemPageHeader **page_list;
    size_t page_size, num_pages, *pages_used;

    assert(self->type == KND_ALLOC_LIST);
    
    switch (page_type) {
    case KND_MEMPAGE_BASE:
        page_size = self->page_size;
        num_pages = self->num_pages;
        pages_used = &self->pages_used;
        page_list = &self->page_list;
        break;
    case KND_MEMPAGE_SMALL_X4:
        page_size = self->small_x4_page_size;
        num_pages = self->num_small_x4_pages;
        pages_used = &self->small_x4_pages_used;
        page_list = &self->small_x4_page_list;
        break;
    case KND_MEMPAGE_SMALL_X2:
        page_size = self->small_x2_page_size;
        num_pages = self->num_small_x2_pages;
        pages_used = &self->small_x2_pages_used;
        page_list = &self->small_x2_page_list;
        break;
    case KND_MEMPAGE_SMALL:
        page_size = self->small_page_size;
        num_pages = self->num_small_pages;
        pages_used = &self->small_pages_used;
        page_list = &self->small_page_list;
        break;
    case KND_MEMPAGE_TINY:
        page_size = self->tiny_page_size;
        num_pages = self->num_tiny_pages;
        pages_used = &self->tiny_pages_used;
        page_list = &self->tiny_page_list;
        break;
    default:
        page_size = self->page_size;
        num_pages = self->num_pages;
        pages_used = &self->pages_used;
        page_list = &self->page_list;
        break;
    }

    assert(page_size >= obj_size);

    //if (obj_size <= (page_size * 0.75) && obj_size > 64) {
        //knd_log("-- too large mem page requested: %zu  max size:%zu",
        //        obj_size, page_size);
        //return knd_LIMIT;  // FIXME(k15tfu): temporarily disabled
    //}

    if (*pages_used + 1 > num_pages) {
        knd_log("mem limit reached: max pages:%zu [%d] capacity:%zu",
                num_pages, page_type, self->capacity);
        return knd_LIMIT;
    }

    assert(*page_list != NULL);

    *result = *page_list;
    *page_list = (*page_list)->next;
    (*pages_used)++;
    memset(*result, 0, obj_size);  // FIXME(k15tfu): don't initialize the memory

    return knd_OK;
}

int knd_mempool_incr_alloc(struct kndMemPool *self, knd_mempage_t page_type, size_t obj_size, void **result)
{
    size_t page_size, num_pages, *pages_used;
    char *pages;
    size_t offset;
    char *c;

    assert(self->type == KND_ALLOC_INCR);

    switch (page_type) {
    case KND_MEMPAGE_BASE:
        page_size = self->page_size;
        num_pages = self->num_pages;
        pages_used = &self->pages_used;
        pages = self->pages;
        break;
    case KND_MEMPAGE_SMALL_X4:
        page_size = self->small_x4_page_size;
        num_pages = self->num_small_x4_pages;
        pages_used = &self->small_x4_pages_used;
        pages = self->small_x4_pages;
        break;
    case KND_MEMPAGE_SMALL_X2:
        page_size = self->small_x2_page_size;
        num_pages = self->num_small_x2_pages;
        pages_used = &self->small_x2_pages_used;
        pages = self->small_x2_pages;
        break;
    case KND_MEMPAGE_SMALL:
        page_size = self->small_page_size;
        num_pages = self->num_small_pages;
        pages_used = &self->small_pages_used;
        pages = self->small_pages;
        break;
    case KND_MEMPAGE_TINY:
        page_size = self->tiny_page_size;
        num_pages = self->num_tiny_pages;
        pages_used = &self->tiny_pages_used;
        pages = self->tiny_pages;
        break;
    default:
        page_size = self->page_size;
        num_pages = self->num_pages;
        pages_used = &self->pages_used;
        pages = self->pages;
        break;
    }

    assert(page_size >= obj_size);

    //if (obj_size <= (page_size * 0.75) && obj_size > 64) {
        //knd_log("-- too large mem page requested: %zu  max size:%zu",
        //        obj_size, page_size);
        //return knd_LIMIT;  // FIXME(k15tfu): temporarily disabled
    //}

    if (*pages_used + 1 > num_pages) {
        knd_log("-- mem limit reached: max pages:%zu [%d] capacity:%zu",
                num_pages, page_type, self->capacity);
        return knd_LIMIT;
    }

    offset = page_size * (*pages_used);
    c = pages + offset;
    memset(c, 0, obj_size);

    *result = c;
    (*pages_used)++;

    return knd_OK;
}

void knd_mempool_reset(struct kndMemPool *self)
{
    self->pages_used = 0;    
    self->small_x4_pages_used = 0;
    self->small_x2_pages_used = 0;
    self->small_pages_used = 0;
    self->small_pages_used = 0;    
    self->tiny_pages_used = 0;    
}

void knd_mempool_free(struct kndMemPool *self, knd_mempage_t page_type, void *page_data)
{
    struct kndMemPageHeader **page_list, *freed = page_data;
    size_t *pages_used;

    switch (page_type) {
        case KND_MEMPAGE_BASE:
            pages_used = &self->pages_used;
            page_list = &self->page_list;
            break;
        case KND_MEMPAGE_SMALL_X4:
            pages_used = &self->small_x4_pages_used;
            page_list = &self->small_x4_page_list;
            break;
        case KND_MEMPAGE_SMALL_X2:
            pages_used = &self->small_x2_pages_used;
            page_list = &self->small_x2_page_list;
            break;
        case KND_MEMPAGE_SMALL:
            pages_used = &self->small_pages_used;
            page_list = &self->small_page_list;
            break;
        case KND_MEMPAGE_TINY:
            pages_used = &self->tiny_pages_used;
            page_list = &self->tiny_page_list;
            break;
        default:
            pages_used = &self->pages_used;
            page_list = &self->page_list;
            break;
    }

    //knd_log(".. free page:%p", page_data);
    freed->next = *page_list;
    *page_list = freed;
    (*pages_used)--;
}

static int alloc_page_buf(struct kndMemPool *self,
                          char **result_pages,
                          size_t *result_num_pages, size_t default_num_pages,
                          size_t *result_page_size, size_t default_page_size)
{
    size_t num_pages = *result_num_pages;
    size_t page_size = *result_page_size;
    char *pages;

    if (!num_pages)
        num_pages = default_num_pages;
    if (!page_size)
        page_size = default_page_size;

    if (page_size <= sizeof(struct kndMemPageHeader)) return knd_LIMIT;

    pages = calloc(num_pages, page_size);
    if (!pages) {
        knd_log("-- mem pages not allocated");
        return knd_NOMEM;
    }
    self->capacity += (num_pages * page_size);

    // knd_log("++ mempool %p: alloc'd %zu pages of %zu size, total capacity:%zu",
    //        self, num_pages, page_size, self->capacity);

    *result_pages = pages;
    *result_page_size = page_size;
    *result_num_pages = num_pages;
    return knd_OK;
}

static void build_linked_list(char *pages, size_t num_pages, size_t page_size, struct kndMemPageHeader **page_list)
{
    for (size_t i = 0; i < num_pages; i++) {
        *page_list = (struct kndMemPageHeader *)pages;

        pages += page_size;
        page_list = &(*page_list)->next;
    }
    *page_list = NULL;
}

static int reset_capacity(struct kndMemPool *self)
{
    memset(self->pages, 0, self->page_size * self->num_pages);
    build_linked_list(self->pages, self->num_pages, self->page_size, &self->page_list);

    memset(self->small_x4_pages, 0, self->small_x4_page_size * self->num_small_x4_pages);
    build_linked_list(self->small_x4_pages, self->num_small_x4_pages, self->small_x4_page_size, &self->small_x4_page_list);

    memset(self->small_x2_pages, 0, self->small_x2_page_size * self->num_small_x2_pages);
    build_linked_list(self->small_x2_pages, self->num_small_x2_pages, self->small_x2_page_size, &self->small_x2_page_list);

    memset(self->small_pages, 0, self->small_page_size * self->num_small_pages);
    build_linked_list(self->small_pages, self->num_small_pages, self->small_page_size, &self->small_page_list);

    memset(self->tiny_pages, 0, self->tiny_page_size * self->num_tiny_pages);
    build_linked_list(self->tiny_pages, self->num_tiny_pages, self->tiny_page_size, &self->tiny_page_list);

    return knd_OK;
}

static int alloc_capacity(struct kndMemPool *self)
{
    int err;

    err = alloc_page_buf(self,
                         &self->pages, &self->num_pages,
                         KND_NUM_BASE_MEMPAGES,
                         &self->page_size, KND_BASE_MEMPAGE_SIZE);                RET_ERR();

    err = alloc_page_buf(self,
                         &self->small_x4_pages, &self->num_small_x4_pages,
                         KND_NUM_SMALL_X4_MEMPAGES,
                         &self->small_x4_page_size, KND_SMALL_X4_MEMPAGE_SIZE);   RET_ERR();

    err = alloc_page_buf(self,
                         &self->small_x2_pages, &self->num_small_x2_pages,
                         KND_NUM_SMALL_X2_MEMPAGES,
                         &self->small_x2_page_size, KND_SMALL_X2_MEMPAGE_SIZE);   RET_ERR();

    err = alloc_page_buf(self,
                         &self->small_pages, &self->num_small_pages,
                         KND_NUM_SMALL_MEMPAGES,
                         &self->small_page_size, KND_SMALL_MEMPAGE_SIZE);         RET_ERR();

    err = alloc_page_buf(self,
                         &self->tiny_pages, &self->num_tiny_pages,
                         KND_NUM_TINY_MEMPAGES,
                         &self->tiny_page_size, KND_TINY_MEMPAGE_SIZE);           RET_ERR();

    // knd_log("== MemPool total bytes alloc'd: %zu", self->capacity);

    if (self->type != KND_ALLOC_LIST) return knd_OK;

    build_linked_list(self->pages, self->num_pages, self->page_size, &self->page_list);

    build_linked_list(self->small_x4_pages, self->num_small_x4_pages, self->small_x4_page_size, &self->small_x4_page_list);
    
    build_linked_list(self->small_x2_pages, self->num_small_x2_pages, self->small_x2_page_size, &self->small_x2_page_list);

    build_linked_list(self->small_pages, self->num_small_pages, self->small_page_size, &self->small_page_list);
    
    build_linked_list(self->tiny_pages, self->num_tiny_pages, self->tiny_page_size, &self->tiny_page_list);

    return knd_OK;
}

static gsl_err_t
parse_memory_settings(struct kndMemPool *self, const char *rec, size_t *total_size)
{
    struct gslTaskSpec specs[] = {
        {   .name = "max_base_pages",
            .name_size = strlen("max_base_pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->num_pages
        },
        {   .name = "max_small_x4_pages",
            .name_size = strlen("max_small_x4_pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->num_small_x4_pages
        },
        {   .name = "max_small_x2_pages",
            .name_size = strlen("max_small_x2_pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->num_small_x2_pages
        },
        {   .name = "max_small_pages",
            .name_size = strlen("max_small_pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->num_small_pages
        },
        {   .name = "max_tiny_pages",
            .name_size = strlen("max_tiny_pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->num_tiny_pages
        },
        {   .name = "max_set_size",
            .name_size = strlen("max_set_size"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_set_size
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

void kndMemPool_init(struct kndMemPool *self)
{
    self->parse = parse_memory_settings;
    self->alloc = alloc_capacity;
    self->reset = reset_capacity;
    self->present = present_status;
}

int knd_mempool_new(struct kndMemPool **obj, int mempool_id)
{
    struct kndMemPool *self;

    self = malloc(sizeof(struct kndMemPool));
    if (!self) return knd_NOMEM;
    memset(self, 0, sizeof(struct kndMemPool));
    self->id = mempool_id;

    kndMemPool_init(self);
    *obj = self;
    return knd_OK;
}
