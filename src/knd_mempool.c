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

static int get_shared_page(struct kndMemPool *self, knd_mempage_t page_type, void **result)
{
    struct kndMemPageHeader *page_list, *next_page;
    switch (page_type) {
    case KND_MEMPAGE_SMALL_X4:
        do {
            page_list = atomic_load_explicit(&self->shared_small_x4_page_list, memory_order_relaxed);
            if (!page_list) return knd_NOMEM;
            next_page = page_list->next;
        }
        while (!atomic_compare_exchange_weak(&self->shared_small_x4_page_list, &page_list, next_page));
        atomic_fetch_add_explicit(&self->shared_small_x4_pages_used, 1, memory_order_relaxed);
        break;
    case KND_MEMPAGE_SMALL_X2:
        do {
            page_list = atomic_load_explicit(&self->shared_small_x2_page_list, memory_order_relaxed);
            if (!page_list) return knd_NOMEM;
            next_page = page_list->next;
        }
        while (!atomic_compare_exchange_weak(&self->shared_small_x2_page_list, &page_list, next_page));
        atomic_fetch_add_explicit(&self->shared_small_x2_pages_used, 1, memory_order_relaxed);
        break;
    case KND_MEMPAGE_SMALL:
        do {
            page_list = atomic_load_explicit(&self->shared_small_page_list, memory_order_relaxed);
            if (!page_list) return knd_NOMEM;
            next_page = page_list->next;
        }
        while (!atomic_compare_exchange_weak(&self->shared_small_page_list, &page_list, next_page));
        atomic_fetch_add_explicit(&self->shared_small_pages_used, 1, memory_order_relaxed);
        break;
    case KND_MEMPAGE_TINY:
        do {
            page_list = atomic_load_explicit(&self->shared_tiny_page_list, memory_order_relaxed);
            if (!page_list) return knd_NOMEM;
            next_page = page_list->next;
        }
        while (!atomic_compare_exchange_weak(&self->shared_tiny_page_list, &page_list, next_page));
        atomic_fetch_add_explicit(&self->shared_tiny_pages_used, 1, memory_order_relaxed);
        break;
    default:
        // KND_MEMPAGE_BASE
        do {
            page_list = atomic_load_explicit(&self->shared_page_list, memory_order_relaxed);
            if (!page_list) return knd_NOMEM;
            next_page = page_list->next;
        }
        while (!atomic_compare_exchange_weak(&self->shared_page_list, &page_list, next_page));
        atomic_fetch_add_explicit(&self->shared_pages_used, 1, memory_order_relaxed);
        break;
    }
    *result = page_list;
    return knd_OK;
}

static int get_page(struct kndMemPool *self, knd_mempage_t page_type, void **result)
{
    struct kndMemPageHeader **page_list;
    size_t *pages_used;

    switch (page_type) {
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
        // KND_MEMPAGE_BASE
        pages_used = &self->pages_used;
        page_list = &self->page_list;
        break;
    }
    if (*page_list == NULL)
        return knd_NOMEM;
    *result = *page_list;
    *page_list = (*page_list)->next;
    (*pages_used)++;
    return knd_OK;
}

int knd_mempool_page(struct kndMemPool *self, knd_mempage_t page_type, void **result)
{
    switch (self->type) {
    case KND_ALLOC_SHARED:
        return get_shared_page(self, page_type, result);
    case KND_ALLOC_LIST:
        return get_page(self, page_type, result);
    default:
        // KND_ALLOC_INCR
        break;
    }
    size_t offset, num_pages, page_size, *pages_used;
    char *pages, *c;

    switch (page_type) {
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
        // KND_MEMPAGE_BASE
        page_size = self->page_size;
        num_pages = self->num_pages;
        pages_used = &self->pages_used;
        pages = self->pages;
        break;
    }
    if (*pages_used + 1 > num_pages)
        return knd_NOMEM;
    offset = page_size * (*pages_used);
    c = pages + offset;
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
    freed->next = *page_list;
    *page_list = freed;
    (*pages_used)--;
}

static int alloc_page_buf(struct kndMemPool *self, char **result_pages,
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

    *result_pages = pages;
    *result_page_size = page_size;
    *result_num_pages = num_pages;
    return knd_OK;
}

static void build_linked_list(char *pages, size_t num_pages, size_t page_size, struct kndMemPageHeader **page_list)
{
    for (size_t i = 0; i < num_pages; i++) {
        *page_list = (struct kndMemPageHeader*)pages;

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

    err = alloc_page_buf(self, &self->pages, &self->num_pages, KND_NUM_BASE_MEMPAGES,
                         &self->page_size, KND_BASE_MEMPAGE_SIZE);                RET_ERR();

    err = alloc_page_buf(self, &self->small_x4_pages, &self->num_small_x4_pages, KND_NUM_SMALL_X4_MEMPAGES,
                         &self->small_x4_page_size, KND_SMALL_X4_MEMPAGE_SIZE);   RET_ERR();

    err = alloc_page_buf(self, &self->small_x2_pages, &self->num_small_x2_pages, KND_NUM_SMALL_X2_MEMPAGES,
                         &self->small_x2_page_size, KND_SMALL_X2_MEMPAGE_SIZE);   RET_ERR();

    err = alloc_page_buf(self, &self->small_pages, &self->num_small_pages, KND_NUM_SMALL_MEMPAGES,
                         &self->small_page_size, KND_SMALL_MEMPAGE_SIZE);         RET_ERR();

    err = alloc_page_buf(self, &self->tiny_pages, &self->num_tiny_pages, KND_NUM_TINY_MEMPAGES,
                         &self->tiny_page_size, KND_TINY_MEMPAGE_SIZE);           RET_ERR();

    switch (self->type) {
    case KND_ALLOC_SHARED:
        // fall through
    case KND_ALLOC_LIST:
        build_linked_list(self->pages, self->num_pages, self->page_size, &self->page_list);
        build_linked_list(self->small_x4_pages, self->num_small_x4_pages, self->small_x4_page_size, &self->small_x4_page_list);
        build_linked_list(self->small_x2_pages, self->num_small_x2_pages, self->small_x2_page_size, &self->small_x2_page_list);
        build_linked_list(self->small_pages, self->num_small_pages, self->small_page_size, &self->small_page_list);
        build_linked_list(self->tiny_pages, self->num_tiny_pages, self->tiny_page_size, &self->tiny_page_list);
        break;
    default:
        break;
    }

    if (self->type == KND_ALLOC_SHARED) {
        atomic_store_explicit(&self->shared_page_list, self->page_list, memory_order_relaxed);
        atomic_store_explicit(&self->shared_tiny_page_list, self->tiny_page_list, memory_order_relaxed);
        atomic_store_explicit(&self->shared_small_page_list, self->small_page_list, memory_order_relaxed);
        atomic_store_explicit(&self->shared_small_x2_page_list, self->small_x2_page_list, memory_order_relaxed);
        atomic_store_explicit(&self->shared_small_x4_page_list, self->small_x4_page_list, memory_order_relaxed);
    }
    return knd_OK;
}

static gsl_err_t parse_memory_settings(struct kndMemPool *self, const char *rec, size_t *total_size)
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

static void mempool_init(struct kndMemPool *self)
{
    self->parse = parse_memory_settings;
    self->alloc = alloc_capacity;
    self->reset = reset_capacity;
    self->present = present_status;
}

int knd_mempool_new(struct kndMemPool **obj, knd_mempool_t type, int mempool_id)
{
    struct kndMemPool *self;
    self = malloc(sizeof(struct kndMemPool));
    if (!self) return knd_NOMEM;
    memset(self, 0, sizeof(struct kndMemPool));
    self->type = type;
    self->id = mempool_id;
    mempool_init(self);
    *obj = self;
    return knd_OK;
}
