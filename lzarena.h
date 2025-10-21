#ifndef LZARENA_H
#define LZARENA_H

#include <stddef.h>
#include <stdint.h>

#define LZARENA_OK        0
#define LZARENA_ERR_ALLOC 1

#define LZARENA_DEFAULT_ALIGNMENT 16
#define LZARENA_DEFAULT_FACTOR    1

#define LZARENA_BACKEND_MALLOC       0
#define LZARENA_BACKEND_MMAP         1
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
typedef struct lzregion          LZRegion;
typedef struct lzarena           LZArena;

struct lzarena_allocator{
    void *ctx;
    void *(*alloc)(size_t size, void *ctx);
    void *(*realloc)(void *ptr, size_t old_size, size_t new_size, void *ctx);
    void (*dealloc)(void *ptr, size_t size, void *ctx);
};

LZRegion *lzregion_init(
	LZRegion *region,
	size_t block_size,
	void *block,
	LZArenaAllocator *allocator
);
LZRegion *lzregion_create(LZArenaAllocator *allocator, size_t block_size);
void lzregion_destroy(LZRegion *region);

size_t lzregion_available(LZRegion *region);
size_t lzregion_available_alignment(LZRegion *region, size_t alignment);
void *lzregion_alloc_align(
	LZRegion *region,
	size_t alignment,
	size_t size,
	size_t *out_bytes
);
void *lzregion_calloc_align(LZRegion *region, size_t alignment, size_t size);
void *lzregion_realloc_align(
	LZRegion *region,
	void *ptr,
	size_t old_size,
	size_t alignment,
	size_t new_size
);

LZArena *lzarena_create(LZArenaAllocator *allocator);
void lzarena_destroy(LZArena *arena);

void lzarena_report(LZArena *arena, size_t *used, size_t *size);
int lzarena_append_region(LZArena *arena, size_t size);
void lzarena_free_all(LZArena *arena);

void *lzarena_alloc_align(LZArena *arena, size_t alignment, size_t size);
void *lzarena_calloc_align(LZArena *arena, size_t alignment, size_t size);
void *lzarena_realloc_align(
	LZArena *arena,
	void *ptr,
	size_t old_size,
	size_t alignment,
	size_t new_size
);

#define LZARENA_ALLOC(_arena, _size) \
	(lzarena_alloc_align(_arena, LZARENA_DEFAULT_ALIGNMENT, _size))

#define LZARENA_REALLOC(_arena, _ptr, _old_size, _new_size) \
	(lzarena_realloc_align(_arena, _ptr, _old_size, LZARENA_DEFAULT_ALIGNMENT, _new_size))

#endif
