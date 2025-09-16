#ifndef ROUTES_H
#define ROUTES_H

#include <stdint.h>
#include <openssl/ssl.h>

#include "ark/http.h"
#include "ark/memory.h"

typedef struct {
    Memory *memory;
    int fd;
    struct HttpMessage *msg;
    char *raw_request;
    uint32_t raw_request_length;
    SSL *ssl;
} RequestCtx;

void login_create_session(void *v);
void signup_create_account(void *v);
void home(void *v);
void login(void *v);
void logout(void *v);
void signup(void *v);
void not_found(void *v);

#endif /* ROUTES_H */