#include <string.h>
#include <sys/mman.h>

#include "memory_internal.h"
#include "../../include/ark/memory.h"

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS 0x20
#endif

Memory *reserve_memory(size_t size) {
    void *raw_memory = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    Memory *memory = memory_setup(raw_memory, size);

    return memory;
}

Memory *memory_setup(void *raw_memory, size_t size) {
    memset(raw_memory, 0, size);

    Memory *memory = raw_memory;

    memory->size = size;
    memory->start = memory;
    memory->current = (uint8_t *)memory + sizeof(Memory);
    memory->used = (uint8_t *)memory->current - (uint8_t *)memory->start;

    return memory;
}

void *memory_alloc(Memory *memory, size_t size) {
    assert(!memory->in_use); /** Cannot allocate while the memory is in use! */

    if ((uint8_t *)memory->current + size > (uint8_t *)memory->start + memory->size) {
        assert(0); /** Out of memory! */
    }

    void *ptr = memory->current;
    memory->current = (uint8_t *)memory->current + size;
    memory->used += size;

    return ptr;
}

void *memory_in_use(Memory *memory) {
    assert(!memory->in_use); /** Memory is already in use! */
    memory->in_use = true;

    return memory->current;
}

void memory_out_of_use(Memory *memory, void *tmp) {
    memory->in_use = false;
    memory->current = (uint8_t *)tmp;
}

void memory_reset(Memory *memory, uint8_t *ptr) {
    size_t set_bytes = (uint8_t *)memory->current - ptr;
    memset(ptr, 0, set_bytes);

    memory->current = ptr;
    memory->used = (uint8_t *)memory->current - (uint8_t *)memory->start;
}

void memory_free(Memory *memory) {
    munmap(memory->start, memory->size);
}

