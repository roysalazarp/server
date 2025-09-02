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

typedef struct {
    Memory *memory;
    char server[20];
    char uri[20];
    int fd;
} ServerUri;

void request_external_server(void *v) {
	int filedescriptor, n;
	char buf[512];

    ServerUri *server_uri = v;
	
    if((filedescriptor = netdial(TCP, server_uri->server, 3000)) < 0){
        fprintf(stderr, "dial %s: %s (%s)\n", server_uri->server, strerror(errno), taskgetstate());
        assert(0);
    }
    snprintf(buf, sizeof buf, "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", server_uri->uri, server_uri->server);
    ark_fd_write(filedescriptor, buf, strlen(buf));
    while((n = ark_fd_read(filedescriptor, buf, sizeof buf)) > 0)
        ;
    close(filedescriptor);

    char resbuf[1000];
    snprintf(resbuf, sizeof resbuf, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 11\r\n\r\nHello World");
    int len = strlen(resbuf);
    ark_fd_write(server_uri->fd, resbuf, len);

    memory_free(server_uri->memory);
}

void handler(void *arg) {
    int fd = *(int *)arg;

    for (;;) {
        Memory *memory = reserve_memory(PAGE_SIZE * 15);

        ark_fd_await(fd, 'r');
    
        int client_fd;
        ark_client_accept(&client_fd, fd);
        ark_fd_await(client_fd, 'r');

        char *request = memory_alloc(memory, sizeof(char) * 1000);
        ark_fd_read(client_fd, request, 1000);
        size_t request_length = strlen(request);

        ARKHttpMessage http_msg = {0};
        ark_parse_http_request_message(&http_msg, request, request_length);

        char *uri = request + http_msg.uri.start_offset;
        size_t uri_length = http_msg.uri.end_offset - http_msg.uri.start_offset;
        
        if (strncmp(uri, "/a", uri_length) == 0 && uri_length == strlen("/a")) {
            ServerUri *server_uri = memory_alloc(memory, sizeof(ServerUri));
            memcpy(server_uri->server, "localhost", strlen("localhost"));
            memcpy(server_uri->uri, "/a", strlen("/a"));
            server_uri->fd = client_fd;
            server_uri->memory = memory;

            int stack_size = KB(8);
            ark_task_create(request_external_server, server_uri, stack_size);
        } else if (strncmp(uri, "/b", uri_length) == 0 && uri_length == strlen("/b")) {
            ServerUri *server_uri = memory_alloc(memory, sizeof(ServerUri));
            memcpy(server_uri->server, "localhost", strlen("localhost"));
            memcpy(server_uri->uri, "/b", strlen("/b"));
            server_uri->fd = client_fd;
            server_uri->memory = memory;
            
            int stack_size = KB(8);
            ark_task_create(request_external_server, server_uri, stack_size);
        } else {
            // send 404
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

    ArkComponentArray component_array = {0};
    ark_compile_templates(&component_array, array_of_filepaths, array_of_file_contents, num_of_filepaths);
    
    int fd;
    ark_setup_server(&fd, (uint16_t)8080);

    int stack_size = KB(8); /** NOTE: Keep an eye that the stack size does not become a problem */
    ark_task_create(handler, &fd, stack_size); 
	ark_task_run_scheduler();
    
    return 0;
}