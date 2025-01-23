#ifndef LZARENA_H
#define LZARENA_H

#include <stddef.h>

#define LZARENA_DEFAULT_ALIGNMENT 8
#define LZARENA_DEFAULT_FACTOR 4

typedef struct lzregion{
    size_t buffer_size;
    void *offset;
    void *buffer;
    struct lzregion *next;
} LZRegion;

typedef struct lzarena{
    struct lzregion *head;
    struct lzregion *tail;
} LZArena;

LZRegion *lzregion_init(size_t buff_size, void *buff);
LZRegion *lzregion_create(size_t size);
void lzregion_destroy(LZRegion *region);

#define LZREGION_FREE(region){       \
    region->offset = region->buffer; \
}

size_t lzregion_available(LZRegion *region);
size_t lzregion_available_alignment(size_t alignment, LZRegion *region);
void *lzregion_alloc_align(size_t size, size_t alignment, LZRegion *region);
void *lzregion_realloc_align(void *ptr, size_t old_size, size_t new_size, size_t alignment, LZRegion *region);

LZArena *lzarena_create();
void lzarena_destroy(LZArena *arena);

void lzarena_report(size_t *used, size_t *size, LZArena *arena);
void lzarena_free_all(LZArena *arena);
void *lzarena_alloc_align(size_t size, size_t alignment, LZArena *arena);
void *lzarena_realloc_align(void *ptr, size_t old_size, size_t new_size, size_t alignment, LZArena *arena);
#define LZARENA_ALLOC(size, arena)(lzarena_alloc_align(size, LZARENA_DEFAULT_ALIGNMENT, arena))
#define LZARENA_REALLOC(ptr, old_size, new_size, arena)(lzarena_realloc_align(ptr, old_size, new_size, LZARENA_DEFAULT_ALIGNMENT, arena))

#endif
