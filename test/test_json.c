#include <stdio.h>
#include <string.h>
#include "ark/json.h"

#define NULL ((void *)0)

int main(void) {
    char *buff = NULL;
    const char* input = "{\"name\": \"Ark\"}";
    GJSON* json;

    json = gjson_parse(buff, input, strlen(input));

    return 0;
}
