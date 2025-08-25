#ifndef TEMPLATE_H
#define TEMPLATE_H

#include <stdio.h>
#include "shared.h"

typedef struct {
    RString *names;
    RString *components;
    uint32_t count;
} ArkComponentArray;

int ark_compile_templates(ArkComponentArray *component_array, char **array_of_filepaths, char **file_content_array, uint32_t array_length);

#endif /* TEMPLATE_H */

