#ifndef ARK__H
#define ARK__H

#include <stdio.h>

#define ARK_SHUT_SERVER_SIGNAL 2

typedef struct {
    char *raw;
    size_t length;
    uint32_t handle;
} ArkRequest;

typedef struct {
    char *domain_name;
    size_t domain_name_length;
    char *request;
    size_t request_length;
    uint16_t port;
} ArkFetchTaskDetails;

typedef struct {
    uint8_t *buffer;
    size_t buffer_size;
    size_t received_bytes;
    boolean headers_checked;
} RawHTTPMessage;

typedef struct {
    RawHTTPMessage *responses;
    uint32_t count;
} ArkFetchResponse;

int ark_server_init(uint16_t port);
int ark_retrive_request(ArkRequest *ark_request);
int ark_fetch(ArkFetchResponse *response, uint32_t handle, ArkFetchTaskDetails *tasks, uint32_t tasks_count);
int ark_send_response(uint32_t handle, char *response, size_t response_length);

#endif /* ARK__H */