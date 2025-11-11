#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_mempool.h"
#include "knd_utils.h"
#include "knd_output.h"

void knd_mempool_del(struct kndMemPool *self)
{
    if (self->large_pages)
        free(self->large_pages);
    if (self->base_pages)
        free(self->base_pages);
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

void knd_mempool_report(struct kndMemPool *self, struct kndMemPoolReport *report)
{
    size_t large_pages_used = self->large_pages_used;
    size_t base_pages_used = self->base_pages_used;
    size_t tiny_pages_used = self->tiny_pages_used;
    size_t small_pages_used = self->small_pages_used;
    size_t small_x2_pages_used = self->small_x2_pages_used;
    size_t small_x4_pages_used = self->small_x4_pages_used;

    switch (self->type) {
    case KND_ALLOC_SHARED:
        large_pages_used =\
            atomic_load_explicit(&self->shared_large_pages_used, memory_order_relaxed);
        base_pages_used =\
            atomic_load_explicit(&self->shared_base_pages_used, memory_order_relaxed);
        tiny_pages_used =\
            atomic_load_explicit(&self->shared_tiny_pages_used, memory_order_relaxed);
        small_pages_used =\
            atomic_load_explicit(&self->shared_small_pages_used, memory_order_relaxed);
        small_x2_pages_used =\
            atomic_load_explicit(&self->shared_small_x2_pages_used, memory_order_relaxed);
        small_x4_pages_used =\
            atomic_load_explicit(&self->shared_small_x4_pages_used, memory_order_relaxed);
        break;
    default:
        break;
    }
    report->total_mem_usage =  (large_pages_used * KND_LARGE_MEMPAGE_SIZE) +
        (base_pages_used * KND_BASE_MEMPAGE_SIZE) +
        (tiny_pages_used * KND_TINY_MEMPAGE_SIZE) +
        (small_pages_used * KND_SMALL_MEMPAGE_SIZE) +
        (small_x2_pages_used * KND_SMALL_X2_MEMPAGE_SIZE) +
        (small_x4_pages_used * KND_SMALL_X4_MEMPAGE_SIZE);

    report->max_mem_usage = self->capacity;
}

int knd_mempool_present(struct kndMemPool *self, struct kndOutput *out)
{
    size_t total_mem_usage = 0;
    size_t large_pages_used = self->large_pages_used;
    size_t num_large_pages = self->num_large_pages;
    size_t base_pages_used = self->base_pages_used;
    size_t num_base_pages = self->num_base_pages;
    size_t tiny_pages_used = self->tiny_pages_used;
    size_t num_tiny_pages = self->num_tiny_pages;
    size_t small_pages_used = self->small_pages_used;
    size_t num_small_pages = self->num_small_pages;
    size_t small_x2_pages_used = self->small_x2_pages_used;
    size_t num_small_x2_pages = self->num_small_x2_pages;
    size_t small_x4_pages_used = self->small_x4_pages_used;
    size_t num_small_x4_pages = self->num_small_x4_pages;
    bool usage_alert = false;

    OUTF("{mempool %p {type %d}\n", self, self->type);

    switch (self->type) {
    case KND_ALLOC_SHARED:
        large_pages_used = self->shared_large_pages_used;
        base_pages_used = self->shared_base_pages_used;
        tiny_pages_used = self->shared_tiny_pages_used;
        small_pages_used = self->shared_small_pages_used;
        small_x2_pages_used = self->shared_small_x2_pages_used;
        small_x4_pages_used = self->shared_small_x4_pages_used;
        break;
    default:
        break;
    }
    total_mem_usage = (large_pages_used * KND_LARGE_MEMPAGE_SIZE) +
        (base_pages_used * KND_BASE_MEMPAGE_SIZE) +
        (tiny_pages_used * KND_TINY_MEMPAGE_SIZE) +
        (small_pages_used * KND_SMALL_MEMPAGE_SIZE) +
        (small_x2_pages_used * KND_SMALL_X2_MEMPAGE_SIZE) +
        (small_x4_pages_used * KND_SMALL_X4_MEMPAGE_SIZE);

    if (total_mem_usage > (self->capacity * (float)KND_SNAPSHOT_MEM_THRESHOLD_RATIO)) {
        knd_log("{max-capacity %zu {threshold %zu}}",
                self->capacity, self->capacity * (float)KND_SNAPSHOT_MEM_THRESHOLD_RATIO);
        usage_alert = true;
    }

    OUTF("{large-pages     %zu of %zu {used %.2f%%}}\n",
         large_pages_used, num_large_pages,
         (double)large_pages_used / num_large_pages * 100);
    OUTF("{base-pages     %zu of %zu {used %.2f%%}}\n",
         base_pages_used, num_base_pages,
         (double)base_pages_used / num_base_pages * 100);
    OUTF("{small-x4-pages %zu of %zu {used %.2f%%}}\n",
         small_x4_pages_used, num_small_x4_pages,
         (double)small_x4_pages_used / num_small_x4_pages * 100);
    OUTF("{small-x2-pages %zu of %zu {used %.2f%%}}\n",
         small_x2_pages_used, num_small_x2_pages,
         (double)small_x2_pages_used / num_small_x2_pages * 100);
    OUTF("{small-pages    %zu of %zu {used %.2f%%}}\n",
         small_pages_used, num_small_pages,
         (double)small_pages_used / num_small_pages * 100);
    OUTF("{tiny-pages     %zu of %zu {used %.2f%%}}\n",
         tiny_pages_used, num_tiny_pages,
         (double)tiny_pages_used / num_tiny_pages * 100);
    OUTF("{total %.2fM {max %.2fM} {threshold %.2f {usage-alert %d}}}\n",
         (double)total_mem_usage / (1024 * 1024),
         (double)self->capacity / (1024 * 1024),
         KND_SNAPSHOT_MEM_THRESHOLD_RATIO, usage_alert);

    return knd_OK;
}

static int get_shared_page(struct kndMemPool *self, knd_mempage_t page_type, void **result)
{
    struct kndMemPageHeader *page_list, *next_page;
    switch (page_type) {
    case KND_MEMPAGE_LARGE:
        do {
            page_list = atomic_load_explicit(&self->shared_large_page_list, memory_order_relaxed);
            if (!page_list) return knd_NOMEM;
            next_page = page_list->next;
        }
        while (!atomic_compare_exchange_weak(&self->shared_large_page_list, &page_list, next_page));
        atomic_fetch_add_explicit(&self->shared_large_pages_used, 1, memory_order_relaxed);
        break;
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
            page_list = atomic_load_explicit(&self->shared_base_page_list, memory_order_relaxed);
            if (!page_list) return knd_NOMEM;
            next_page = page_list->next;
        }
        while (!atomic_compare_exchange_weak(&self->shared_base_page_list, &page_list, next_page));
        atomic_fetch_add_explicit(&self->shared_base_pages_used, 1, memory_order_relaxed);
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
    case KND_MEMPAGE_LARGE:
        pages_used = &self->large_pages_used;
        page_list = &self->large_page_list;
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
        // KND_MEMPAGE_BASE
        pages_used = &self->base_pages_used;
        page_list = &self->base_page_list;
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
    case KND_MEMPAGE_LARGE:
        page_size = self->large_page_size;
        num_pages = self->num_large_pages;
        pages_used = &self->large_pages_used;
        pages = self->large_pages;
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
        // KND_MEMPAGE_BASE
        page_size = self->base_page_size;
        num_pages = self->num_base_pages;
        pages_used = &self->base_pages_used;
        pages = self->base_pages;
        break;
    }
    if (*pages_used + 1 > num_pages) {
        return knd_NOMEM;
    }
    offset = page_size * (*pages_used);
    c = pages + offset;
    *result = c;
    (*pages_used)++;
    return knd_OK;
}

void knd_mempool_reset(struct kndMemPool *self)
{
    self->large_pages_used = 0;    
    self->base_pages_used = 0;    
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
        case KND_MEMPAGE_LARGE:
            pages_used = &self->large_pages_used;
            page_list = &self->large_page_list;
            break;
        case KND_MEMPAGE_BASE:
            pages_used = &self->base_pages_used;
            page_list = &self->base_page_list;
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
            pages_used = &self->base_pages_used;
            page_list = &self->base_page_list;
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

static void build_linked_list(char *pages, size_t num_pages, size_t page_size,
                              struct kndMemPageHeader **page_list)
{
    for (size_t i = 0; i < num_pages; i++) {
        *page_list = (struct kndMemPageHeader*)pages;

        pages += page_size;
        page_list = &(*page_list)->next;
    }
    *page_list = NULL;
}

void knd_mempool_reset_capacity(struct kndMemPool *self)
{
    memset(self->large_pages, 0, self->large_page_size * self->num_large_pages);
    build_linked_list(self->large_pages, self->num_large_pages, self->large_page_size, &self->large_page_list);

    memset(self->base_pages, 0, self->base_page_size * self->num_base_pages);
    build_linked_list(self->base_pages, self->num_base_pages, self->base_page_size, &self->base_page_list);

    memset(self->small_x4_pages, 0, self->small_x4_page_size * self->num_small_x4_pages);
    build_linked_list(self->small_x4_pages, self->num_small_x4_pages, self->small_x4_page_size, &self->small_x4_page_list);

    memset(self->small_x2_pages, 0, self->small_x2_page_size * self->num_small_x2_pages);
    build_linked_list(self->small_x2_pages, self->num_small_x2_pages, self->small_x2_page_size, &self->small_x2_page_list);

    memset(self->small_pages, 0, self->small_page_size * self->num_small_pages);
    build_linked_list(self->small_pages, self->num_small_pages, self->small_page_size, &self->small_page_list);

    memset(self->tiny_pages, 0, self->tiny_page_size * self->num_tiny_pages);
    build_linked_list(self->tiny_pages, self->num_tiny_pages, self->tiny_page_size, &self->tiny_page_list);
}

int knd_mempool_alloc(struct kndMemPool *self)
{
    int err;

    err = alloc_page_buf(self, &self->large_pages, &self->num_large_pages, KND_NUM_LARGE_MEMPAGES,
                         &self->large_page_size, KND_LARGE_MEMPAGE_SIZE);                RET_ERR();

    err = alloc_page_buf(self, &self->base_pages, &self->num_base_pages, KND_NUM_BASE_MEMPAGES,
                         &self->base_page_size, KND_BASE_MEMPAGE_SIZE);                RET_ERR();

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
        build_linked_list(self->large_pages, self->num_large_pages, self->large_page_size, &self->large_page_list);
        build_linked_list(self->base_pages, self->num_base_pages, self->base_page_size, &self->base_page_list);
        build_linked_list(self->small_x4_pages, self->num_small_x4_pages, self->small_x4_page_size, &self->small_x4_page_list);
        build_linked_list(self->small_x2_pages, self->num_small_x2_pages, self->small_x2_page_size, &self->small_x2_page_list);
        build_linked_list(self->small_pages, self->num_small_pages, self->small_page_size, &self->small_page_list);
        build_linked_list(self->tiny_pages, self->num_tiny_pages, self->tiny_page_size, &self->tiny_page_list);
        break;
    default:
        break;
    }

    if (self->type == KND_ALLOC_SHARED) {
        atomic_store_explicit(&self->shared_large_page_list, self->large_page_list, memory_order_relaxed);
        atomic_store_explicit(&self->shared_base_page_list, self->base_page_list, memory_order_relaxed);
        atomic_store_explicit(&self->shared_tiny_page_list, self->tiny_page_list, memory_order_relaxed);
        atomic_store_explicit(&self->shared_small_page_list, self->small_page_list, memory_order_relaxed);
        atomic_store_explicit(&self->shared_small_x2_page_list, self->small_x2_page_list, memory_order_relaxed);
        atomic_store_explicit(&self->shared_small_x4_page_list, self->small_x4_page_list, memory_order_relaxed);
    }    
    return knd_OK;
}

int knd_mempool_create(struct kndMemPool **result, struct kndMemConfig *config, size_t numid)
{
    struct kndMemPool *mempool;
    int err;

    err = knd_mempool_new(&mempool, config->memtype, numid);
    if (err) return err;

    mempool->num_large_pages = config->num_large_pages;
    mempool->num_base_pages = config->num_base_pages;
    mempool->num_small_x4_pages = config->num_small_x4_pages;
    mempool->num_small_x2_pages = config->num_small_x2_pages;
    mempool->num_small_pages = config->num_small_pages;
    mempool->num_tiny_pages = config->num_tiny_pages;

    err = knd_mempool_alloc(mempool);
    if (err) return err;

    *result = mempool;
    return knd_OK;
}

int knd_mempool_new(struct kndMemPool **obj, knd_mempool_t type, size_t mempool_id)
{
    struct kndMemPool *self;
    self = malloc(sizeof(struct kndMemPool));
    if (!self) return knd_NOMEM;
    memset(self, 0, sizeof(struct kndMemPool));
    self->type = type;
    self->numid = mempool_id;
    *obj = self;
    return knd_OK;
}
