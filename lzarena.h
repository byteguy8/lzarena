#ifndef LZARENA_H
#define LZARENA_H

#include <stddef.h>
#include <stdint.h>

#define LZARENA_OK 0
#define LZARENA_ERR_ALLOC 1

#define LZARENA_DEFAULT_ALIGNMENT 16
#define LZARENA_DEFAULT_FACTOR 1

#define LZARENA_BACKEND_MALLOC 0
#define LZARENA_BACKEND_MMAP 1
#define LZARENA_BACKEND_VIRTUALALLOC 2

#ifndef LZARENA_BACKEND
    #ifdef _WIN32
        #define LZARENA_BACKEND LZARENA_BACKEND_VIRTUALALLOC
    #elif __linux__
        #define LZARENA_BACKEND LZARENA_BACKEND_MMAP
    #else
        #define LZARENA_BACKEND LZARENA_BACKEND_MALLOC
    #endif
#endif

typedef uint64_t reset_t;
typedef struct lzarena_allocator LZArenaAllocator;
typedef struct lzregion LZRegion;
typedef struct lzarena LZArena;

struct lzarena_allocator{
    void *ctx;
    void *(*alloc)(size_t size, void *ctx);
    void *(*realloc)(void *ptr, size_t old_size, size_t new_size, void *ctx);
    void (*dealloc)(void *ptr, size_t size, void *ctx);
};

struct lzregion{
    reset_t reset;
    size_t region_size;
    size_t chunk_size;
    void *chunk;
    void *offset;
    LZRegion *next;
};

struct lzarena{
    reset_t reset;
    size_t allocted_bytes;
    LZRegion *head;
    LZRegion *tail;
    LZRegion *current;
    LZArenaAllocator *allocator;
};

LZRegion *lzregion_init(size_t buff_size, void *buff);
LZRegion *lzregion_create(size_t size);
void lzregion_destroy(LZRegion *region);

#define LZREGION_FREE(region){       \
    region->offset = region->chunk;  \
}

size_t lzregion_available(LZRegion *region);
size_t lzregion_available_alignment(size_t alignment, LZRegion *region);
void *lzregion_alloc_align(size_t size, size_t alignment, LZRegion *region, size_t *out_bytes);
void *lzregion_calloc_align(size_t size, size_t alignment, LZRegion *region);
void *lzregion_realloc_align(void *ptr, size_t old_size, size_t new_size, size_t alignment, LZRegion *region);

LZArena *lzarena_create(LZArenaAllocator *allocator);
void lzarena_destroy(LZArena *arena);

#define LZARENA_OFFSET(_lzarena)((_lzarena)->current->offset)
void lzarena_report(size_t *used, size_t *size, LZArena *arena);
int lzarena_append_region(size_t size, LZArena *arena);
void lzarena_free_all(LZArena *arena);

void *lzarena_alloc_align(size_t size, size_t alignment, LZArena *arena);
void *lzarena_calloc_align(size_t size, size_t alignment, LZArena *arena);
void *lzarena_realloc_align(void *ptr, size_t old_size, size_t new_size, size_t alignment, LZArena *arena);

#define LZARENA_ALLOC(_size, _arena)(lzarena_alloc_align(_size, LZARENA_DEFAULT_ALIGNMENT, _arena))
#define LZARENA_REALLOC(_ptr, _old_size, _new_size, _arena)(lzarena_realloc_align(_ptr, _old_size, _new_size, LZARENA_DEFAULT_ALIGNMENT, _arena))

#endif