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
    char server[20];
    char uri[20];
    Channel *done;
} ServerUri;

typedef struct {
    Memory *memory;
    int fd;
} MFD;

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
    MFD *mfd = (MFD *)v;
    Memory *memory = mfd->memory;
    int client_fd = mfd->fd;

	ServerUri *server_uri = memory_alloc(memory, sizeof(ServerUri));
    memcpy(server_uri->server, "localhost", strlen("localhost"));
    memcpy(server_uri->uri, "/a", strlen("/a"));

    int stack_size = KB(8);
    
    printf("get_a req to /a\n");
    Channel *done = chancreate(sizeof(void*), 0);
    server_uri->done = done;
    ark_task_create(make_request, server_uri, stack_size);
    chanrecv(done, NULL);
    printf("get_a req to /a DONE\n");

	ServerUri *server_uri_1 = memory_alloc(memory, sizeof(ServerUri));
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
    ark_fd_write(client_fd, resbuf, len);
}

void get_b(void *v) {
    MFD *mfd = (MFD *)v;
    Memory *memory = mfd->memory;
    int client_fd = mfd->fd;

	ServerUri *server_uri = memory_alloc(memory, sizeof(ServerUri));
    memcpy(server_uri->server, "localhost", strlen("localhost"));
    memcpy(server_uri->uri, "/b", strlen("/b"));

    int stack_size = KB(8);
    
    printf("get_b req to /b\n");
    Channel *done = chancreate(sizeof(void*), 0);
    server_uri->done = done;
    ark_task_create(make_request, server_uri, stack_size);
    chanrecv(done, NULL);
    printf("get_b req to /b DONE\n");

	ServerUri *server_uri_1 = memory_alloc(memory, sizeof(ServerUri));
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
    ark_fd_write(client_fd, resbuf, len);
}

void handler(void *arg) {
    int fd = *(int *)arg;

    for (;;) {
        ark_fd_await(fd, 'r');
    
        int client_fd;
        ark_client_accept(&client_fd, fd);
        ark_fd_await(client_fd, 'r');

        Memory *memory = reserve_memory(PAGE_SIZE * 15);
        MFD *mfd = memory_alloc(memory, sizeof(MFD));
        mfd->memory = memory;
        mfd->fd = client_fd;

        char *request = memory_alloc(memory, sizeof(char) * 1000);
        ark_fd_read(client_fd, request, 1000);
        size_t request_length = strlen(request);

        ARKHttpMessage http_msg = {0};
        ark_parse_http_request_message(&http_msg, request, request_length);

        char *uri = request + http_msg.uri.start_offset;
        size_t uri_length = http_msg.uri.end_offset - http_msg.uri.start_offset;
        
        if (strncmp(uri, "/a", uri_length) == 0 && uri_length == strlen("/a")) {
            int stack_size = KB(8);
            ark_task_create(get_a, mfd, stack_size);
        } else if (strncmp(uri, "/b", uri_length) == 0 && uri_length == strlen("/b")) {
            int stack_size = KB(8);
            ark_task_create(get_b, mfd, stack_size);
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