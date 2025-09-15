#ifndef COSMOPOLITAN_LIBC_STR_SLICE_H_
#define COSMOPOLITAN_LIBC_STR_SLICE_H_
// #include "libc/stdbool.h"
// #include "libc/str/str.h"

#include <stdbool.h>
#include <string.h>

#define SlicesEqual(a, n, b, m)       \
  ({                                  \
    /* if n is an expression, __n will cache the result of the n expression */ \
    /* no need to do the same thing for m since it's only used once */         \
    size_t __n = (n);                 \
    __n == (m) && !memcmp(a, b, __n); \
  })

#define SlicesEqualCase(a, n, b, m)       \
  ({                                      \
    size_t __n = (n);                     \
    __n == (m) && !memcasecmp(a, b, __n); \
  })

int CompareSlices(const char *, size_t, const char *, size_t);
int CompareSlicesCase(const char *, size_t, const char *, size_t);

#endif /* COSMOPOLITAN_LIBC_STR_SLICE_H_ */
