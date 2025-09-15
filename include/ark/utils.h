#ifndef ARK_UTILS_H
#define ARK_UTILS_H

#include <stdio.h>
#include <stdint.h>

void bzero(void *p, size_t n);
unsigned long available_ram();
int ark_count_files(uint32_t *num_of_filepaths, char *root_path);
int ark_list_files(char **array_of_filepaths, uint32_t num_of_filepaths, char *base_path);
size_t ark_get_file_size(char *filepath);
int string_to_int(int *result, char *str, uint32_t length);

#endif /* ARK_UTILS_H */
