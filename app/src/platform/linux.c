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

void handler(void *arg) {
    int fd = *(int *)arg;

    for (;;) {
        printf("\n");
        
        fdwait(fd, 'r');

        printf("\n");
        
        int client_fd;
        ark_client_accept(&client_fd, fd);
        fdwait(client_fd, 'r');

        char buf[1000];

        fdread(client_fd, buf, 1000);
        printf("%s\n", buf);

        char resbuf[1000];

        snprintf(resbuf, sizeof resbuf, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 11\r\n\r\nHello World");
        int len = strlen(resbuf);
        fdwrite(client_fd, resbuf, len);

        printf("%s\n", resbuf);
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

    taskcreate(handler, &fd, 256*1024);
	taskscheduler();
    
    // ark_task_init(handler, &fd, 256*1024);
    // ark_server_init((uint16_t)8080);

    // int code;
    // while (1) {
    //     ArkRequest new_request = {0};
    //     code = ark_retrive_request(&new_request);
    //     if (code == ARK_SHUT_SERVER_SIGNAL) { break; }

    //     ARKHttpMessage http_msg = {0};
    //     ark_parse_http_request_message(&http_msg, new_request.raw, new_request.length);
        
    //     char response[] = "HTTP/1.1 200 OK\r\n"
    //                       "Content-Type: text/html\r\n"
    //                       "Content-Length: 49\r\n"
    //                       "Connection: close\r\n"
    //                       "\r\n"
    //                       "<html><body><h1>Hello, World!</h1></body></html>\n";

    //     ark_send_response(new_request.handle, response, strlen(response));
    // }

    printf("\n");
    
    return 0;
}