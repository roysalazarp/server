#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "ark/shared.h"
#include "ark/server.h"

#define ARK_MAX_HTTP_REQUEST_SIZE MB(300)
#define ARK_MAX_QUEUED_HTTP_REQUESTS 500

int main() {
    ark_server_init((uint16_t)8080);

    while (1) {
        ArkRequest new_request = ark_retrive_request();

        char response[] = "HTTP/1.1 200 OK\r\n"
                          "Content-Type: text/html\r\n"
                          "Content-Length: 49\r\n"
                          "Connection: close\r\n"
                          "\r\n"
                          "<html><body><h1>Hello, World!</h1></body></html>\n";

        ArkFetchTaskDetails tasks[2] = {0};

        char *domain_name = "localhost";
        char *request = "GET /?foo=bar HTTP/1.1\r\nHost: www.example.com\r\nConnection: close\r\n\r\n";
        
        tasks[0].domain_name = domain_name;
        tasks[0].domain_name_length = strlen(domain_name);
        tasks[0].request = request;
        tasks[0].request_length = strlen(request);
        
        tasks[1].domain_name = domain_name;
        tasks[1].domain_name_length = strlen(domain_name);
        tasks[1].request = request;
        tasks[1].request_length = strlen(request);

        FetchResponse r = ark_fetch(new_request.handle, tasks, 2);

        ark_send_response(new_request.handle, response, strlen(response));
    }
    
    return 0;
}