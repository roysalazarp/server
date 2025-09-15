#ifndef GLOBALS_H
#define GLOBALS_H

#include <stdint.h>
#include <sqlite3.h>

#define HASH_LENGTH 32
#define SALT_LENGTH 16
#define PASSWORD_BUFFER 255

extern char **component_name_arr;
extern char **component_content_arr;
extern uint32_t component_arr_length;
extern sqlite3 *db;

extern char *error_messages[];

typedef enum {
    ERR_MIN_PASSWORD_LENGTH,
    ERR_INVALID_EMAIL,
} UserErrorCodes;

#endif // GLOBALS_H
