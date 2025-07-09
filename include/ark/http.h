#ifndef ARK_HTTP_H
#define ARK_HTTP_H

#include <stdio.h>

/* Opaque type representing an HTTP message */
typedef struct HttpMessage HttpMessage;

/* Parse HTTP message */
HttpMessage *parse_http_message(void *buff, const char *raw_http_msg, size_t raw_http_msg_length);

#endif /* ARK_HTTP_H */
