#include <ctype.h>
#include <regex.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#include "../../include/ark/shared.h"
#include "../../include/ark/memory.h"
#include "../../include/ark/utils.h"

#define HTML_EXTENSION  0x2E68746D6C000000 /* ".html" */

#define XDEF_OPEN       0x3C782D6465660000 /* "<x-def"   */ 
#define XDEF_CLOSE      0x3C2F782D6465663E /* "</x-def>" */ 

#define XDEF            0x782D646566000000 /* "x-def"  */ 
#define XUSE            0x782D757365000000 /* "x-use"  */
#define XSLOT           0x782D736C6F740000 /* "x-slot" */
#define XFILL           0x782D66696C6C0000 /* "x-fill" */
#define XVAL            0x782D76616C000000 /* "x-val"  */
#define XFOR            0x782D666F72000000 /* "x-for"  */

typedef struct {
    size_t start;
    size_t end;
} Range;

typedef struct {
    String file_path;
    String file_content;
    size_t cursor;
    size_t bol; /* beginning of line */
    size_t line_number;
} Lexer;

Lexer lexer_init(char *file_path, size_t file_path_length, char *content, size_t content_length) {
    Lexer lexer = {0};
    lexer.file_path.data = file_path;
    lexer.file_path.length = file_path_length;
    lexer.file_content.data = content;
    lexer.file_content.length = content_length;
    lexer.cursor = 0;
    lexer.bol = 0;
    lexer.line_number = 1;
    return lexer;
}

boolean is_html_path(char *path, size_t path_length) {
    uint64_t ext = *(uint64_t *)(path + path_length - 5 /* .html */);

    if ((__builtin_bswap64(ext) & MASK_5) == HTML_EXTENSION) {
        return true;
    }

    return false;
}

size_t find_eol(size_t pos, char *text, size_t text_length) {    
    for (; pos < text_length; ++pos) {
        if (text[pos] == '\n') {
            return pos;
        }
    }

    return 0;
}

size_t find_bol(size_t pos, char *text) {    
    for (; pos > 0; --pos) {
        if (text[pos] == '\n') {
            return pos + 1;
        }
    }

    return 0;
}

uint8_t strlen_u64(uint64_t x) {
    unsigned char *bytes = (unsigned char *)&x;
    uint8_t count = 0;

    uint8_t i;
    for (i = 0; i < sizeof(uint64_t); i++) {
        if (bytes[i] != 0) {
            count++;
        }
    }

    return count;
}

void print_tag_error(String file_path, String file_content, size_t line_number, size_t bol, Range identifier, char error_msg[] ) {
    printf("%s:\n", error_msg);
    printf("    file: %.*s\n", (int)file_path.length, file_path.data);
    printf("    line: %lu\n", line_number);
    printf("\n");

    /* print previous line */
    if (line_number > 1) {
        size_t prev_bol = find_bol(bol - 2, file_content.data);
        size_t prev_line_length = (bol - 1) - prev_bol;
        printf("    %lu|  %.*s\n", line_number - 1, (int)prev_line_length, &file_content.data[prev_bol]);
    }
    
    /* current line */
    size_t eol = find_eol(bol, file_content.data, file_content.length);
    size_t current_line_length = eol - bol;
    printf("    %lu|  %.*s\033[31m%.*s\033[0m%.*s\n", line_number, (int)(identifier.start - bol), &file_content.data[bol], (int)(identifier.end - identifier.start), &file_content.data[identifier.start], (int)(eol - identifier.end), &file_content.data[identifier.end]);

    if (eol < file_content.length) {
        size_t next_bol = eol + 1;
        size_t next_eol = find_eol(next_bol, file_content.data, file_content.length);
        size_t next_line_length = next_eol - next_bol;
        printf("    %lu|  %.*s\n", line_number + 1, (int)next_line_length, &file_content.data[next_bol]);
    }

    printf("\n");
}

int ark_validate_components(char **array_of_filepaths, char **file_content_array, uint32_t array_length) {
    uint64_t i;

    Memory *memory = reserve_memory(PAGE_SIZE * 100);

    uint32_t *html_file_mask = NULL;

    {
        size_t mask_size = array_length / U32_BITS;
        mask_size = mask_size < 1 ? 1 : mask_size; /* ensure a minimum */

        html_file_mask = memory_alloc(memory, mask_size * sizeof(uint32_t));

        for (i = 0; i < array_length; i++) {
            if (is_html_path(array_of_filepaths[i], strlen(array_of_filepaths[i]))) {
                TURN_ON_U32_BIT(html_file_mask, i);
            }
        }
    }

    uint32_t components_count = 0;

    {
        for (i = 0; i < array_length; i++) {
            if (IS_U32_BIT_OFF(html_file_mask, i)) {
                continue;
            }
    
            char *file_path = array_of_filepaths[i];
            char *file_content = file_content_array[i];

            {   /* 
                A file may contain multiple components. Check that component 
                declarations are not nested and that they are properly closed. 
                For example:

                Valid
                <x-def name="Button">
                    <!-- component definition -->
                </x-def>

                Invalid
                <x-def name="Button">
                    <x-def name="Nested"> <!-- Invalid: nested declaration -->
                    </x-def>
                </x-def>
                */

                Lexer lexer = lexer_init(file_path, strlen(file_path), file_content, strlen(file_content));
                
                boolean inside_xdef = false;
                uint8_t xdef_open_length = strlen_u64(XDEF_OPEN);

                for (; lexer.cursor < lexer.file_content.length; lexer.cursor++) {
                    uint64_t x = *(uint64_t *)(lexer.file_content.data + lexer.cursor);

                    if (((__builtin_bswap64(x) & MASK_6) == XDEF_OPEN) && isspace(lexer.file_content.data[lexer.cursor + xdef_open_length])) {
                        Range identifier = {0};
                        identifier.start = lexer.cursor;
                        identifier.end = identifier.start + (size_t)xdef_open_length;

                        if (inside_xdef) {
                            char error[] =  "x-def can't ever be a child tag, it must always be placed at the top level. "
                                            "If you think you already placed x-def at the top level, check that you did't "
                                            "leave open a x-def earlier in the file";

                            print_tag_error(lexer.file_path, lexer.file_content, lexer.line_number, lexer.bol, identifier, error);
                            
                            assert(0);
                            return -1;
                        }

                        inside_xdef = true;
                        components_count += 1;
                    } else if ((__builtin_bswap64(x) & MASK_8) == XDEF_CLOSE) {
                        inside_xdef = false;
                    } else if (lexer.file_content.data[lexer.cursor] == '\n') {
                        lexer.bol = lexer.cursor + 1;
                        lexer.line_number += 1;
                    }
                }
            }
        }
    }

    {
        for (i = 0; i < array_length; i++) {
            if (IS_U32_BIT_OFF(html_file_mask, i)) {
                continue;
            }
    
            char *file_path = array_of_filepaths[i];
            char *file_content = file_content_array[i];

            {
                /* TODO, LEFT OFF HERE! */
            }
        }
    }

    return 0;
}