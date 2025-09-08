#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>

#include "ark/shared.h"
#include "ark/server.h"
#include "ark/memory.h"
#include "ark/http.h"
#include "ark/template.h"
#include "ark/utils.h"
#include "ark/task.h"

#define ARK_MAX_HTTP_REQUEST_SIZE MB(300)
#define ARK_MAX_QUEUED_HTTP_REQUESTS 500

#define MYDIR "./assets"

#define PAGE_SIZE KB(4)
#define MAX_REQUEST_MSG_SIZE KB(2)

typedef struct {
    char server[20];
    char uri[20];
    Channel *done;
} ServerUri;

typedef struct {
    Memory *memory;
    int fd;
} RequestCtx;

ArkComponentArray component_array;

void make_request(void *v) {
    ServerUri *server_uri = (ServerUri *)v;

    int filedescriptor, n;
	char buf[512];
	
    if((filedescriptor = netdial(TCP, server_uri->server, 3000)) < 0){
        fprintf(stderr, "dial %s: %s (%s)\n", server_uri->server, strerror(errno), taskgetstate());
        assert(0);
    }
    snprintf(buf, sizeof buf, "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", server_uri->uri, server_uri->server);
    ark_fd_write(filedescriptor, buf, strlen(buf));
    while((n = ark_fd_read(filedescriptor, buf, sizeof buf)) > 0)
        ;
    close(filedescriptor);
    chansend(server_uri->done, NULL);
}

void get_a(void *v) {
    RequestCtx *request_ctx = (RequestCtx *)v;

	ServerUri *server_uri = memory_alloc(request_ctx->memory, sizeof(ServerUri));
    memcpy(server_uri->server, "localhost", strlen("localhost"));
    memcpy(server_uri->uri, "/a", strlen("/a"));

    int stack_size = KB(8);
    
    printf("get_a req to /a\n");
    Channel *done = chancreate(sizeof(void*), 0);
    server_uri->done = done;
    ark_task_create(make_request, server_uri, stack_size);
    chanrecv(done, NULL);
    printf("get_a req to /a DONE\n");

	ServerUri *server_uri_1 = memory_alloc(request_ctx->memory, sizeof(ServerUri));
    memcpy(server_uri_1->server, "localhost", strlen("localhost"));
    memcpy(server_uri_1->uri, "/b", strlen("/b"));

    printf("get_a req to /b\n");
    Channel *done_1 = chancreate(sizeof(void*), 0);
    server_uri_1->done = done_1;
    ark_task_create(make_request, server_uri_1, stack_size);
    chanrecv(done_1, NULL);
    printf("get_a req to /b DONE\n");

    char resbuf[1000];
    snprintf(resbuf, sizeof resbuf, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 11\r\n\r\nHello World");
    int len = strlen(resbuf);
    ark_fd_write(request_ctx->fd, resbuf, len);
}

void get_b(void *v) {
    RequestCtx *request_ctx = (RequestCtx *)v;

	ServerUri *server_uri = memory_alloc(request_ctx->memory, sizeof(ServerUri));
    memcpy(server_uri->server, "localhost", strlen("localhost"));
    memcpy(server_uri->uri, "/b", strlen("/b"));

    int stack_size = KB(8);
    
    printf("get_b req to /b\n");
    Channel *done = chancreate(sizeof(void*), 0);
    server_uri->done = done;
    ark_task_create(make_request, server_uri, stack_size);
    chanrecv(done, NULL);
    printf("get_b req to /b DONE\n");

	ServerUri *server_uri_1 = memory_alloc(request_ctx->memory, sizeof(ServerUri));
    memcpy(server_uri_1->server, "localhost", strlen("localhost"));
    memcpy(server_uri_1->uri, "/a", strlen("/a"));

    printf("get_b req to /a\n");
    Channel *done_1 = chancreate(sizeof(void*), 0);
    server_uri_1->done = done_1;
    ark_task_create(make_request, server_uri_1, stack_size);
    chanrecv(done_1, NULL);
    printf("get_b req to /a DONE\n");

    char resbuf[1000];
    snprintf(resbuf, sizeof resbuf, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 11\r\n\r\nHello World");
    int len = strlen(resbuf);
    ark_fd_write(request_ctx->fd, resbuf, len);
}

int string_compare(char *str, char *str2, size_t str_length) {
    if (strncmp(str, str2, str_length) == 0 && strlen(str2) == str_length) {
        return 1;
    }
    return 0;
}

int find_component(String *component, char *name) {
    uint32_t i;
    for (i = 0; i < component_array.count; i++) {
        if (string_compare(component_array.names[i].data, name, component_array.names[i].length)) {
            *component = component_array.components[i];
            return 0;
        }
    }
    return -1;
}

void not_found(void *v) {
    RequestCtx *request_ctx = (RequestCtx *)v;

    String component = {0};
    find_component(&component, "not_found");
    assert(component.length);

    char response[KB(2)];
    bzero(response, KB(2));
    sprintf(response,
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: %d\r\n"
        "Content-Type: text/html\r\n\r\n"
        "%s",
        component.length, component.data);
    
    int response_length = strlen(response);
    ark_fd_write(request_ctx->fd, response, response_length);
    
    close(request_ctx->fd);
    memory_free(request_ctx->memory);
}

void handler(void *arg) {
    int fd = *(int *)arg;

    for (;;) {
        ark_fd_await(fd, 'r');
        
        Memory *memory = reserve_memory(PAGE_SIZE * 15);
        RequestCtx *request_ctx = memory_alloc(memory, sizeof(RequestCtx));
        request_ctx->memory = memory;
        
        ark_client_accept(&request_ctx->fd, fd);
        ark_fd_await(request_ctx->fd, 'r');

        char *request = memory_alloc(memory, sizeof(char) * MAX_REQUEST_MSG_SIZE);
        ark_fd_read(request_ctx->fd, request, MAX_REQUEST_MSG_SIZE);
        size_t request_length = strlen(request);

        ARKHttpMessage http_msg = {0};
        ark_parse_http_request_message(&http_msg, request, request_length);

        char *uri = request + http_msg.uri.start_offset;
        size_t uri_length = http_msg.uri.end_offset - http_msg.uri.start_offset;
        
        if (strncmp(uri, "/a", uri_length) == 0 && uri_length == strlen("/a")) {
            ark_task_create(get_a, request_ctx, KB(8));
        } else if (strncmp(uri, "/b", uri_length) == 0 && uri_length == strlen("/b")) {
            ark_task_create(get_b, request_ctx, KB(8));
        } else {
            ark_task_create(not_found, request_ctx, KB(8));
        }
    }
}

int main() {
    setbuf(stdout, NULL);

    uint32_t i;
    
    uint32_t num_of_filepaths = 0;
    ark_count_files(&num_of_filepaths, MYDIR);
    
    Memory *memory = reserve_memory(PAGE_SIZE * 50);
    
    size_t *array_of_lengths = memory_alloc(memory, num_of_filepaths * sizeof(size_t));
    ark_list_file_sizes(array_of_lengths, num_of_filepaths, MYDIR);
    
    char **array_of_filepaths = memory_alloc(memory, num_of_filepaths * sizeof(char *));
    for (i = 0; i < num_of_filepaths; i++) {
        array_of_filepaths[i] = (char *)memory_alloc(memory, array_of_lengths[i] * sizeof(char));
    }

    ark_list_files(array_of_filepaths, num_of_filepaths, MYDIR);

    char **array_of_file_contents = memory_alloc(memory, num_of_filepaths * sizeof(char *));

    for (i = 0; i < num_of_filepaths; i++) {
        char *filepath = array_of_filepaths[i];
        
        size_t file_size = ark_get_file_size(filepath);
        char *file_content = memory_alloc(memory, file_size + 1);
            
        FILE *file = fopen(filepath, "r");
        size_t read_size = fread(file_content, sizeof(char), file_size, file);
        assert(read_size == file_size);
        file_content[file_size + 1] = '\0';
        fclose(file);

        array_of_file_contents[i] = file_content;
    }
    
    uint32_t mask_size = (num_of_filepaths / U32_BITS) ? (num_of_filepaths / U32_BITS) : 1; /* ensure a minimum */
    uint32_t *html_file_mask = memory_alloc(memory, mask_size * sizeof(uint32_t));

    ark_make_html_file_mask(html_file_mask, array_of_filepaths, num_of_filepaths);
    
    ark_validate_templates(html_file_mask, array_of_filepaths, array_of_file_contents, num_of_filepaths);
    
    ark_compile_templates(&component_array, html_file_mask, array_of_filepaths, array_of_file_contents, num_of_filepaths);
    
    int fd;
    ark_setup_server(&fd, (uint16_t)8080);

    ark_task_create(handler, &fd, KB(8)); /** NOTE: Keep an eye that the stack size does not become a problem */
	ark_task_run_scheduler();
    
    return 0;
}