#include <stdio.h>
#include <stdlib.h>

#include "http_internal.h"
#include "../../include/ark/utils.h"
#include "gethttpheader.c"

#define SHRT_MAX 0x7fff

const char http_token[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x00 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x10 */
    0, 1, 0, 1, 1, 1, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0,  /* 0x20 */ /*│ ! #$%&‘  *+ -. │*/
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,  /* 0x30 */ /*│0123456789      │*/
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  /* 0x40 */ /*│ ABCDEFGHIJKLMNO│*/
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1,  /* 0x50 */ /*│PQRSTUVWXYZ   ^_│*/
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  /* 0x60 */ /*│`abcdefghijklmno│*/
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0,  /* 0x70 */ /*│pqrstuvwxyz | ~ │*/
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x80 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x90 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0xa0 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0xb0 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0xc0 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0xd0 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0xe0 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0xf0 */
};

/** Returns small number for HTTP header, or -1 if not found. */
int get_http_header(const char *str, size_t len) {
  const struct HttpHeaderSlot *slot;
  if ((slot = lookup_http_header(str, len))) {
    return slot->code;
  }
  
  return -1;
}

/** NOTES
 *  - When the same header is present multiple times in the http request we only consider the last one, the user should only send one header and separate the content with comas.
 *  - Standard and custom http headers should be register in the "perfect hash table".
 *  - POST, PUT and PATCH methods must have a Content-Length header, other http methods are not allowed to have a body.
 *  - Only parse http request not response.
 *  - Should I accept POST, PUT and PATCH methods without a Content-Length header but a Transfer-Encoding "chunked"?
 * 
 *  NOTE, All http request messages (start line + headers) end with "\r\n\r\n" and after that is the start of the body
 */
int ark_parse_http_request_message(ARKHttpMessage *http_msg, char *raw_http_msg, size_t raw_http_msg_length) {
    size_t i = 0;
    int http_header = 0;
    int c = 0; /** ASCII character */

    for (; http_msg->cursor < raw_http_msg_length; ++http_msg->cursor) {
        c = raw_http_msg[http_msg->cursor] & 255;
        switch (http_msg->token) {
            case HttpStateStart: {
                if (c == '\r' || c == '\n') {
                    break; /* RFC7230 § 3.5 */
                }
                if (!http_token[c]) {
                    assert(0);
                    return -1;
                }
                http_msg->token = HttpStateMethod;
                http_msg->method = c;
                http_msg->a = 8;
                break;
            }
            case HttpStateMethod: {
                for (;;) {
                    if (c == ' ') {
                        http_msg->a = http_msg->cursor + 1;
                        http_msg->token = HttpStateUri;
                        break;
                    } else if (http_msg->a == 64 || !http_token[c]) {
                        assert(0);
                        return -1;
                    }
                    http_msg->method |= (uint64_t)c << http_msg->a;
                    http_msg->a += 8;
                    if (++http_msg->cursor == raw_http_msg_length) {
                        break;
                    }
                    c = raw_http_msg[http_msg->cursor] & 255;
                }
                break;
            }
            case HttpStateUri: {
                for (;;) {
                    if (c == ' ' || c == '\r' || c == '\n') {
                        if (http_msg->cursor == http_msg->a) {
                            assert(0);
                            return -1;
                        }
                        http_msg->uri.start_offset = http_msg->a;
                        http_msg->uri.end_offset = http_msg->cursor;

                        if (c == ' ') {
                            http_msg->a = http_msg->cursor + 1;
                            http_msg->token = HttpStateVersion;
                        } else {
                            http_msg->version = 9;
                            http_msg->token = c == '\r' ? HttpStateCr : HttpStateLf1;
                        }
                        break;
                    } else if (c < 0x20 || (0x7F <= c && c < 0xA0)) {
                        assert(0);
                        return -1;
                    }
                    if (++http_msg->cursor == raw_http_msg_length) {
                        break;
                    }
                    c = raw_http_msg[http_msg->cursor] & 255;
                }
                break;
            }
            case HttpStateVersion: {
                if (c == ' ' || c == '\r' || c == '\n') {
                    if (http_msg->cursor - http_msg->a == 8 && (READ64BE(raw_http_msg + http_msg->a) & 0xFFFFFFFFFF00FF00) == 0x485454502F002E00 && is_digit(raw_http_msg[http_msg->a + 5]) && is_digit(raw_http_msg[http_msg->a + 7])) {
                        http_msg->version = (raw_http_msg[http_msg->a + 5] - '0') * 10 + (raw_http_msg[http_msg->a + 7] - '0');
                        http_msg->token = c == '\r' ? HttpStateCr : HttpStateLf1;
                    } else {
                        assert(0);
                        return -1;
                    }
                }
                break;
            }
            case HttpStateStatus: {
                for (;;) {
                    if (c == ' ' || c == '\r' || c == '\n') {
                        if (http_msg->status < 100) {
                            assert(0);
                            return -1;
                        }
                        if (c == ' ') {
                            http_msg->a = http_msg->cursor + 1;
                            http_msg->token = HttpStateMessage;
                        } else {
                            http_msg->token = c == '\r' ? HttpStateCr : HttpStateLf1;
                        }
                        break;
                    } else if ('0' <= c && c <= '9') {
                        http_msg->status *= 10;
                        http_msg->status += c - '0';
                        if (http_msg->status > 999) {
                            assert(0);
                            return -1;
                        }
                    } else {
                        assert(0);
                        return -1;
                    }
                    if (++http_msg->cursor == raw_http_msg_length) {
                        break;
                    }
                    c = raw_http_msg[http_msg->cursor] & 255;
                }
                break;
            }
            case HttpStateMessage: {
                for (;;) {
                    if (c == '\r' || c == '\n') {
                        http_msg->message.start_offset = http_msg->a;
                        http_msg->message.end_offset = http_msg->cursor;
                        http_msg->token = c == '\r' ? HttpStateCr : HttpStateLf1;
                        break;
                    } else if (c < 0x20 || (0x7F <= c && c < 0xA0)) {
                        assert(0);
                        return -1;
                    }
                    if (++http_msg->cursor == raw_http_msg_length) {
                        break;
                    }
                    c = raw_http_msg[http_msg->cursor] & 255;
                }
                break;
            }
            case HttpStateCr: {
                if (c != '\n') {
                    assert(0);
                    return -1;
                }
                http_msg->token = HttpStateLf1;
                break;
            }
            case HttpStateLf1: {
                if (c == '\r') {
                    http_msg->token = HttpStateLf2;
                    break;
                } else if (c == '\n') {
                    return ++http_msg->cursor;
                } else if (!http_token[c]) {
                    /* 1. Forbid empty header name (RFC2616 §2.2) */
                    /* 2. Forbid line folding (RFC7230 §3.2.4) */
                    assert(0);
                    return -1;
                }
                http_msg->tmp_key.start_offset = http_msg->cursor;
                http_msg->token = HttpStateName;
                break;
            }
            case HttpStateName: {
                for (;;) {
                    if (c == ':') {
                        http_msg->tmp_key.end_offset = http_msg->cursor;
                        http_msg->token = HttpStateColon;
                        break;
                    } else if (!http_token[c]) {
                        assert(0);
                        return -1;
                    }
                    if (++http_msg->cursor == raw_http_msg_length) {
                        break;
                    }
                    c = raw_http_msg[http_msg->cursor] & 255;
                }
                break;
            }
            case HttpStateColon: {
                if (c == ' ' || c == '\t') {
                    break;
                }
                http_msg->a = http_msg->cursor;
                http_msg->token = HttpStateValue;
                /* fallthrough */
                __attribute__((fallthrough));
            }
            case HttpStateValue: {
                for (;;) {
                    if (c == '\r' || c == '\n') {
                        i = http_msg->cursor;
                        while (i > http_msg->a && (raw_http_msg[i - 1] == ' ' || raw_http_msg[i - 1] == '\t'))
                            --i;
                        if ((http_header = get_http_header(raw_http_msg + http_msg->tmp_key.start_offset, http_msg->tmp_key.end_offset - http_msg->tmp_key.start_offset)) != -1 && (!http_msg->headers[http_header].start_offset /* || !http_repeatable[http_header] */)) {
                            http_msg->headers[http_header].start_offset = http_msg->a;
                            http_msg->headers[http_header].end_offset = i;
                        } else {
                            assert(0);
                            return -1;
                        }
                        http_msg->token = c == '\r' ? HttpStateCr : HttpStateLf1;
                        break;
                    } else if ((c < 0x20 && c != '\t') || (0x7F <= c && c < 0xA0)) {
                        assert(0);
                        return -1;
                    }
                    if (++http_msg->cursor == raw_http_msg_length)
                        break;
                    c = raw_http_msg[http_msg->cursor] & 255;
                }
                break;
            }
            case HttpStateLf2: {
                if (c == '\n') {
                    http_msg->parsed = true;
                    return ++http_msg->cursor;
                }
                assert(0);
                return -1;
            }
            default: {
                __builtin_unreachable();
            }
        }
    }
    
    if (http_msg->cursor < raw_http_msg_length) {
        return 0;
    }

    assert(0);
    return -1;
}