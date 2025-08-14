#ifndef MEM_ARENA_H_
#define MEM_ARENA_H_

#ifndef MEM_ARENA_ASSERT
#include <assert.h>
#define MEM_ARENA_ASSERT assert
#endif

#include <stddef.h>
#include <stdint.h>

typedef struct mem_arena_region {
    struct mem_arena_region *next;
    size_t data_offset; // Offset into data_ptr
    size_t region_size;

    // Don't worry, this isn't adding a level of indirection.
    // The bytes are close after the end of this structure,
    // but not necessarily contiguous (alignment).
    char *data_ptr;
} MemArenaRegion;

struct mem_arena_mark_data {
    struct mem_arena_mark_data *next;
    size_t data_offset;
};

typedef struct mem_arena_mark {
    struct mem_arena_region *first_region;
    struct mem_arena_mark_data *data_first;
} MemArenaMark;

/* Must set the 'size' field of the region returned. */
typedef MemArenaRegion *(*MemArena_RegionProvider)(void *user, size_t bytes);
typedef void (*MemArena_RegionConsumer)(void *user, MemArenaRegion *region);

typedef struct mem_arena {
    void *user;
    size_t alignment, region_granularity;
    struct mem_arena_region *first_region;

    /* this allocator must return pointers aligned
       to at least `alignment' */
    MemArena_RegionProvider mem_req;
    MemArena_RegionConsumer mem_rel;
} MemArena;

void init_mem_arena(struct mem_arena *ma);

/* @param ma: the memory arena
   @param user: the user data to be stored in the arena structure
   @param alignment: the base alignment of pointers returned by arena functions
                     NOTE: this cannot be larger than the alignment
                    provided by the underlying allocator (mem_req).
                    this parameter is simply included to allow "passthrough"
                    of the underlying allocator's alignment.

   @param region_granularity: the minimum arena size, and the value up to which region sizes will be
   rounded */
void init_mem_arena_explicit(struct mem_arena *ma, void *user, size_t alignment,
                             size_t region_granularity, MemArena_RegionProvider req, MemArena_RegionConsumer rel);

void deinit_mem_arena(struct mem_arena *ma);

void *mem_arena_alloc(struct mem_arena *ma, size_t bytes);

/* this function does nothing, but exists
   in case one needs to pass a function pointer
   to some code expecting an allocator */
void mem_arena_free(struct mem_arena *ma, void *ptr);

/* Save the state of the arena to be furthered restored.
   This utility enables the arena to function in a manner
   similar to that of the program stack,
   growing and shrinking from one end as needed. */
struct mem_arena_mark *mem_arena_create_mark(struct mem_arena *ma);
void mem_arena_reset_to_mark(struct mem_arena *ma, struct mem_arena_mark *mark);
void mem_arena_free_mark(struct mem_arena *ma, struct mem_arena_mark *mark);

#endif
