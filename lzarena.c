#include "lzarena.h"
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

    size_t module = addr & (alignment - 1);;
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

static LZRegion *create_region(size_t requested_size, LZArenaAllocator *allocator){
    requested_size += REGION_SIZE;

    size_t page_size = (size_t)PAGE_SIZE;
    size_t needed_pages = requested_size / page_size;
    size_t pre_needed_size = needed_pages * page_size;

    size_t needed_size =
        ((((size_t) 0) - (pre_needed_size >= requested_size)) & pre_needed_size) |
        ((((size_t) 0) - (pre_needed_size < requested_size)) & ((needed_pages + 1) * page_size));

    if(allocator){
        void *buff = lzalloc(needed_size, allocator);

        if(!buff){
            return NULL;
        }

        return lzregion_init(needed_size, buff);
    }

    return lzregion_create(needed_size);
}

static int append_region(size_t size, LZArena *arena){
    LZRegion *region = create_region(size, arena->allocator);

    if(!region){
        return LZARENA_ERR_ALLOC;
    }

    region->reset = arena->reset;

    if(arena->tail){
        arena->tail->next = region;
    }else{
        arena->head = region;
    }

    arena->tail = region;
    arena->current = region;

    return LZARENA_OK;
}

LZRegion *lzregion_init(size_t buff_size, void *buffer){
	uintptr_t buff_start = (uintptr_t)buffer;
	uintptr_t buff_end = buff_start + buff_size;

	uintptr_t region_start = buff_start;
    uintptr_t region_end = region_start + REGION_SIZE;
    uintptr_t chunk_start = region_end;

    assert(chunk_start <= buff_end);

    size_t chunk_size = buff_end - chunk_start;
    LZRegion *region = (LZRegion *)region_start;

    region->reset = 0;
    region->region_size = buff_size;
	region->chunk_size = chunk_size;
    region->offset = (void *)chunk_start;
    region->chunk = (void *)chunk_start;
    region->next = NULL;

    return region;
}

LZRegion *lzregion_create(size_t size){
#ifndef LZARENA_BACKEND
    #error "A backend must be defined"
#endif

#if LZARENA_BACKEND == LZARENA_BACKEND_MALLOC
    char *buffer = (char *)malloc(size);

    if(!buffer){
        return NULL;
    }
#elif LZARENA_BACKEND == LZARENA_BACKEND_MMAP
    char *buffer = (char *)mmap(
		NULL,
		size,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS,
		-1,
		0
    );

    if(buffer == MAP_FAILED){
        return NULL;
    }
#elif LZARENA_BACKEND == LZARENA_BACKEND_VIRTUALALLOC
    char *buffer = (char *)VirtualAlloc(
        NULL,
        size,
        MEM_COMMIT,
        PAGE_READWRITE
    );

    if(!buffer){
        return NULL;
    }
#else
    #error "Unknown backend"
#endif

    return lzregion_init(size, buffer);
}

void lzregion_destroy(LZRegion *region){
    if (!region){
        return;
    }

#ifndef LZARENA_BACKEND
    #error "a backend must be defined"
#endif

#if LZARENA_BACKEND == LZARENA_BACKEND_MALLOC
    free(region);
#elif LZARENA_BACKEND == LZARENA_BACKEND_MMAP
    if(munmap(region, region->region_size) == -1){
        perror(NULL);
    }
#elif LZARENA_BACKEND == LZARENA_BACKEND_VIRTUALALLOC
    VirtualFree(region, 0, MEM_RELEASE);
#else
    #error "unknown backend"
#endif
}

size_t lzregion_available(LZRegion *region){
    uintptr_t offset = (uintptr_t)region->offset;
    uintptr_t chunk_end = offset + region->chunk_size;

    return offset >= chunk_end ? 0 : chunk_end - offset;
}

size_t lzregion_available_alignment(size_t alignment, LZRegion *region){
    uintptr_t chunk_start = (uintptr_t)region->chunk;
    uintptr_t chunk_end = chunk_start + region->chunk_size;
    uintptr_t offset = (uintptr_t)region->offset;

    offset = align_forward(offset, alignment);

    return offset >= chunk_end ? 0 : chunk_end - offset;
}

void *lzregion_alloc_align(size_t size, size_t alignment, LZRegion *region, size_t *out_bytes){
    if(size == 0){
        return NULL;
    }

    uintptr_t old_offset = (uintptr_t)region->offset;
    uintptr_t chunk_end = old_offset + region->chunk_size;
    uintptr_t area_start = align_forward(old_offset, alignment);
    uintptr_t area_end = area_start + size;

    if(area_end > chunk_end){
        return NULL;
    }

    if(out_bytes){
        *out_bytes += size;
    }

    region->offset = (void *)area_end;

    return (void *)area_start;
}

void *lzregion_calloc_align(size_t size, size_t alignment, LZRegion *region){
    void *ptr = lzregion_alloc_align(size, alignment, region, NULL);

    if(ptr){
        memset(ptr, 0, size);
    }

    return ptr;
}

void *lzregion_realloc_align(void *ptr, size_t old_size, size_t new_size, size_t alignment, LZRegion *region){
    if(!ptr){
        return lzregion_alloc_align(new_size, alignment, region, NULL);
    }

    if(new_size == 0){
        return NULL;
    }

    if(new_size <= old_size){
        return ptr;
    }

	void *new_ptr = lzregion_alloc_align(new_size, alignment, region, NULL);

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

    arena->reset = 0;
    arena->allocted_bytes = 0;
    arena->head = NULL;
    arena->tail = NULL;
    arena->current = NULL;
    arena->allocator = allocator;

    return arena;
}

void lzarena_destroy(LZArena *arena){
    if(!arena){
        return;
    }

    LZArenaAllocator *allocator = arena->allocator;
    LZRegion *current = arena->head;

    while(current){
		LZRegion *next = current->next;

        if(allocator){
            lzdealloc(current, current->region_size, allocator);
        }else{
            lzregion_destroy(current);
        }

		current = next;
	}

    lzdealloc(arena, ARENA_SIZE, allocator);
}

void lzarena_report(size_t *used, size_t *size, LZArena *arena){
    size_t u = 0;
    size_t s = 0;
    LZRegion *current = arena->head;

    while(current){
		LZRegion *next = current->next;
		size_t available = lzregion_available(current);
		u += current->chunk_size - available;
		s += current->chunk_size;
		current = next;
	}

	*used = u;
	*size = s;
}

inline int lzarena_append_region(size_t size, LZArena *arena){
    return append_region(size, arena);
}

inline void lzarena_free_all(LZArena *arena){
    if(!arena->current){
        return;
    }

    arena->allocted_bytes = 0;

    if(arena->head == arena->tail){
        arena->current->offset = arena->current->chunk;
    }else{
        arena->reset++;
        arena->current = arena->head;
    }
}

void *lzarena_alloc_align(size_t size, size_t alignment, LZArena *arena){
    LZRegion *selected = NULL;

    while (arena->current){
        LZRegion *current = arena->current;
        size_t arena_reset = arena->reset;
        size_t current_reset = current->reset;

        current->reset =
            ((((reset_t) 0) - (current_reset < arena_reset)) & arena_reset) |
            ((((reset_t) 0) - (current_reset == arena_reset)) & current_reset);

        current->offset =
            (void *)(((((uintptr_t)0) - (current_reset != arena_reset)) & ((uintptr_t)current->chunk)) |
            ((((uintptr_t)0) - (current_reset == arena_reset)) & ((uintptr_t)current->offset)));

        if(lzregion_available_alignment(alignment, current) >= size){
            selected = current;
            break;
        }

        arena->current = current->next;
    }

    if(selected){
        return lzregion_alloc_align(size, alignment, selected, &arena->allocted_bytes);
    }

    if(append_region(size, arena)){
        return NULL;
    }

    return lzregion_alloc_align(size, alignment, arena->current, &arena->allocted_bytes);
}

void *lzarena_calloc_align(size_t size, size_t alignment, LZArena *arena){
    void *ptr = lzarena_alloc_align(size, alignment, arena);

    if(ptr){
        memset(ptr, 0, size);
    }

    return ptr;
}

void *lzarena_realloc_align(void *ptr, size_t old_size, size_t new_size, size_t alignment, LZArena *arena){
    if(!ptr){
        return lzarena_alloc_align(new_size, alignment, arena);
    }

    if(new_size <= old_size){
        return ptr;
    }

	void *new_ptr = lzarena_alloc_align(new_size, alignment, arena);

	if(new_ptr){
        memcpy(new_ptr, ptr, old_size);
    }

    return new_ptr;
}