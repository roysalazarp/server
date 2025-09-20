#ifndef ARK_MEMORY_H
#define ARK_MEMORY_H

#include <stdio.h>
#include "shared.h"

typedef struct {
    size_t size;
    size_t used;
    void *start;
    void *current;
    boolean in_use;
} Memory;

fail_safe memory_init(Memory *memory, void *mem, uint64_t mem_size);
panic memory_alloc(void **ptr, Memory *memory, uint64_t size);
int try_memory_alloc(char *err, void **ptr, Memory *memory, uint64_t size);
void memory_reset(Memory *memory, uint8_t *ptr);
void memory_free(Memory *memory);
void *memory_in_use(Memory *memory);
void memory_out_of_use(Memory *memory, void *tmp);

#define LOCK_MEMORY(memory) memory_in_use(memory)
#define UNLOCK_MEMORY(memory, ptr) memory_out_of_use(memory, ptr)

#endif /* ARK_MEMORY_H */