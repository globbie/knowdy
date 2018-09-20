#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_mempool.h"
#include "knd_utils.h"
#include <glb-lib/output.h>

static void del(struct kndMemPool *self)
{
    free(self);
}

static int present_status(struct kndMemPool *self,
                          struct glbOutput *out)
{
    int err;

    err = out->writef(out, "{\"used_pages\":%zu}", self->pages_used);    RET_ERR();

    return knd_OK;
}

extern int knd_mempool_alloc(struct kndMemPool *self,
                             knd_mempage_t page_size,
                             size_t obj_size, void **result)
{
    struct kndMemPage *page = NULL, *prev_page, **head_page, **tail_page;
    size_t *pages_used;
    size_t max_pages = 0;
    size_t max_page_payload_size;

    //knd_log(".. alloc: page_type:%d size:%zu", page_size, obj_size);
    
    switch (page_size) {
    case KND_MEMPAGE_LARGE:
        max_page_payload_size = self->page_payload_size;
        head_page = &self->head_page;
        tail_page = &self->tail_page;
        pages_used = &self->pages_used;
        max_pages = self->num_pages;
        break;
    case KND_MEMPAGE_NORMAL:
        max_page_payload_size = self->page_payload_size;
        head_page = &self->head_page;
        tail_page = &self->tail_page;
        pages_used = &self->pages_used;
        max_pages = self->num_pages;
        break;
    case KND_MEMPAGE_MED:
        max_page_payload_size = self->med_page_payload_size;
        head_page = &self->head_med_page;
        tail_page = &self->tail_med_page;
        pages_used = &self->med_pages_used;
        max_pages = self->num_med_pages;
        break;
    case KND_MEMPAGE_SMALL:
        max_page_payload_size = self->small_page_payload_size;
        head_page = &self->head_small_page;
        tail_page = &self->tail_small_page;
        pages_used = &self->small_pages_used;
        max_pages = self->num_small_pages;
        break;
    case KND_MEMPAGE_TINY:
        max_page_payload_size = self->tiny_page_payload_size;
        head_page = &self->head_tiny_page;
        tail_page = &self->tail_tiny_page;
        pages_used = &self->tiny_pages_used;
        max_pages = self->num_tiny_pages;
        break;
    default:
        max_page_payload_size = self->page_payload_size;
        head_page = &self->head_page;
        tail_page = &self->tail_page;
        pages_used = &self->pages_used;
        max_pages = self->num_pages;
        break;
    }

    if (obj_size >= max_page_payload_size) {
        knd_log("-- mem page size exceeded: %zu", obj_size);
        return knd_LIMIT;
    }

    if (*pages_used + 1 > max_pages) {
        knd_log("-- mem limit reached: max pages:%zu [%d]",
                max_pages, page_size);
        return knd_LIMIT;
    }

    //knd_log("head:%p tail:%p page:%p", *head_page, *tail_page, page);

    page = *tail_page;
    prev_page = page->prev;
    prev_page->next = NULL;
    *tail_page = prev_page;

    page->prev = NULL;
    page->next = *head_page;
    (*head_page)->prev = page;
    *head_page = page;
    (*pages_used)++;

    *result = (void*)page->data;
    return knd_OK;
}

extern void knd_mempool_free(struct kndMemPool *self __attribute__((unused)),
                             knd_mempage_t page_size __attribute__((unused)),
                             void *page_data)
{
    struct kndMemPage *page = NULL; //, *prev_page, **head_page, **tail_page;
    //size_t offset;
    char *c;

    knd_log(".. free page:%p", page_data);
    c = (char*)page_data - sizeof(struct kndMemPage) + sizeof(void*);
    page = (void*)c;
    knd_log(".. page prev:%p page next:%p", page->prev, page->next);
}

static void build_linked_list(char *pages,
                              size_t num_pages,
                              size_t page_size,
                              struct kndMemPage **head_page,
                              struct kndMemPage **tail_page)
{
    struct kndMemPage *page, *prev_page = NULL;
    char *c = NULL;
    size_t offset = 0;

    *head_page = (struct kndMemPage*)pages;
    for (size_t i = 0; i < num_pages; i++) {
        offset = (i * page_size);
        c = pages + offset;
        page = (struct kndMemPage*)c;
        c = (char*)page + sizeof(struct kndMemPage);
        page->data = (void*)c;

        //knd_log("%zi) offset:%zu page:%p", i, offset, page);

        if (prev_page) {
            prev_page->next = page;
            page->prev = prev_page;
        }
        prev_page = page;
    }
    *tail_page = page;
}

static int alloc(struct kndMemPool *self)
{
    size_t total_alloc_size = 0;

    /* regular pages */
    if (!self->page_size)
        self->page_size = KND_MEMPAGE_SIZE;
    if (self->page_size <= sizeof(struct kndMemPage)) return knd_LIMIT;
    self->page_payload_size = self->page_size - sizeof(struct kndMemPage);
    if (!self->num_pages)
        self->num_pages = KND_NUM_MEMPAGES;

    self->pages = calloc(self->num_pages, self->page_size);
    if (!self->pages) {
        knd_log("-- mem pages not allocated :(");
        return knd_NOMEM;
    }

    /* medium size pages */
    if (!self->med_page_size)
        self->med_page_size = KND_MED_MEMPAGE_SIZE;
    if (self->med_page_size <= sizeof(struct kndMemPage)) return knd_LIMIT;
    self->med_page_payload_size = self->med_page_size - sizeof(struct kndMemPage);
    if (!self->num_med_pages)
        self->num_med_pages = KND_NUM_MED_MEMPAGES;

    self->med_pages = calloc(self->num_med_pages, self->med_page_size);
    if (!self->med_pages) {
        knd_log("-- mem pages not allocated :(");
        return knd_NOMEM;
    }

    
    /* small pages */
    if (!self->small_page_size)
        self->small_page_size = KND_SMALL_MEMPAGE_SIZE;
    if (self->small_page_size <= sizeof(struct kndMemPage)) return knd_LIMIT;
    self->small_page_payload_size = self->small_page_size - sizeof(struct kndMemPage);
    if (!self->num_small_pages)
        self->num_small_pages = KND_NUM_SMALL_MEMPAGES;

    self->small_pages = calloc(self->num_small_pages, self->small_page_size);
    if (!self->small_pages) {
        knd_log("-- mem pages not allocated :(");
        return knd_NOMEM;
    }

    /* tiny pages */
    if (!self->tiny_page_size)
        self->tiny_page_size = KND_TINY_MEMPAGE_SIZE;
    if (self->tiny_page_size <= sizeof(struct kndMemPage)) return knd_LIMIT;
    self->tiny_page_payload_size = self->tiny_page_size - sizeof(struct kndMemPage);
    if (!self->num_tiny_pages)
        self->num_tiny_pages = KND_NUM_TINY_MEMPAGES;

    self->tiny_pages = calloc(self->num_tiny_pages, self->tiny_page_size);
    if (!self->tiny_pages) {
        knd_log("-- mem pages not allocated :(");
        return knd_NOMEM;
    }

    build_linked_list(self->pages, self->num_pages, self->page_size,
                      &self->head_page, &self->tail_page);

    build_linked_list(self->med_pages, self->num_med_pages, self->med_page_size,
                      &self->head_med_page, &self->tail_med_page);

    build_linked_list(self->small_pages, self->num_small_pages, self->small_page_size,
                      &self->head_small_page, &self->tail_small_page);

    build_linked_list(self->tiny_pages, self->num_tiny_pages, self->tiny_page_size,
                      &self->head_tiny_page, &self->tail_tiny_page);

    total_alloc_size = (self->num_pages * self->page_size);
    total_alloc_size += self->num_med_pages * self->med_page_size;
    total_alloc_size += self->num_small_pages * self->small_page_size;
    total_alloc_size += self->num_tiny_pages * self->tiny_page_size;

    knd_log("== MemPool total bytes alloc'd: %zu", total_alloc_size);

    return knd_OK;
}

static gsl_err_t
parse_memory_settings(struct kndMemPool *self, const char *rec, size_t *total_size)
{
    struct gslTaskSpec specs[] = {
        {   .name = "max_user_ctxs",
            .name_size = strlen("max_use_ctxs"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_user_ctxs
        },
        {   .name = "max_normal_pages",
            .name_size = strlen("max_normal_pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->num_pages
        },
        {   .name = "max_med_pages",
            .name_size = strlen("max_med_pages"),
            .parse = gsl_parse_size_t,
            .obj = &self->num_med_pages
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
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

extern void kndMemPool_init(struct kndMemPool *self)
{
    self->del = del;
    self->parse = parse_memory_settings;
    self->alloc = alloc;
    self->present = present_status;
}

extern int kndMemPool_new(struct kndMemPool **obj)
{
    struct kndMemPool *self;
    int err;
    self = malloc(sizeof(struct kndMemPool));
    if (!self) return knd_NOMEM;
    memset(self, 0, sizeof(struct kndMemPool));

    err = glbOutput_new(&self->log, KND_MED_BUF_SIZE);
    if (err != knd_OK) goto error;

    kndMemPool_init(self);
    *obj = self;
    return knd_OK;

error:
    del(self);
    return err;
}
