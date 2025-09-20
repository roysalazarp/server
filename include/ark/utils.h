#ifndef ARK_UTILS_H
#define ARK_UTILS_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>

#include "shared.h"

#define ANSI_GRAY   "\033[90m"
#define ANSI_RESET  "\033[0m"

/** Use:
 *  ARK_ASSERT(dir != NULL, 1, "opendir func failed for file %.*s", (int)strlen(root_path), root_path);
 */
#define ARK_ASSERT(expr, lines_above, fmt, ...)                                         \
    do {                                                                                \
        if (!(expr)) {                                                                  \
            fprintf(stderr, "\n======== ASSERTION FAILED ========\n");                  \
            fprintf(stderr, "Expression:    " ANSI_GRAY "%s" ANSI_RESET "\n", #expr);   \
            fprintf(stderr, "File:          %s\n", __FILE__);                           \
            fprintf(stderr, "Line:          %d\n", __LINE__);                           \
            print_source_line(__FILE__, __LINE__ - (lines_above));                      \
            fprintf(stderr, "Details:       " fmt "\n", ##__VA_ARGS__);                 \
            if (errno != 0) {                                                           \
                fprintf(stderr, "System error:  %s\n", strerror(errno));                \
            }                                                                           \
            fprintf(stderr, "==================================\n\n");                  \
            abort();                                                                    \
        }                                                                               \
    } while (0)

#define ERR_SIZE 255

#define PROPAGATE_ERR(expr, ...)                                                    \
    do {                                                                            \
        if (expr == -1) {                                                           \
            if (sizeof(#__VA_ARGS__) > 1 && strcmp(#__VA_ARGS__, "\"\"") != 0) {    \
                snprintf(err, ERR_SIZE, __VA_ARGS__);                               \
            }                                                                       \
            return -1;                                                              \
        }                                                                           \
    } while (0)

#define CHECK_ERR(expr, ...)                                                        \
    do {                                                                            \
        if (!(expr)) {                                                              \
            if (sizeof(#__VA_ARGS__) > 1 && strcmp(#__VA_ARGS__, "\"\"") != 0) {    \
                snprintf(err, ERR_SIZE, __VA_ARGS__);                               \
            }                                                                       \
            return -1;                                                              \
        }                                                                           \
    } while (0)

/*
Maybe I don't need this macro.

#define PANIC_ON_ERR(err_code, expr, ...)                                           \
    do {                                                                            \
        if (err_code == expr) {                                                     \
            if (sizeof(#__VA_ARGS__) > 1 && strcmp(#__VA_ARGS__, "\"\"") != 0) {    \
                snprintf(err, ERR_SIZE, __VA_ARGS__);                               \
            }                                                                       \
            fprintf(stderr, err);                                                   \
            abort();                                                                \
        }                                                                           \
    } while (0)
*/

void bzero(void *p, size_t n);
void print_source_line(const char *file, int line_offset);
unsigned long available_ram();
panic ark_count_files(uint32_t *num_of_filepaths, char *root_path);
int ark_list_files(char **array_of_filepaths, uint32_t num_of_filepaths, char *base_path);
size_t ark_get_file_size(char *filepath);
int string_to_int(int *result, char *str, uint32_t length);
int ark_list_file_sizes(size_t *array_of_lengths, uint32_t num_of_filepaths, char *base_path);

#endif /* ARK_UTILS_H */
