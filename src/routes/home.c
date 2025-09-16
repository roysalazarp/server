#include <string.h>
#include <assert.h>

#include "ark/utils.h"

#include "routes.h"
#include "../globals.h"

void home(void *v) {
    RequestCtx *rctx = (RequestCtx *)v;

    int user_id = 0;
    is_authenticated(&user_id, rctx);

    uint32_t index = 0;
    find_element_index(&index, component_name_arr, "home", component_arr_length);
    char *component = component_content_arr[index];
    uint32_t component_length = strlen(component);
    assert(component_length);

    char response[KB(8)];
    bzero(response, KB(8));
    sprintf(response,
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: %d\r\n"
        "Content-Type: text/html\r\n\r\n"
        "%s",
        component_length, component);
    
    int response_length = strlen(response);
    ark_fd_write(rctx->ssl, rctx->fd, response, response_length);
    
    SSL_shutdown(rctx->ssl);
    SSL_free(rctx->ssl);
    close(rctx->fd);
    memory_free(rctx->memory);
}
