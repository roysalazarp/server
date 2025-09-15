#ifndef UTILS_H
#define UTILS_H

#include <stdlib.h>
#include <stdint.h>

int find_element_index(uint32_t *index, char **arr, char *str, uint32_t arr_length);
int check_password(char *errors[1], uint32_t *errors_count, char *password, uint32_t password_length);
int check_email(char *errors[1], uint32_t *errors_count, char *email, uint32_t email_length);
int generate_salt(void *salt, size_t salt_size);

#endif /* UTILS_H */