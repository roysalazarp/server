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
    memory->current = (u8 *)memory + sizeof(Memory);
    memory->used = (u8 *)memory->current - (u8 *)memory->start;

    return memory;
}

void *memory_alloc(Memory *memory, size_t size) {
    assert(!memory->in_use); /** Cannot allocate while the memory is in use! */

    if ((u8 *)memory->current + size > (u8 *)memory->start + memory->size) {
        assert(0); /** Out of memory! */
    }

    void *ptr = memory->current;
    memory->current = (u8 *)memory->current + size;
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
    memory->current = (u8 *)tmp;
}

void memory_reset(Memory *memory, u8 *ptr) {
    size_t set_bytes = (u8 *)memory->current - ptr;
    memset(ptr, 0, set_bytes);

    memory->current = ptr;
    memory->used = (u8 *)memory->current - (u8 *)memory->start;
}
