#include <stdio.h>
#include <string.h>
#include "ark/http.h"

#define NULL ((void *)0)

int main(void) {
    char *buff = NULL;
    const char* raw_http_msg = "GET / HTTP/1.1\r\nHost: 127.0.0.1:8080\r\n\r\n";
    HttpMessage* msg;

    msg = parse_http_message(buff, raw_http_msg, strlen(raw_http_msg));

    return 0;
}
