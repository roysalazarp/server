#ifndef SHARED_H
#define SHARED_H

#include <stdint.h>

#define MASK_4        0xFFFFFFFF00000000
#define MASK_5        0xFFFFFFFFFF000000
#define MASK_6        0xFFFFFFFFFFFF0000
#define MASK_7        0xFFFFFFFFFFFFFF00
#define MASK_8        0xFFFFFFFFFFFFFFFF

#define KB(value) ((value) * 1024)
#define MB(value) (KB(value) * 1024)
#define GB(value) (MB(value) * 1024)

#define PAGE_SIZE KB(4)

#define array_count(array) (sizeof(array) / sizeof((array)[0]))

typedef uint8_t boolean;
#define true 1
#define false 0

#define PATH_MAX 4096

typedef struct {
    char *data;
    uint32_t length;
} String;

#define U32_BITS 32

#define MASK_INDEX(i)       ((i) / U32_BITS)
#define BIT_POSITION(i)     ((i) % U32_BITS)

#define IS_U32_BIT_ON(mask, i)    ((mask[MASK_INDEX(i)] & (1U << BIT_POSITION(i))) != 0)
#define IS_U32_BIT_OFF(mask, i)   ((mask[MASK_INDEX(i)] & (1U << BIT_POSITION(i))) == 0)

#define TURN_OFF_U32_BIT(mask, i)    (mask[MASK_INDEX(i)] &= ~(1U << BIT_POSITION(i)))
#define TURN_ON_U32_BIT(mask, i)     (mask[MASK_INDEX(i)] |=  (1U << BIT_POSITION(i)))

#endif /* SHARED_H */