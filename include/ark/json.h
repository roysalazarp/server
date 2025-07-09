#ifndef ARK_JSON_H
#define ARK_JSON_H

#include <stdio.h>

/* Opaque type representing a JSON value */
typedef struct GJSON GJSON;

/* Parse JSON from string */
GJSON* gjson_parse(void *buff, const char* input, size_t input_lenth);

#endif /* ARK_JSON_H */
