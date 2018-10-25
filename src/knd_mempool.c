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

int knd_mempool_alloc(struct kndMemPool *self,
                      knd_mempage_t page_type,
                      size_t obj_size, void **result)
{
    struct kndMemPageHeader **page_list;
    size_t page_size, num_pages, *pages_used;

    //knd_log(".. alloc: page_type:%d size:%zu", page_type, obj_size);
    
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

    if (obj_size > page_size) {
        knd_log("-- mem page size exceeded: %zu max size:%zu",
                obj_size, page_size);
        return knd_LIMIT;
    }

    if (obj_size <= (page_size * 0.75) && obj_size > 64) {
        //knd_log("-- too large mem page requested: %zu  max size:%zu",
        //        obj_size, page_size);
        //return knd_LIMIT;  // FIXME(k15tfu): temporarily disabled
    }

    if (*pages_used + 1 > num_pages) {
        knd_log("-- mem limit reached: max pages:%zu [%d] capacity:%zu",
                num_pages, page_type, self->capacity);
        return knd_LIMIT;
    }

    //knd_log("head:%p tail:%p page:%p", *head_page, *tail_page, page);

    assert(*page_list != NULL);
    *result = *page_list;
    *page_list = (*page_list)->next;
    (*pages_used)++;

    memset(*result, 0, obj_size);  // FIXME(k15tfu): don't initialize the memory

    return knd_OK;
}

void knd_mempool_free(struct kndMemPool *self,
                      knd_mempage_t page_type,
                      void *page_data)
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


    knd_log(".. free page:%p", page_data);

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
        knd_log("-- mem pages not allocated :(");
        return knd_NOMEM;
    }
    self->capacity += (num_pages * page_size);

    knd_log("++ alloc'd %zu pages of %zu size, total capacity:%zu",
            num_pages, page_size, self->capacity);

    *result_pages = pages;
    *result_page_size = page_size;
    *result_num_pages = num_pages;
    return knd_OK;
}

static void build_linked_list(char *pages,
                              size_t num_pages,
                              size_t page_size,
                              struct kndMemPageHeader **page_list)
{
    for (size_t i = 0; i < num_pages; i++) {
        *page_list = (struct kndMemPageHeader *)pages;

        pages += page_size;
        page_list = &(*page_list)->next;
    }
    *page_list = NULL;
}

static int alloc_capacity(struct kndMemPool *self)
{
    int err;

    err = alloc_page_buf(self,
                         &self->pages, &self->num_pages,
                         KND_NUM_BASE_MEMPAGES,
                         &self->page_size, KND_BASE_MEMPAGE_SIZE);                             RET_ERR();
    build_linked_list(self->pages, self->num_pages, self->page_size, &self->page_list);

    err = alloc_page_buf(self,
                         &self->small_x4_pages, &self->num_small_x4_pages,
                         KND_NUM_SMALL_X4_MEMPAGES,
                         &self->small_x4_page_size, KND_SMALL_X4_MEMPAGE_SIZE);                    RET_ERR();
    build_linked_list(self->small_x4_pages,
                      self->num_small_x4_pages, self->small_x4_page_size,
                      &self->small_x4_page_list);

    err = alloc_page_buf(self,
                         &self->small_x2_pages, &self->num_small_x2_pages,
                         KND_NUM_SMALL_X2_MEMPAGES,
                         &self->small_x2_page_size, KND_SMALL_X2_MEMPAGE_SIZE);                    RET_ERR();
    build_linked_list(self->small_x2_pages,
                      self->num_small_x2_pages, self->small_x2_page_size,
                      &self->small_x2_page_list);

    err = alloc_page_buf(self,
                         &self->small_pages, &self->num_small_pages,
                         KND_NUM_SMALL_MEMPAGES,
                         &self->small_page_size, KND_SMALL_MEMPAGE_SIZE);                       RET_ERR();
    build_linked_list(self->small_pages,
                      self->num_small_pages, self->small_page_size,
                      &self->small_page_list);

    err = alloc_page_buf(self,
                         &self->tiny_pages, &self->num_tiny_pages,
                         KND_NUM_TINY_MEMPAGES,
                         &self->tiny_page_size, KND_TINY_MEMPAGE_SIZE);                        RET_ERR();
    build_linked_list(self->tiny_pages,
                      self->num_tiny_pages, self->tiny_page_size,
                      &self->tiny_page_list);


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
