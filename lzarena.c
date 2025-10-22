#include "lzarena.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>

#ifdef _WIN32
    #include <sysinfoapi.h>
    #include <windows.h>
#elif __linux__
    #include <unistd.h>
    #include <sys/mman.h>
#endif

struct lzregion{
    size_t           block_size;
    void             *block_offset;
    void             *block;
    LZRegion         *next;
    LZArenaAllocator *allocator;
};

typedef struct lzregion_list{
	size_t   len;
	LZRegion *head;
	LZRegion *tail;
}LZRegionList;

struct lzarena{
	size_t           reserved_memory;
    size_t           used_memory;
    LZRegionList     regions;
    LZRegion         *current;
    LZArenaAllocator *allocator;
};

#ifdef _WIN32
    static DWORD windows_page_size(){
        SYSTEM_INFO sysinfo;
        GetSystemInfo(&sysinfo);
        return sysinfo.dwPageSize;
    }

    #define PAGE_SIZE windows_page_size()
#elif __linux__
    #define PAGE_SIZE sysconf(_SC_PAGESIZE)
#endif

#define REGION_SIZE (sizeof(LZRegion))
#define ARENA_SIZE (sizeof(LZArena))

static inline int is_power_of_two(uintptr_t x){
    return (x & (x - 1)) == 0;
}

static inline uintptr_t align_forward(uintptr_t addr, size_t alignment){
    assert(is_power_of_two(alignment));

    size_t module = addr & (alignment - 1);
    size_t padding = module == 0 ? 0 : alignment - module;

    return addr + padding;
}

static inline void *lzalloc(size_t size, LZArenaAllocator *allocator){
    return allocator ? allocator->alloc(size, allocator->ctx) : malloc(size);
}

static inline void lzdealloc(void *ptr, size_t size, LZArenaAllocator *allocator){
    if(allocator){
        allocator->dealloc(ptr, size, allocator->ctx);
    }else{
        free(ptr);
    }
}

static inline void lzregion_list_insert(LZRegionList *list, LZRegion *region){
	LZRegion *tail = list->tail;

	if(tail){
		tail->next = region;
	}else{
		list->head = region;
	}

	list->len++;
	list->tail = region;
}

static LZRegion *create_region(LZArenaAllocator *allocator, size_t requested_size){
    size_t page_size = (size_t)PAGE_SIZE;
    size_t needed_pages = requested_size / page_size;
    size_t pre_needed_size = needed_pages * page_size;
    size_t block_size = ((((size_t) 0) - (pre_needed_size >= requested_size)) & pre_needed_size) |
        				((((size_t) 0) - (pre_needed_size < requested_size)) & ((needed_pages + 1) * page_size));

    return lzregion_create(allocator, block_size);
}

static int append_region(LZArena *arena, size_t size){
    LZRegion *region = create_region(arena->allocator, size);

    if(!region){
        return LZARENA_ERR_ALLOC;
    }

    lzregion_list_insert(&arena->regions, region);

    arena->reserved_memory += region->block_size;
    arena->current = region;

    return LZARENA_OK;
}

LZRegion *lzregion_init(
	LZRegion *region,
	size_t block_size,
	void *block,
	LZArenaAllocator *allocator
){
	region->block_size = block_size;
	region->block_offset = block;
	region->block = block;
	region->next = NULL;
	region->allocator = allocator;

    return region;
}

LZRegion *lzregion_create(LZArenaAllocator *allocator, size_t block_size){
#ifndef LZARENA_BACKEND
    #error "A backend must be defined"
#endif

	LZRegion *region = lzalloc(REGION_SIZE, allocator);

#if LZARENA_BACKEND == LZARENA_BACKEND_MALLOC
    void *block = lzalloc(block_size, allocator);

    if(!region || !block){
    	lzdealloc(region, REGION_SIZE, allocator);
     	lzdealloc(block, block_size, allocator);

      	return NULL;
    }
#elif LZARENA_BACKEND == LZARENA_BACKEND_MMAP
    void *block = mmap(
		NULL,
		block_size,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS,
		-1,
		0
    );

    if(!region || block == MAP_FAILED){
   		lzdealloc(region, REGION_SIZE, allocator);
    	munmap(block, block_size);

     	return NULL;
    }
#elif LZARENA_BACKEND == LZARENA_BACKEND_VIRTUALALLOC
    void *block = VirtualAlloc(
        NULL,
        block_size,
        MEM_COMMIT,
        PAGE_READWRITE
    );

    if(!region || !block){
    	lzdealloc(region, REGION_SIZE, allocator);
    	VirtualFree(block, 0, MEM_RELEASE);

     	return NULL;
    }
#else
    #error "Unknown backend"
#endif

    return lzregion_init(region, block_size, block, allocator);
}

void lzregion_destroy(LZRegion *region){
    if (!region){
        return;
    }

#ifndef LZARENA_BACKEND
    #error "a backend must be defined"
#endif

	LZArenaAllocator *allocator = region->allocator;

#if LZARENA_BACKEND == LZARENA_BACKEND_MALLOC
	lzdealloc(region->block, region->block_size, allocator);
#elif LZARENA_BACKEND == LZARENA_BACKEND_MMAP
    if(munmap(region->block, region->block_size) == -1){
        perror(NULL);
    }
#elif LZARENA_BACKEND == LZARENA_BACKEND_VIRTUALALLOC
    VirtualFree(region->block, 0, MEM_RELEASE);
#else
    #error "unknown backend"
#endif

	lzdealloc(region, REGION_SIZE, allocator);
}

size_t lzregion_available(LZRegion *region){
    uintptr_t offset = (uintptr_t)region->block_offset;
    uintptr_t chunk_end = offset + region->block_size;

    return offset >= chunk_end ? 0 : chunk_end - offset;
}

size_t lzregion_available_alignment(LZRegion *region, size_t alignment){
    uintptr_t chunk_start = (uintptr_t)region->block;
    uintptr_t chunk_end = chunk_start + region->block_size;
    uintptr_t offset = (uintptr_t)region->block_offset;

    offset = align_forward(offset, alignment);

    return offset >= chunk_end ? 0 : chunk_end - offset;
}

void *lzregion_alloc_align(
	LZRegion *region,
	size_t alignment,
	size_t size,
	size_t *out_bytes
){
    if(size == 0){
        return NULL;
    }

    uintptr_t old_block_offset = (uintptr_t)region->block_offset;
    uintptr_t block_end = old_block_offset + region->block_size;
    uintptr_t chunk_start = alignment == 0 ? old_block_offset :
        					align_forward(old_block_offset, alignment);
    uintptr_t chunk_end = chunk_start + size;

    if(chunk_end > block_end){
        return NULL;
    }

    if(out_bytes){
        *out_bytes += size;
    }

    region->block_offset = (void *)chunk_end;

    return (void *)chunk_start;
}

void *lzregion_calloc_align(LZRegion *region, size_t alignment, size_t size){
    void *ptr = lzregion_alloc_align(region, alignment, size, NULL);

    if(ptr){
        memset(ptr, 0, size);
    }

    return ptr;
}

void *lzregion_realloc_align(
	LZRegion *region,
	void *ptr,
	size_t old_size,
	size_t alignment,
	size_t new_size
){
    if(!ptr){
        return lzregion_alloc_align(region, alignment, new_size, NULL);
    }

    if(new_size == 0){
        return NULL;
    }

    if(new_size <= old_size){
        return ptr;
    }

	void *new_ptr = lzregion_alloc_align(region, alignment, new_size, NULL);

	if(new_ptr){
        memcpy(new_ptr, ptr, old_size);
    }

    return new_ptr;
}

LZArena *lzarena_create(LZArenaAllocator *allocator){
    LZArena *arena = (LZArena *)lzalloc(ARENA_SIZE, allocator);

    if (!arena){
        return NULL;
    }

    arena->reserved_memory = 0;
    arena->used_memory = 0;
    arena->regions.len = 0;
    arena->regions.head = NULL;
    arena->regions.tail = NULL;
    arena->current = NULL;
    arena->allocator = allocator;

    return arena;
}

void lzarena_destroy(LZArena *arena){
    if(!arena){
        return;
    }

    LZRegion *current = arena->regions.head;

    while(current){
		LZRegion *next = current->next;

		lzregion_destroy(current);

		current = next;
	}

    lzdealloc(arena, ARENA_SIZE, arena->allocator);
}

void lzarena_report(LZArena *arena, size_t *used, size_t *size){
    size_t u = 0;
    size_t s = 0;
    LZRegion *current = arena->regions.head;

    while(current){
		LZRegion *next = current->next;
		size_t available = lzregion_available(current);
		u += current->block_size - available;
		s += current->block_size;
		current = next;
	}

	*used = u;
	*size = s;
}

inline int lzarena_append_region(LZArena *arena, size_t size){
    return append_region(arena, size);
}

inline void lzarena_free_all(LZArena *arena){
    LZRegion *head = arena->regions.head;

    if(head){
	    head->block_offset = head->block;
	    arena->used_memory = 0;
	    arena->current = head;
    }
}

inline size_t lzarena_reserved_memory(const LZArena *arena){
	return arena->reserved_memory;
}

inline size_t lzarena_used_memory(const LZArena *arena){
	return arena->used_memory;
}

void *lzarena_alloc_align(LZArena *arena, size_t alignment, size_t size){
    LZRegion *selected = NULL;

    while (arena->current){
        LZRegion *current = arena->current;

        if(lzregion_available_alignment(current, alignment) >= size){
            selected = current;
            break;
        }

        LZRegion *next = current->next;

        if(next){
       		next->block_offset = next->block;
        }

        arena->current = next;
    }

    if(selected){
        return lzregion_alloc_align(
        	selected,
         	alignment,
        	size,
           	&arena->used_memory
        );
    }

    if(append_region(arena, size)){
        return NULL;
    }

    return lzregion_alloc_align(
    	arena->current,
     	alignment,
    	size,
       	&arena->used_memory
    );
}

void *lzarena_calloc_align(LZArena *arena, size_t alignment, size_t size){
    void *ptr = lzarena_alloc_align(arena, alignment, size);

    if(ptr){
        memset(ptr, 0, size);
    }

    return ptr;
}

void *lzarena_realloc_align(
	LZArena *arena,
	void *ptr,
	size_t old_size,
	size_t alignment,
	size_t new_size
){
    if(!ptr){
        return lzarena_alloc_align(arena, alignment, new_size);
    }

    if(new_size <= old_size){
        return ptr;
    }

	void *new_ptr = lzarena_alloc_align(arena, alignment, new_size);

	if(new_ptr){
        memcpy(new_ptr, ptr, old_size);
    }

    return new_ptr;
}
