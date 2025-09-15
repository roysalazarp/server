#ifndef COOKIES_H_
#define COOKIES_H_

#include <stdint.h>

typedef struct {
  uint32_t a;
  uint32_t b;
} CookieSlice;

typedef struct {
  CookieSlice k;
  CookieSlice v;
} Cookie;

typedef struct {
  uint32_t n;
  Cookie *p;
} Cookies;

int cookie_count(uint32_t *count, char *cookies, uint32_t cookies_length);
int parse_cookies(Cookies *ck, char *cookies, uint32_t cookies_length);

#endif /* COOKIES */
