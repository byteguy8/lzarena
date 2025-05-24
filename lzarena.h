#ifndef LZARENA_H
#define LZARENA_H

#include <stddef.h>

#define LZARENA_OK 0
#define LZARENA_ERR_ALLOC 1

#define LZARENA_DEFAULT_ALIGNMENT 8
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
    size_t region_len;
    size_t chunk_len;
    void *offset;
    void *chunk;
    LZRegion *next;
};

struct lzarena{
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
void *lzregion_alloc_align(size_t size, size_t alignment, LZRegion *region);
void *lzregion_calloc_align(size_t size, size_t alignment, LZRegion *region);
void *lzregion_realloc_align(void *ptr, size_t old_size, size_t new_size, size_t alignment, LZRegion *region);

LZArena *lzarena_create(LZArenaAllocator *allocator);
void lzarena_destroy(LZArena *arena);

#define LZARENA_OFFSET(_lzarena)((_lzarena)->current->offset)
void lzarena_report(size_t *used, size_t *size, LZArena *arena);
void lzarena_free_all(LZArena *arena);
void *lzarena_alloc_align(size_t size, size_t alignment, LZArena *arena);
void *lzarena_calloc_align(size_t size, size_t alignment, LZArena *arena);
void *lzarena_realloc_align(void *ptr, size_t old_size, size_t new_size, size_t alignment, LZArena *arena);
#define LZARENA_ALLOC(size, arena)(lzarena_alloc_align(size, LZARENA_DEFAULT_ALIGNMENT, arena))
#define LZARENA_REALLOC(ptr, old_size, new_size, arena)(lzarena_realloc_align(ptr, old_size, new_size, LZARENA_DEFAULT_ALIGNMENT, arena))

#endif