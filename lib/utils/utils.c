#include <string.h>
#include <stdio.h>
#include <sys/sysinfo.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>

#include "../../include/ark/shared.h"
#include "../../include/ark/utils.h"

#define PROJECT_ROOT "/workspaces/server/"

void bzero(void *p, size_t n) { memset(p, 0, n); }

static void print_source_line(const char *file, int line_offset) {
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s%s", PROJECT_ROOT, file);

    FILE *fp = fopen(full_path, "r");
    if (!fp) return;

    char buf[512];
    int current = 1;
    int target_line = line_offset;

    while (fgets(buf, sizeof(buf), fp)) {
        if (current == target_line) {
            // Remove trailing newline
            size_t len = strlen(buf);
            if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';

            // Skip leading whitespace
            char *trimmed = buf;
            while (*trimmed && isspace((unsigned char)*trimmed)) {
                trimmed++;
            }

            fprintf(stderr, "Line details:  " ANSI_GRAY "%s" ANSI_RESET "\n", trimmed);

            break;
        }
        current++;
    }
    fclose(fp);
}


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

panic ark_count_files(uint32_t *num_of_filepaths, char *root_path) {
    int op_res;
    DIR *dir = opendir(root_path);
    ARK_ASSERT(dir != NULL, 1, "opendir func failed for file %.*s", (int)strlen(root_path), root_path);

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
            ark_count_files(num_of_filepaths, path_buffer);
        } else {
            (*num_of_filepaths)++;
        }
    }

    closedir(dir);
}

int list_files_recursive(char **array_of_filepaths, uint32_t num_of_filepaths, char *base_path, uint32_t *internal_count);
int list_files_recursive(char **array_of_filepaths, uint32_t num_of_filepaths, char *base_path, uint32_t *internal_count) {
    int _;
    DIR *dir = opendir(base_path);
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

        sprintf(path_buffer, "%s/%s", base_path, entry->d_name);
        boolean is_dir = (boolean)(stat(path_buffer, &statbuf) == 0 && S_ISDIR(statbuf.st_mode));

        if (is_dir) {
            _ = list_files_recursive(array_of_filepaths, num_of_filepaths, path_buffer, internal_count);
            assert(_ != -1);
        } else {
            memcpy(array_of_filepaths[*internal_count], path_buffer, strlen(path_buffer));
            (*internal_count)++;
        }
    }

    closedir(dir);
    return 0;
}

int ark_list_files(char **array_of_filepaths, uint32_t num_of_filepaths, char *base_path) {
    uint32_t count = 0;
    list_files_recursive(array_of_filepaths, num_of_filepaths, base_path, &count);
    assert(count == num_of_filepaths);
    return count;
}

int list_file_sizes_recursive(size_t *array_of_lengths, uint32_t num_of_filepaths, char *base_path, uint32_t *internal_count) {
    int _;
    DIR *dir = opendir(base_path);
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

        sprintf(path_buffer, "%s/%s", base_path, entry->d_name);
        boolean is_dir = (boolean)(stat(path_buffer, &statbuf) == 0 && S_ISDIR(statbuf.st_mode));

        if (is_dir) {
            _ = list_file_sizes_recursive(array_of_lengths, num_of_filepaths, path_buffer, internal_count);
            assert(_ != -1);
        } else {
            array_of_lengths[*internal_count] = strlen(path_buffer) + 1;
            (*internal_count)++;
        }
    }

    closedir(dir);
    return 0;
}

int ark_list_file_sizes(size_t *array_of_lengths, uint32_t num_of_filepaths, char *base_path) {
    uint32_t count = 0;
    list_file_sizes_recursive(array_of_lengths, num_of_filepaths, base_path, &count);
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

int string_to_int(int *result, char *str, uint32_t length) {
    int sign = 1;
    int i = 0;

    // Handle optional sign (only if we have at least 1 character)
    if (length > 0 && str[0] == '-') {
        sign = -1;
        i++;
    } else if (length > 0 && str[0] == '+') {
        i++;
    }

    // Iterate through characters up to length
    for (; i < length; i++) {
        char c = str[i];
        if (c >= '0' && c <= '9') {
            *result = *result * 10 + (c - '0');
        } else {
            // Stop if we find a non-digit
            break;
        }
    }


    *result *= sign;

    return 0;
}