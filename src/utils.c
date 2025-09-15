#include <stdio.h>
#include <string.h>

#include "parsers/parsers.h"
#include "utils.h"
#include "globals.h"

int string_compare(char *str, char *str2, uint32_t str_length) {
    if (strncmp(str, str2, str_length) == 0 && strlen(str2) == str_length) {
        return 1;
    }
    return 0;
}

int find_element_index(uint32_t *index, char **arr, char *str, uint32_t arr_length) {
    uint32_t i;
    for (i = 0; i < arr_length; i++) {
        if (string_compare(str, arr[i], strlen(arr[i]))) {
            *index = i;
            return 0;
        }
    }
    return -1;
}

int check_password(char *errors[1], uint32_t *errors_count, char *password, uint32_t password_length) {
    if (password_length < 4) {
        errors[*errors_count] = error_messages[ERR_MIN_PASSWORD_LENGTH];
        *errors_count += 1;
    }

    return 0;
}

int check_email(char *errors[1], uint32_t *errors_count, char *email, uint32_t email_length) {
    Email e = {0};
    if (parse_email(&e, email, email_length) == -1) {
        errors[*errors_count] = error_messages[ERR_INVALID_EMAIL];
        *errors_count += 1;
    }

    return 0;
}

int generate_salt(void *salt, size_t salt_size) {
    FILE *dev_urandom = fopen("/dev/urandom", "rb");

    if (fread(salt, 1, salt_size, dev_urandom) != salt_size) {
        fclose(dev_urandom);
        return -1;
    }

    fclose(dev_urandom);
    return 0;
}
