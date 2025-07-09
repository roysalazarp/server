#ifndef ARK__H
#define ARK__H

#include <stdio.h>

typedef struct {
    size_t length;
    char *content;
} ArkResponse;

typedef struct {
    int code;
} ArkError;

typedef struct {
    int error;
} ArkRequestResult;

ArkError ark_server(uint16_t port);
/*
ArkRequestResult ark_listen_and_serve(ArkResponse func(const char[]));
*/
ArkRequestResult ark_listen_and_serve(ArkResponse func(const char[]), size_t *received, char *request_buff, void **p);

#endif /* ARK__H */