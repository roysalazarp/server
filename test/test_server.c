#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "ark/shared.h"
#include "ark/server.h"
#include "ark/memory.h"
#include "../src/memory/memory_internal.h"

Memory *persisting_memory;

ArkResponse request_handler(const char *raw_request) {
    ArkResponse rendered_response = {0};
    /** Framework user needs to allocate their own request memory arena, they know how much they need for each route */
    Memory *request_memory = reserve_memory(PAGE_SIZE * 200);

    /* here I can use persisting_memory */
    return rendered_response;
}

void init_resources() {
    persisting_memory = reserve_memory(PAGE_SIZE * 200);
}

int main() {
    ark_server_init((uint16_t)8080);
    init_resources();

    while (1) {
        ArkRequest request = ark_retrive_request();
        char response[] =   "HTTP/1.1 200 OK\r\n"
                            "Content-Type: text/html\r\n"
                            "Content-Length: 49\r\n"
                            "Connection: close\r\n"
                            "\r\n"
                            "<html><body><h1>Hello, World!</h1></body></html>\n";
        ark_send_response(request.handle, response, strlen(response));
    }
    
    return 0;
}