#include <string.h>
#include <sys/mman.h>
#include <assert.h>

#include "../../include/ark/utils.h"
#include "../../include/ark/memory.h"

fail_safe memory_init(Memory *memory, void *mem, uint64_t mem_size) {
    memset(mem, 0, mem_size);

    memory->size = (size_t)mem_size;
    memory->start = mem;
    memory->current = mem;
    memory->used = (uint8_t *)memory->current - (uint8_t *)memory->start;
}

int try_memory_alloc(char *err, void **ptr, Memory *memory, uint64_t size) {
    CHECK_ERR(!memory->in_use, "Cannot allocate while the memory is in use");
    CHECK_ERR((uint8_t *)memory->current + size <= (uint8_t *)memory->start + memory->size, "Out of memory");

    *ptr = memory->current;
    memory->current = (uint8_t *)memory->current + size;
    memory->used += size;

    return 0;
}

panic memory_alloc(void **ptr, Memory *memory, uint64_t size) {
    ARK_ASSERT(!memory->in_use, 0, "Cannot allocate while the memory is in use");
    ARK_ASSERT((uint8_t *)memory->current + size <= (uint8_t *)memory->start + memory->size, 0, "Out of memory");

    *ptr = memory->current;
    memory->current = (uint8_t *)memory->current + size;
    memory->used += size;
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

