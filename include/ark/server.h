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

typedef struct {
    char *raw;
    size_t length;
    int handle;
} ArkRequest;

ArkError ark_server_init(uint16_t port);
ArkRequest ark_retrive_request();
void ark_send_response(int handle, char *response, size_t response_length);

#endif /* ARK__H */