#ifndef TEMPLATE_H
#define TEMPLATE_H

#include <stdio.h>
#include "shared.h"

typedef struct {
    String *names;
    String *components;
    uint32_t count;
} ArkComponentArray;

int ark_make_html_file_mask(uint32_t *html_file_mask, char **array_of_filepaths, uint32_t array_length);
int ark_components_count(uint32_t *count, uint32_t *html_file_mask, char **array_of_filepaths, char **file_content_array, uint32_t array_length);
int ark_validate_templates(uint32_t *html_file_mask, char **array_of_filepaths, char **file_content_array, uint32_t array_length);
int ark_compile_templates(ArkComponentArray *component_array, uint32_t *html_file_mask, char **array_of_filepaths, char **file_content_array, uint32_t array_length);

#endif /* TEMPLATE_H */

