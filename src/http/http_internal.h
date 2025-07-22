#ifndef HTTP_INTERNAL_H
#define HTTP_INTERNAL_H

#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include "../../include/ark/shared.h"
#include "../../include/ark/http.h"
 
#define READ32LE(P)                    \
  (__extension__({                     \
    uint32_t __x;                      \
    memcpy(&__x, P, 32 / 8);           \
    __builtin_bswap32(__x);            \
  }))

#define READ64LE(P)                    \
  (__extension__({                     \
    uint64_t __x;                      \
    memcpy(&__x, P, 64 / 8);           \
    __builtin_bswap32(__x);            \
  }))

#define READ64BE(P)                    \
  (__extension__({                     \
    uint64_t __x;                      \
    memcpy(&__x, P, 64 / 8);           \
    __builtin_bswap64(__x);            \
  }))

#define HttpGet     READ32LE("GET")
#define HttpHead    READ32LE("HEAD")
#define HttpPost    READ32LE("POST")
#define HttpPut     READ32LE("PUT")
#define HttpDelete  READ64LE("DELETE\0")
#define HttpOptions READ64LE("OPTIONS")
#define HttpConnect READ64LE("CONNECT")
#define HttpTrace   READ64LE("TRACE\0\0")

#define HttpStateStart   0
#define HttpStateMethod  1
#define HttpStateUri     2
#define HttpStateVersion 3
#define HttpStateStatus  4
#define HttpStateMessage 5
#define HttpStateName    6
#define HttpStateColon   7
#define HttpStateValue   8
#define HttpStateCr      9
#define HttpStateLf1     10
#define HttpStateLf2     11

int ark_parse_http_request_message(ARKHttpMessage *http_msg, char *raw_http_msg, size_t raw_http_msg_length);

#endif /* HTTP_INTERNAL_H */