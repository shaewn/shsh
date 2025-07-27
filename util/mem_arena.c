#include "mem_arena.h"

#include <stdlib.h>

#define DEFAULT_MIN_REGION_SIZE 4096

static size_t total_ma_reg_size(struct mem_arena_region *r);
static struct mem_arena_region *find_existing(struct mem_arena *ma, size_t min_avail);
static size_t align_size_to_requirement(size_t alignment, size_t bytes);
static struct mem_arena_region *create_new_region(struct mem_arena *ma, size_t guaranteed_bytes);
static struct mem_arena_region *free_region(struct mem_arena *ma, struct mem_arena_region *r);
static void *malloc_wrapper(void *user, size_t bytes);
static void free_wrapper(void *user, void *base, size_t bytes);

void init_mem_arena(struct mem_arena *ma) {
    init_mem_arena_explicit(ma, NULL, sizeof(max_align_t), DEFAULT_MIN_REGION_SIZE, malloc_wrapper, free_wrapper);
}

void init_mem_arena_explicit(struct mem_arena *ma, void *user, size_t alignment,
                             size_t region_granularity, mem_arena_mem_req_t req, mem_arena_mem_rel_t rel) {
    ma->user = user;
    ma->alignment = alignment;
    ma->region_granularity = region_granularity;
    ma->first_region = NULL;
    ma->mem_req = req;
    ma->mem_rel = rel;

    MEM_ARENA_ASSERT(ma->alignment > 0 && (ma->alignment & (ma->alignment - 1)) == 0);
}

void deinit_mem_arena(struct mem_arena *ma) {
    struct mem_arena_region *next, *r;
    for (r = ma->first_region; r; r = next) {
        next = free_region(ma, r);
    }
}

void *mem_arena_alloc(struct mem_arena *ma, size_t bytes) {
    bytes = align_size_to_requirement(ma->alignment, bytes);
    struct mem_arena_region *r = find_existing(ma, bytes);

    if (r == NULL) {
        r = create_new_region(ma, bytes);
        r->next = ma->first_region;
        ma->first_region = r;
    }

    char *ptr = r->data_ptr + r->data_offset;
    r->data_offset += bytes;

    return ptr;
}

// does nothing.
void mem_arena_free(struct mem_arena *ma, void *ptr) {}

struct mem_arena_mark *mem_arena_create_mark(struct mem_arena *ma) {
    struct mem_arena_mark *mark = malloc(sizeof(*mark));
    mark->first_region = ma->first_region;

    struct mem_arena_mark_data **p = &mark->data_first;

    for (struct mem_arena_region *r = ma->first_region; r; r = r->next) {
        struct mem_arena_mark_data *d = malloc(sizeof(*d));
        d->data_offset = r->data_offset;

        *p = d;
        p = &d->next;
    }

    *p = NULL;

    return mark;
}

void mem_arena_reset_to_mark(struct mem_arena *ma, struct mem_arena_mark *mark) {
    struct mem_arena_region *r = ma->first_region;
    struct mem_arena_mark_data *md;

    while (r != mark->first_region) {
        r = free_region(ma, r);
    }

    for (md = mark->data_first; md; md = md->next, r = r->next) {
        r->data_offset = md->data_offset;
    }
}

void mem_arena_free_mark(struct mem_arena *ma, struct mem_arena_mark *mark) {
    struct mem_arena_mark_data *next, *md;
    for (md = mark->data_first; md; md = next) {
        next = md->next;

        free(md);
    }

    free(mark);
}

static size_t total_ma_reg_size(struct mem_arena_region *r) { return sizeof(*r) + r->region_size; }

static struct mem_arena_region *find_existing(struct mem_arena *ma, size_t min_avail) {
    for (struct mem_arena_region *r = ma->first_region; r; r = r->next) {
        if (r->data_offset + min_avail <= r->region_size) {
            // Suitable.
            return r;
        }
    }

    return NULL;
}

static size_t align_size_to_requirement(size_t alignment, size_t bytes) {
    bytes += alignment - 1;    // Go past the multiple of alignment
    bytes &= ~(alignment - 1); // Discard excess

    return bytes;
}

static struct mem_arena_region *create_new_region(struct mem_arena *ma, size_t guaranteed_bytes) {
    size_t non_data_size =
        align_size_to_requirement(ma->alignment, sizeof(struct mem_arena_region));
    size_t region_data_size;

    if (guaranteed_bytes > ma->region_granularity) {
        region_data_size = align_size_to_requirement(ma->region_granularity, guaranteed_bytes);
        region_data_size = align_size_to_requirement(ma->alignment, region_data_size);
    } else {
        region_data_size = align_size_to_requirement(ma->alignment, ma->region_granularity);
    }

    size_t total_bytes = non_data_size + region_data_size;

    struct mem_arena_region *region = ma->mem_req(ma->user, total_bytes);
    region->next = NULL;
    region->data_offset = 0;
    region->region_size = region_data_size;
    region->data_ptr = (char *)region + non_data_size;

    return region;
}

static struct mem_arena_region *free_region(struct mem_arena *ma, struct mem_arena_region *r) {
    struct mem_arena_region *next = r->next;

    size_t bytes_to_release = total_ma_reg_size(r);
    ma->mem_rel(ma->user, r, bytes_to_release);

    return next;
}

static void *malloc_wrapper(void *user, size_t bytes) {
    return malloc(bytes);
}


static void free_wrapper(void *user, void *base, size_t bytes) {
    return free(base);
}
