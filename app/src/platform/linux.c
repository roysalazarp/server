#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "ark/shared.h"
#include "ark/server.h"
#include "ark/memory.h"
#include "ark/http.h"
#include "ark/utils.h"

#define ARK_MAX_HTTP_REQUEST_SIZE MB(300)
#define ARK_MAX_QUEUED_HTTP_REQUESTS 500

#define MYDIR "./assets"

#define PAGE_SIZE KB(4)

typedef struct {
    String *filepath_array;
    String *content_array;
    u32 count;
} AssetSOA;

int main() {
    int i;

    ark_server_init((uint16_t)8080);
    
    int filepath_array_length = 0;
    size_t max_filepath_item_length = 0;
    ark_file_count(&filepath_array_length, &max_filepath_item_length, MYDIR);
    
    Memory *memory = reserve_memory(PAGE_SIZE * 50);
    
    /** flat refers to null-terminated */
    char *flat_filepath_array = memory_alloc(memory, filepath_array_length * max_filepath_item_length);
    size_t *filepath_length_array = memory_alloc(memory, filepath_array_length * sizeof(size_t));
    
    ark_list_files(flat_filepath_array, filepath_length_array, filepath_array_length, max_filepath_item_length, "./assets");
    
    String *filepath_array = memory_alloc(memory, filepath_array_length * sizeof(String));
    for (i = 0; i < filepath_array_length; i++) {
        filepath_array[i].data = flat_filepath_array + (i * max_filepath_item_length);
        filepath_array[i].length = *(filepath_length_array + (i * sizeof(size_t)));
    }

    String *file_content_array = memory_alloc(memory, filepath_array_length * sizeof(String));

    for (i = 0; i < filepath_array_length; i++) {
        char *filepath = filepath_array[i].data;
        
        size_t file_size = ark_file_size(filepath);
        char *file_content = memory_alloc(memory, file_size);
            
        FILE *file = fopen(filepath, "r");
        size_t read_size = fread(file_content, sizeof(char), file_size, file);
        assert(read_size == file_size);
        fclose(file);

        file_content_array[i].data = file_content;
        file_content_array[i].length = file_size;
    }

    AssetSOA assets_soa = {0};
    assets_soa.filepath_array = filepath_array;
    assets_soa.content_array = file_content_array;
    assets_soa.count = filepath_array_length;

    printf("\n");
    
    int code;
    while (1) {
        ArkRequest new_request = {0};
        code = ark_retrive_request(&new_request);
        if (code == ARK_SHUT_SERVER_SIGNAL) { break; }

        ARKHttpMessage http_msg = {0};
        ark_parse_http_request_message(&http_msg, new_request.raw, new_request.length);
        
        char response[] = "HTTP/1.1 200 OK\r\n"
                          "Content-Type: text/html\r\n"
                          "Content-Length: 49\r\n"
                          "Connection: close\r\n"
                          "\r\n"
                          "<html><body><h1>Hello, World!</h1></body></html>\n";

        /*
        ArkFetchTaskDetails tasks[2] = {0};

        char *domain_name = "localhost";
        char *request = "GET / HTTP/1.1\r\nHost: www.example.com\r\nConnection: close\r\n\r\n";
        
        tasks[0].domain_name = domain_name;
        tasks[0].domain_name_length = strlen(domain_name);
        tasks[0].request = request;
        tasks[0].request_length = strlen(request);
        tasks[0].port = 3000;
        
        tasks[1].domain_name = domain_name;
        tasks[1].domain_name_length = strlen(domain_name);
        tasks[1].request = request;
        tasks[1].request_length = strlen(request);
        tasks[1].port = 3000;

        ArkFetchResponse r;
        ark_fetch(&r, new_request.handle, tasks, 2);
        */

        ark_send_response(new_request.handle, response, strlen(response));
    }
    
    return 0;
}