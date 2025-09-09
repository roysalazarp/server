#ifndef ARK_UTILS_H
#define ARK_UTILS_H

#include <stdio.h>

void bzero(void *p, size_t n);
int is_digit(uint8_t c);
unsigned long available_ram();
int ark_count_files(uint32_t *num_of_filepaths, char *root_path);
int ark_list_files(char **array_of_filepaths, uint32_t num_of_filepaths, char *base_path);
size_t ark_get_file_size(char *filepath);

#endif /* ARK_UTILS_H */
