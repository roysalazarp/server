#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>

void bzero(void *p, size_t n);
int is_digit(uint8_t c);
unsigned long available_ram();
int ark_count_files(uint32_t *num_of_filepaths, size_t *longest_filepath, char *root_path);
int ark_list_files(char *flat_array_of_filepaths, size_t *array_of_lengths, uint32_t num_of_filepaths, size_t longest_filepath, char *base_path);
size_t ark_get_file_size(char *filepath);

#endif /* UTILS_H */
