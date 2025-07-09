#include <stdio.h>
#include <stdint.h>

#include "ark/shared.h"
#include "ark/server.h"
#include "ark/memory.h"
#include "../src/utils.h"
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
    available_ram();

    ark_server((uint16_t)8080);
    init_resources();

    Memory *memory = reserve_memory(231701558 + KB(4));

    char *request_buff = (char *)memory_in_use(memory);
    void *p = request_buff;

    size_t received = 0;

    while (1) {
        ArkRequestResult result = ark_listen_and_serve(&request_handler, &received, request_buff, &p);
        
        if (result.error) {
            printf("error!\n");
        }
    }
    
    return 0;
}