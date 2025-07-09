#ifndef MEMORY_H
#define MEMORY_H

#include <stdio.h>
#include "../../include/ark/memory.h"

#define LOCK_MEMORY(memory) memory_in_use(memory)
#define UNLOCK_MEMORY(memory, ptr) memory_out_of_use(memory, ptr)

Memory *memory_setup(void *raw_memory, size_t size);

void *memory_in_use(Memory *memory);
void memory_out_of_use(Memory *memory, void *tmp);

#endif /* MEMORY_H */