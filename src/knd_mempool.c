#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_mempool.h"
#include "knd_utils.h"
#include <glb-lib/output.h>

static void del(struct kndMemPool *self)
{
    knd_log(".. del mempool pages..");
    free(self->pages);
    free(self->small_x4_pages);
    free(self->small_x2_pages);
    free(self->small_pages);
    free(self->tiny_pages);

    knd_log(".. del mempool: %p", self);
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
    struct kndMemPageHeader *page = NULL, *prev_page, **head_page, **tail_page;
    size_t *pages_used;
    size_t max_pages = 0;
    size_t max_page_payload_size;

    //knd_log(".. alloc: page_type:%d size:%zu", page_size, obj_size);
    
    switch (page_size) {
    case KND_MEMPAGE_BASE:
        max_page_payload_size = self->page_payload_size;
        head_page = &self->head_page;
        tail_page = &self->tail_page;
        pages_used = &self->pages_used;
        max_pages = self->num_pages;
        break;
    case KND_MEMPAGE_SMALL_X4:
        max_page_payload_size = self->small_x4_page_payload_size;
        head_page = &self->head_small_x4_page;
        tail_page = &self->tail_small_x4_page;
        pages_used = &self->small_x4_pages_used;
        max_pages = self->num_small_x4_pages;
        break;
    case KND_MEMPAGE_SMALL_X2:
        max_page_payload_size = self->small_x2_page_payload_size;
        head_page = &self->head_small_x2_page;
        tail_page = &self->tail_small_x2_page;
        pages_used = &self->small_x2_pages_used;
        max_pages = self->num_small_x2_pages;
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

    if (obj_size > max_page_payload_size) {
        knd_log("-- mem page size exceeded: %zu max payload:%zu",
                obj_size, max_page_payload_size);
        return knd_LIMIT;
    }

    if (obj_size < (max_page_payload_size * 0.7) && obj_size > 64) {
        knd_log("-- too large mem page requested: %zu  max payload:%zu",
                obj_size, max_page_payload_size);
        return knd_LIMIT;
    }

    if (*pages_used + 1 > max_pages) {
        knd_log("-- mem limit reached: max pages:%zu [%d] capacity:%zu",
                max_pages, page_size, self->capacity);
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

    *result = (void*)(page + 1);
    return knd_OK;
}

extern void knd_mempool_free(struct kndMemPool *unused_var(self),
                             knd_mempage_t unused_var(page_size),
                             void *page_data)
{
    struct kndMemPageHeader *page = NULL; //, *prev_page, **head_page, **tail_page;
    //size_t offset;
    char *c;

    knd_log(".. free page:%p", page_data);
    c = (char*)page_data - sizeof(struct kndMemPageHeader);
    page = (void*)c;
    knd_log(".. page prev:%p page next:%p", page->prev, page->next);
}

static void build_linked_list(char *pages,
                              size_t num_pages,
                              size_t page_size,
                              struct kndMemPageHeader **head_page,
                              struct kndMemPageHeader **tail_page)
{
    struct kndMemPageHeader *page, *prev_page = NULL;
    char *c = NULL;
    size_t offset = 0;

    *head_page = (struct kndMemPageHeader*)pages;
    for (size_t i = 0; i < num_pages; i++) {
        offset = (i * page_size);
        c = pages + offset;
        page = (struct kndMemPageHeader*)c;
        if (prev_page) {
            prev_page->next = page;
            page->prev = prev_page;
        }
        prev_page = page;
    }
    *tail_page = page;
}

static int alloc_page_buf(struct kndMemPool *self,
                          char **result_pages,
                          size_t *result_num_pages, size_t default_num_pages,
                          size_t *result_page_size, size_t default_page_size,
                          size_t *page_payload_size)
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
        knd_log("-- mem pages not allocated :(");
        return knd_NOMEM;
    }
    self->capacity += (num_pages * page_size);

    knd_log("++ alloc'd %zu pages of %zu size, total capacity:%zu",
            num_pages, page_size, self->capacity);

    *result_pages = pages;
    *result_page_size = page_size;
    *result_num_pages = num_pages;
    *page_payload_size = page_size - sizeof(struct kndMemPageHeader);
    return knd_OK;
}

static int alloc_capacity(struct kndMemPool *self)
{
    int err;

    err = alloc_page_buf(self,
                         &self->pages, &self->num_pages,
                         KND_NUM_BASE_MEMPAGES,
                         &self->page_size, KND_BASE_MEMPAGE_SIZE,
                         &self->page_payload_size);
    build_linked_list(self->pages, self->num_pages, self->page_size,
                      &self->head_page, &self->tail_page);

    err = alloc_page_buf(self,
                         &self->small_x4_pages, &self->num_small_x4_pages,
                         KND_NUM_SMALL_X4_MEMPAGES,
                         &self->small_x4_page_size, KND_SMALL_X4_MEMPAGE_SIZE,
                         &self->small_x4_page_payload_size);
    build_linked_list(self->small_x4_pages,
                      self->num_small_x4_pages, self->small_x4_page_size,
                      &self->head_small_x4_page, &self->tail_small_x4_page);

    err = alloc_page_buf(self,
                         &self->small_x2_pages, &self->num_small_x2_pages,
                         KND_NUM_SMALL_X2_MEMPAGES,
                         &self->small_x2_page_size, KND_SMALL_X2_MEMPAGE_SIZE,
                         &self->small_x2_page_payload_size);
    build_linked_list(self->small_x2_pages,
                      self->num_small_x2_pages, self->small_x2_page_size,
                      &self->head_small_x2_page, &self->tail_small_x2_page);

    err = alloc_page_buf(self,
                         &self->small_pages, &self->num_small_pages,
                         KND_NUM_SMALL_MEMPAGES,
                         &self->small_page_size, KND_SMALL_MEMPAGE_SIZE,
                         &self->small_page_payload_size);
    build_linked_list(self->small_pages,
                      self->num_small_pages, self->small_page_size,
                      &self->head_small_page, &self->tail_small_page);

    err = alloc_page_buf(self,
                         &self->tiny_pages, &self->num_tiny_pages,
                         KND_NUM_TINY_MEMPAGES,
                         &self->tiny_page_size, KND_TINY_MEMPAGE_SIZE,
                         &self->tiny_page_payload_size);
    build_linked_list(self->tiny_pages,
                      self->num_tiny_pages, self->tiny_page_size,
                      &self->head_tiny_page, &self->tail_tiny_page);


    knd_log("== MemPool total bytes alloc'd: %zu", self->capacity);

    return knd_OK;
}

static gsl_err_t
parse_memory_settings(struct kndMemPool *self, const char *rec, size_t *total_size)
{
    struct gslTaskSpec specs[] = {
//        {   .name = "max_user_ctxs",
//            .name_size = strlen("max_use_ctxs"),
//            .parse = gsl_parse_size_t,
//            .obj = &self->max_user_ctxs
//        },
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
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

extern void kndMemPool_init(struct kndMemPool *self)
{
    self->del = del;
    self->parse = parse_memory_settings;
    self->alloc = alloc_capacity;
    self->present = present_status;
}

extern int kndMemPool_new(struct kndMemPool **obj)
{
    struct kndMemPool *self;

    self = malloc(sizeof(struct kndMemPool));
    if (!self) return knd_NOMEM;
    memset(self, 0, sizeof(struct kndMemPool));

    kndMemPool_init(self);
    *obj = self;
    return knd_OK;
}
