#include "lzarena.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>

#define LZREGION_SIZE sizeof(LZRegion)
#define BUFF_END(region)((void *)(((char *)region->buffer) + region->buffer_size))

static int is_power_of_two(uintptr_t x){
    return (x & (x - 1)) == 0;
}

static size_t align_size(size_t size, size_t alignment){
    assert(is_power_of_two(alignment));

    if (alignment == 0) return size;

    size_t module = size & (alignment - 1);
    size_t padding = module == 0 ? 0 : alignment - module;

    return padding + size;
}

static int is_aligned_to(void *buff, size_t alignment){
    assert(is_power_of_two(alignment));
    uintptr_t ibuff = (uintptr_t)buff;
    return ibuff % alignment == 0;
}

static uintptr_t align_forward(void *buff, size_t alignment){
    assert(is_power_of_two(alignment));

    uintptr_t ibuff = (uintptr_t)buff;
    size_t module = ibuff & (alignment - 1);;
    size_t padding = module == 0 ? 0 : alignment - module;

    return padding + ibuff;
}

static uintptr_t align_uiptr_forward(uintptr_t addr, size_t alignment){
    assert(is_power_of_two(alignment));

    size_t module = addr & (alignment - 1);
    size_t padding = module == 0 ? 0 : alignment - module;
    
    return padding + addr;
}

#define PAGE_SIZE sysconf(_SC_PAGESIZE)

static int append_region(size_t size, size_t alignment, LZArena *arena){
    size_t page_size = PAGE_SIZE * LZARENA_DEFAULT_FACTOR;
    size_t aligned_size = align_size(size, alignment);
	size_t factor = aligned_size / page_size + 1;
    size_t buffer_size = page_size * LZARENA_DEFAULT_FACTOR * factor;
	LZRegion *region = lzregion_create(buffer_size);
	
    if(region == NULL) return 1;
    
    if(arena->tail)
		arena->tail->next = region;
	else
		arena->head = region;
    
    arena->tail = region;

    return 0;
}

LZRegion *lzregion_init(size_t buffer_size, void *buffer){
	uintptr_t old_region_start = (uintptr_t)buffer;
	uintptr_t buffer_end = old_region_start + buffer_size;
	uintptr_t region_start = align_forward(buffer, LZARENA_DEFAULT_ALIGNMENT);
    
    assert(region_start < buffer_end);
    buffer_size -= region_start - old_region_start;
    
    uintptr_t region_end = region_start + LZREGION_SIZE;
    uintptr_t buff_start = align_uiptr_forward(region_end, LZARENA_DEFAULT_ALIGNMENT);
    assert(buff_start < buffer_end);
    
    buffer_size -= LZREGION_SIZE + (buff_start - region_end);

    LZRegion *region = (LZRegion *)region_start;

	region->buffer_size = buffer_size;
    region->offset = (void *)buff_start;
    region->buffer = (void *)buff_start;
    region->next = NULL;

    return region;
}

LZRegion *lzregion_create(size_t size){
    size += align_size(LZREGION_SIZE, LZARENA_DEFAULT_ALIGNMENT);
    char *buffer = (char *)mmap(
		NULL,
		size,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS,
		0,
		0
    );
    if (buffer == MAP_FAILED) return NULL;
    return lzregion_init(size, buffer);
}

void lzregion_destroy(LZRegion *region){
    if (!region) return;
    size_t buffer_size = align_size(LZREGION_SIZE, LZARENA_DEFAULT_ALIGNMENT) + region->buffer_size;
    memset(region, 0, buffer_size);
    assert(munmap(region, buffer_size) == 0);
}

size_t lzregion_available(LZRegion *region){
    uintptr_t start = (uintptr_t)region->offset;
    uintptr_t end = (uintptr_t)BUFF_END(region);
    return end - start;
}

size_t lzregion_available_alignment(size_t alignment, LZRegion *region){
    uintptr_t offset = (uintptr_t)region->offset;
    
    if(!is_aligned_to(region->offset, alignment))
        offset = align_forward(region->offset, alignment);

    uintptr_t buff_end = (uintptr_t)BUFF_END(region);

    return offset > buff_end ? 0 : buff_end - offset;
}

void *lzregion_alloc_align(size_t size, size_t alignment, LZRegion *region){
    uintptr_t offset_start = (uintptr_t)region->offset;

    if(!is_aligned_to(region->offset, alignment))
        offset_start = align_forward(region->offset, alignment);

    uintptr_t offset_end = offset_start + size;
    uintptr_t buff_end = (uintptr_t)BUFF_END(region);
    
    if(offset_end > buff_end) return NULL;

    region->offset = (void *)offset_end;

    return memset((void *)offset_start, 0, size);
}

void *lzregion_realloc_align(void *ptr, size_t old_size, size_t new_size, size_t alignment, LZRegion *region){
    if(new_size <= old_size) return ptr;
	void *new_ptr = lzregion_alloc_align(new_size, alignment, region);
	if(ptr) memcpy(new_ptr, ptr, old_size);
	return new_ptr;
}

LZArena *lzarena_create(){
    LZArena *arena = (LZArena *)malloc(sizeof(LZArena));
    if (!arena) return NULL;
    arena->head = NULL;
    arena->tail = NULL;
    return arena;
}

void lzarena_destroy(LZArena *arena){
    LZRegion *current = arena->head;
    
    while(current){
		LZRegion *next = current->next;
		lzregion_destroy(current);
		current = next;
	}
	
	free(arena);
}

void lzarena_report(size_t *used, size_t *size, LZArena *arena){
    size_t u = 0;
    size_t s = 0;
    LZRegion *current = arena->head;
    
    while(current){
		LZRegion *next = current->next;
		size_t available = lzregion_available(current);
		u += current->buffer_size - available;
		s += current->buffer_size;
		current = next;
	}
	
	*used = u;
	*size = s;
}

void lzarena_free_all(LZArena *arena){
    LZRegion *current = arena->head;
    
    while(current){
		LZRegion *next = current->next;
		LZREGION_FREE(current);
		current = next;
	}
	
	arena->tail = arena->head;
}

void *lzarena_alloc_align(size_t size, size_t alignment, LZArena *arena){
    if(!arena->tail && append_region(size, alignment, arena)) return NULL;

	while(arena->tail->next){
		size_t available = lzregion_available_alignment(alignment, arena->tail);
		
		if(available < size){
			arena->tail = arena->tail->next;
			continue;
		}
		
		break;
	}
	
    size_t available = lzregion_available_alignment(alignment, arena->tail);
    if(available < size && append_region(size, alignment, arena)) return NULL;

    return lzregion_alloc_align(size, alignment, arena->tail);
}

void *lzarena_realloc_align(void *ptr, size_t old_size, size_t new_size, size_t alignment, LZArena *arena){
    if(new_size <= old_size) return ptr;
	void *new_ptr = lzarena_alloc_align(new_size, alignment, arena);
	if(ptr) memcpy(new_ptr, ptr, old_size);
	return new_ptr;
}
