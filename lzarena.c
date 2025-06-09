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

#define REGION_SIZE sizeof(LZRegion)
#define ARENA_SIZE sizeof(LZArena)

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
    if(!ptr){
        return;
    }

    if(allocator){
        allocator->dealloc(ptr, size, allocator->ctx);
    }else{
        free(ptr);
    }
}

static int append_region(size_t size, LZArena *arena){
    size += REGION_SIZE;

    size_t base_size = PAGE_SIZE * LZARENA_DEFAULT_FACTOR;
    size_t buff_len = size > base_size ? (size / base_size + 1) * PAGE_SIZE * 2: base_size;

	LZRegion *region = NULL;
    LZArenaAllocator *allocator = arena->allocator;

    if(allocator){
        void *buff = lzalloc(buff_len, allocator);

        if(!buff){
            return LZARENA_ERR_ALLOC;
        }

        region = lzregion_init(buff_len, buff);
    }else{
        region = lzregion_create(buff_len);
    }

    if(!region){
        return LZARENA_ERR_ALLOC;
    }

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

	uintptr_t region_start = align_forward(buff_start, LZARENA_DEFAULT_ALIGNMENT);
    uintptr_t region_end = region_start + REGION_SIZE;
    uintptr_t chunk_start = align_forward(region_end, LZARENA_DEFAULT_ALIGNMENT);

    assert(chunk_start <= buff_end);
    assert((buff_end - chunk_start) < buff_size);

    size_t chunk_len = (buff_end - chunk_start);
    LZRegion *region = (LZRegion *)region_start;

    region->reset = 0;
    region->region_len = buff_size;
	region->chunk_len = chunk_len;
    region->offset = (void *)chunk_start;
    region->chunk = (void *)chunk_start;
    region->next = NULL;

    return region;
}

LZRegion *lzregion_create(size_t size){
#ifndef LZARENA_BACKEND
    #error "a backend must be defined"
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
    #error "unknown backend"
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
    if(munmap(region, region->region_len) == -1){
        perror(NULL);
    }
#elif LZARENA_BACKEND == LZARENA_BACKEND_VIRTUALALLOC
    VirtualFree(region, 0, MEM_RELEASE);
#else
    #error "unknown backend"
#endif
}

inline size_t lzregion_available(LZRegion *region){
    uintptr_t buff_start = (uintptr_t)region->offset;
    uintptr_t buff_end = buff_start + region->chunk_len;

    assert(buff_start < buff_end);

    return buff_end - buff_start;
}

inline size_t lzregion_available_alignment(size_t alignment, LZRegion *region){
    uintptr_t chunk_start = (uintptr_t)region->chunk;
    uintptr_t chunk_end = chunk_start + region->chunk_len;
    uintptr_t offset = (uintptr_t)region->offset;

    offset = align_forward(offset, alignment);

    return offset >= chunk_end ? 0 : chunk_end - offset;
}

void *lzregion_alloc_align(size_t size, size_t alignment, LZRegion *region){
    size_t available = lzregion_available_alignment(alignment, region);

    if(size > available){
        return NULL;
    }

    uintptr_t offset = (uintptr_t)region->offset;

    offset = align_forward(offset, alignment);
    region->offset = (void *)(offset + size);

    return (void *)offset;
}

void *lzregion_calloc_align(size_t size, size_t alignment, LZRegion *region){
    void *ptr = lzregion_alloc_align(size, alignment, region);

    if(ptr){
        memset(ptr, 0, size);
    }

    return ptr;
}

void *lzregion_realloc_align(void *ptr, size_t old_size, size_t new_size, size_t alignment, LZRegion *region){
    if(new_size <= old_size){
        return ptr;
    }

	void *new_ptr = lzregion_alloc_align(new_size, alignment, region);

	if(ptr){
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
            lzdealloc(current, current->region_len, allocator);
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
		u += current->chunk_len - available;
		s += current->chunk_len;
		current = next;
	}

	*used = u;
	*size = s;
}

inline void lzarena_free_all(LZArena *arena){
    if(!arena->current){
        return;
    }

    if(arena->head == arena->tail){
        arena->current->offset = arena->current->chunk;
    }else{
        arena->reset++;
        arena->current = arena->head;
    }
}

void *lzarena_alloc_align(size_t size, size_t alignment, LZArena *arena){
    while(arena->current && arena->current->next){
        if(arena->current->reset != arena->reset){
            arena->current->reset = arena->reset;
            arena->current->offset = arena->current->chunk;
            break;
        }else if(lzregion_available_alignment(alignment, arena->current) < size){
            arena->current->reset = 0;
            arena->current = arena->current->next;
        }else{
            break;
        }
    }

    LZRegion *current = arena->current;
    size_t available = current ? lzregion_available_alignment(alignment, current) : 0;

    if(current && available >= size){
        return lzregion_alloc_align(size, alignment, current);
    }

    if(append_region(size, arena)){
        return NULL;
    }

    return lzregion_alloc_align(size, alignment, arena->current);
}

void *lzarena_calloc_align(size_t size, size_t alignment, LZArena *arena){
    void *ptr = lzarena_alloc_align(size, alignment, arena);

    if(ptr){
        memset(ptr, 0, size);
    }

    return ptr;
}

void *lzarena_realloc_align(void *ptr, size_t old_size, size_t new_size, size_t alignment, LZArena *arena){
    if(new_size <= old_size){
        return ptr;
    }

	void *new_ptr = lzarena_alloc_align(new_size, alignment, arena);

	if(ptr){
        memcpy(new_ptr, ptr, old_size);
    }

    return new_ptr;
}