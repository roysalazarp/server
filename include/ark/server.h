#ifndef ARK__H
#define ARK__H

#include <stdio.h>

#define ARK_SHUT_SERVER_SIGNAL 2

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

typedef struct {
    char *domain_name;
    size_t domain_name_length;
    char *request;
    size_t request_length;
    uint16_t port;
} ArkFetchTaskDetails;

typedef struct {
    u8 *buffer;
    size_t buffer_size;
    size_t received_bytes;
    boolean headers_checked;
} RawHTTPMessage;

typedef struct {
    RawHTTPMessage *responses;
    int count;
} ArkFetchResponse;

int ark_server_init(uint16_t port);
int ark_retrive_request(ArkRequest *ark_request);
int ark_fetch(ArkFetchResponse *response, int handle, ArkFetchTaskDetails *tasks, int tasks_count);
int ark_send_response(int handle, char *response, size_t response_length);

#endif /* ARK__H */