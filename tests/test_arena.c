#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../mem_arena.h"

static void *custom_mem_req(void *user, size_t bytes) {
    // Just wrap malloc for demonstration; user ignored.
    printf("[custom_mem_req] Allocating %zu bytes\n", bytes);
    return malloc(bytes);
}

static void custom_mem_ret(void *user, void *base, size_t bytes) {
    // Just wrap free for demonstration; user ignored.
    printf("[custom_mem_ret] Freeing %zu bytes at %p\n", bytes, base);
    free(base);
}

int main(void) {
    struct mem_arena ma;

    // Test: initialize arena with default init
    init_mem_arena(&ma);
    printf("Arena initialized (default)\n");

    // Test: explicit init with user-defined parameters and allocator functions
    size_t alignment = 16;
    size_t region_granularity = 128;
    int userdata = 123;
    init_mem_arena_explicit(&ma, &userdata, alignment, region_granularity, custom_mem_req, custom_mem_ret);
    printf("Arena explicitly initialized\n");

    // Test: simple allocation
    const size_t test_alloc_size = 64;
    void *ptr1 = mem_arena_alloc(&ma, test_alloc_size);
    printf("Allocated %zu bytes: %p\n", test_alloc_size, ptr1);

    // Test: alignment (pointer value is a multiple of alignment)
    if ((uintptr_t)ptr1 % alignment == 0) {
        printf("Allocation is properly aligned to %zu\n", alignment);
    } else {
        printf("ERROR: Allocation is NOT properly aligned!\n");
    }

    // Test: allocation larger than region granularity (forces new region)
    void *ptr2 = mem_arena_alloc(&ma, region_granularity * 2);
    printf("Allocated %zu bytes (should be in new region): %p\n", region_granularity * 2, ptr2);

    // Test: mark, alloc after mark, then reset
    struct mem_arena_mark *mark = mem_arena_create_mark(&ma);
    printf("Created mark\n");

    void *ptr3 = mem_arena_alloc(&ma, 32);
    strcpy((char *)ptr3, "Hello, Arena!");
    printf("Allocated 32 bytes after mark: %p, wrote string: \"%s\"\n", ptr3, (char *)ptr3);

    mem_arena_reset_to_mark(&ma, mark);
    printf("Reset to mark. (ptr3 should now be considered invalid)\n");

    mem_arena_free_mark(&ma, mark);
    printf("Freed mark\n");

    // Test: dummy free
    mem_arena_free(&ma, ptr1);  // This should be a no-op

    // Deinitialize
    deinit_mem_arena(&ma);
    printf("Arena deinitialized\n");

    return 0;
}

