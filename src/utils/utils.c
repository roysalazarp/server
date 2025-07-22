#include <string.h>
#include <stdio.h>
#include <sys/sysinfo.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <stdlib.h>

#include "../../include/ark/shared.h"
#include "../../include/ark/utils.h"

void bzero(void *p, size_t n) { memset(p, 0, n); }
int is_digit(uint8_t c) { return '0' <= c && c <= '9'; }

unsigned long available_ram() {
    struct sysinfo info;
    
    if (sysinfo(&info) == 0) {
        printf("Total RAM: %lu bytes\n", info.totalram);
        printf("Free RAM:  %lu bytes\n", info.freeram);
        printf("Buffer RAM:  %lu bytes\n", info.bufferram);
        
        return info.freeram;
    }

    return 0;
}

int ark_count_files(uint32_t *num_of_filepaths, size_t *longest_filepath, char *root_path) {
    int _;
    DIR *dir = opendir(root_path);
    if (dir == NULL) {
        perror(NULL);
        return -1;
    }

    struct dirent *entry = {0};
    struct stat statbuf = {0};
    char path_buffer[PATH_MAX];
    memset(path_buffer, 0, PATH_MAX);

    while ((entry = readdir(dir)) != NULL) {
        boolean dot_dir = (boolean)(strcmp(entry->d_name, ".") == 0);
        boolean dot_dot_dir = (boolean)(strcmp(entry->d_name, "..") == 0);

        if (dot_dir || dot_dot_dir) {
            continue;
        }

        sprintf(path_buffer, "%s/%s", root_path, entry->d_name);
        boolean is_dir = (boolean)(stat(path_buffer, &statbuf) == 0 && S_ISDIR(statbuf.st_mode));

        if (is_dir) {
            _ = ark_count_files(num_of_filepaths, longest_filepath, path_buffer);
            assert(_ != -1);
        } else {
            (*num_of_filepaths)++;
            printf("%s\n", path_buffer);
            size_t len = strlen(path_buffer) + 1;
            if (len > *longest_filepath) {
                *longest_filepath = len;
            }
        }
    }

    closedir(dir);
    return 0;
}


void list_files_recursive(char *filepath_array, size_t *filepath_length_array, uint32_t filepath_array_length, size_t max_filepath_item_length, char *base_path, uint32_t *internal_count);
void list_files_recursive(char *filepath_array, size_t *filepath_length_array, uint32_t filepath_array_length, size_t max_filepath_item_length, char *base_path, uint32_t *internal_count) {
    DIR *dir = opendir(base_path);
    assert(dir != NULL);

    struct dirent *entry;
    struct stat statbuf;

    char *path_buffer = malloc(max_filepath_item_length);
    assert(path_buffer != NULL);
    bzero(path_buffer, max_filepath_item_length);

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        sprintf(path_buffer, "%s/%s", base_path, entry->d_name);

        if (stat(path_buffer, &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
            list_files_recursive(filepath_array, filepath_length_array, filepath_array_length, max_filepath_item_length, path_buffer, internal_count);
        } else {
            char *filepath_slot = filepath_array + (*internal_count * max_filepath_item_length);
            memcpy(filepath_slot, path_buffer, max_filepath_item_length);

            size_t *filepath_length_slot = filepath_length_array + (*internal_count * sizeof(size_t));
            *filepath_length_slot = strlen(path_buffer);

            (*internal_count)++;
        }
    }

    free(path_buffer);
    closedir(dir);
}

int ark_list_files(char *flat_array_of_filepaths, size_t *array_of_lengths, uint32_t num_of_filepaths, size_t longest_filepath, char *base_path) {
    uint32_t count = 0;
    list_files_recursive(flat_array_of_filepaths, array_of_lengths, num_of_filepaths, longest_filepath, base_path, &count);
    assert(count == num_of_filepaths);
    return count;
}

size_t ark_get_file_size(char *filepath) {
    long file_size = 0;

    FILE *file = fopen(filepath, "r");
    assert(file != NULL);
    
    assert(fseek(file, 0, SEEK_END) != -1);
    file_size = ftell(file);
    assert(file_size != -1);
    rewind(file);
    
    fclose(file);

    return (size_t)file_size;
}