#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>

void bzero(void *p, size_t n);
int is_digit(int c);
unsigned long available_ram();
int ark_file_count(int *count, size_t *max_filepath_length, char *root_path);
int ark_list_files(char *filepath_array, size_t *filepath_length_array, int filepath_array_length, size_t max_filepath_item_length, char *base_path);
size_t ark_file_size(const char *filepath);

#endif /* UTILS_H */