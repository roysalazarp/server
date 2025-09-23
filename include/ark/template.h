#ifndef ARK_TEMPLATE_H
#define ARK_TEMPLATE_H

#include <stdio.h>
#include "shared.h"
#include "utils.h"

FAIL_SAFE void ark_make_html_file_mask(uint32_t *html_file_mask, char **array_of_filepaths, uint32_t array_length);
int ark_templates_count(uint32_t *count, uint32_t *html_file_mask, char **array_of_filepaths, char **file_content_array, uint32_t array_length);
int ark_validate_templates(uint32_t *html_file_mask, char **array_of_filepaths, char **file_content_array, uint32_t array_length);
int ark_get_compile_templates_lengths(uint32_t *compiled_templates_lengths, char **template_name_array, char **template_content_array, uint32_t template_count);
int ark_compile_templates(char **compiled_template_content_array, char **template_name_array, char **template_content_array, uint32_t template_count);
int ark_get_templates_info(uint32_t *names_lengths, uint32_t *templates_lengths, uint32_t template_count, uint32_t *html_file_mask, char **array_of_filepaths, char **file_content_array, uint32_t array_length);
int ark_make_templates_array(char **template_name_array, char **template_content_array, uint32_t template_count, uint32_t *html_file_mask, char **array_of_filepaths, char **file_content_array, uint32_t array_length);
// int ark_count_template_rendering_ctx_amount(uint32_t *template_rendering_ctx_count, char **template_content_array, uint32_t template_count);
// int ark_get_single_template_rendering_ctx_lengths_info(uint32_t *ctx_names_lengths, uint32_t ctx_arr_length, char *template_content, uint32_t template_content_length);
// int ark_get_single_template_rendering_ctx(char **ctx_names, uint64_t *ctx_arr, uint32_t ctx_arr_length, char *template_content, uint32_t template_content_length);
int calc_rendered_length(char *err, uint32_t *new_length, char *ctxname, char **placeholders, uint32_t placeholders_count, char ***values, uint32_t times, char *component, uint32_t component_length);
int render_ctx_values(char *err, char *ctxname, char **placeholders, uint32_t placeholders_count, char ***values, uint32_t times, char *component, uint32_t component_length);

#endif /* ARK_TEMPLATE_H */

