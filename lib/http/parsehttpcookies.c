#include "../../include/ark/cookies.h"
#include "../../include/ark/shared.h"

#define IS_COOKIE_TOKEN(c)  true

#define COOKIE_START 0
#define COOKIE_KEY 1
#define COOKIE_VALUE 2

int cookie_count(uint32_t *count, char *cookies, uint32_t cookies_length) {
    if (cookies_length) {
        // we can assume there is at least one cookie
        *count += 1;
    }
    
    uint32_t cursor = 0;
    int ch;
    for (; cursor < cookies_length;) {
        ch = cookies[cursor];
        if (ch == ';') {
            *count += 1;
        }
        cursor++;
    }

    return 0;
}

int parse_cookies(Cookies *ck, char *cookies, uint32_t cookies_length) {
    uint32_t cursor = 0;
    int ch;

    int start;
    
    uint32_t t = 0;
    for (; cursor < cookies_length;) {
        ch = cookies[cursor];
        switch (t) {
            case COOKIE_START: {
                for (;;) {
                    if (IS_COOKIE_TOKEN(ch)) {
                        start = cursor;
                        t = COOKIE_KEY;
                        break;
                    }
                    if (ch != ' ') {
                        return -1;
                    }
                    if (++cursor == cookies_length) {
                        break;
                    }
                    ch = cookies[cursor];
                }
                break;
            }
            case COOKIE_KEY: {
                for (;;) {
                    if (ch == ';') {
                        ck->p[ck->n].k.a = start;
                        ck->p[ck->n].k.b = cursor;

                        cursor += 1; /* skip ; */
                        start = cursor;
                        t = COOKIE_KEY;
                        ck->n++;
                        break;
                    }
                    if (ch == '=') {
                        ck->p[ck->n].k.a = start;
                        ck->p[ck->n].k.b = cursor;

                        cursor += 1; /* skip = */
                        start = cursor;
                        t = COOKIE_VALUE;
                        break;
                    }
                    if (!IS_COOKIE_TOKEN(ch)) {
                        return -1;
                    }
                    if (++cursor == cookies_length) {
                        ck->p[ck->n].k.a = start;
                        ck->p[ck->n].k.b = cursor;
                        ck->n++;
                        break;
                    }
                    ch = cookies[cursor];
                }
                break;
            }
            case COOKIE_VALUE: {
                for (;;) {
                    if (ch == ';') {
                        ck->p[ck->n].v.a = start;
                        ck->p[ck->n].v.b = cursor;
    
                        char separator = cursor;
                        cursor += 1; /* skip ; */
                        start = cursor;
                        t = COOKIE_KEY;
                        ck->n++;
                        break;
                    }
                    if (!IS_COOKIE_TOKEN(ch)) {
                        return -1;
                    }
                    if (++cursor == cookies_length) {
                        ck->p[ck->n].v.a = start;
                        ck->p[ck->n].v.b = cursor;
                        ck->n++;
                        break;
                    }
                    ch = cookies[cursor];
                }
                break;
            }
            default: {
                break;
            }
        }
    }

    return 0;
}