#ifndef SHARED_H
#define SHARED_H

#define KB(value) ((value) * 1024)
#define MB(value) (KB(value) * 1024)
#define GB(value) (MB(value) * 1024)

#define PAGE_SIZE KB(4)

#define NULL ((void *)0)

#define array_count(array) (sizeof(array) / sizeof((array)[0]))

typedef signed char s8;
typedef unsigned char u8;
typedef short s16;
typedef unsigned short u16;
typedef int s32;
typedef unsigned int u32;
typedef long long s64;
typedef unsigned long long u64;

typedef float f32;
typedef double f64;

typedef u8 boolean;

#define true 1
#define false 0

#ifdef DEBUG
#define ASSERT(condition) \
    do { \
        if (!(condition)) { \
            volatile int* crash = (void*)0; \
            *crash = 0; \
        } \
    } while(0)
#else
#define ASSERT(condition)
#endif

#endif /* SHARED_H */