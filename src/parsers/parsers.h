#ifndef PARSERS_H
#define PARSERS_H

#include <stdint.h>

typedef struct {
    struct {
        uint32_t start;
        uint32_t end;
    } username;
    struct {
        uint32_t start;
        uint32_t end;
    } mail_server;
    struct {
        uint32_t start;
        uint32_t end;
    } domain;
} Email;

int parse_email(Email *e, char *email, uint32_t email_length);

#endif /* PARSERS_H */