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

Memory *reserve_memory(size_t size);
void *memory_alloc(Memory *memory, size_t size);
void memory_reset(Memory *memory, uint8_t *ptr);
void memory_free(Memory *memory);

#endif /* ARK_MEMORY_H */