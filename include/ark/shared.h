#ifndef SHARED_H
#define SHARED_H

#include <stdint.h>

#define KB(value) ((value) * 1024)
#define MB(value) (KB(value) * 1024)
#define GB(value) (MB(value) * 1024)

#define PAGE_SIZE KB(4)

#define array_count(array) (sizeof(array) / sizeof((array)[0]))

typedef uint8_t boolean;
#define true 1
#define false 0

#ifdef DEBUG
#define assert(condition) \
    do { \
        if (!(condition)) { \
            volatile int* crash = (void*)0; \
            *crash = 0; \
        } \
    } while(0)
#else
#define assert(condition)
#endif

#define PATH_MAX 4096

typedef struct {
    char *data;
    size_t length;
} String;

#endif /* SHARED_H */