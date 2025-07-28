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

#define HTML_ATTR_START         0
#define HTML_ATTR_START_KEY     1
#define HTML_ATTR_KEY           2
#define HTML_ATTR_EQUALS        3
#define HTML_ATTR_START_VALUE   4
#define HTML_ATTR_VALUE         5
#define HTML_ATTR_END           6

const char html_attr_key_token[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x00 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x10 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,  /* 0x20 */ /*│             -  │*/
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,  /* 0x30 */ /*│0123456789      │*/
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x40 */ /*│                │*/
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x50 */ /*│                │*/
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  /* 0x60 */ /*│ abcdefghijklmno│*/
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,  /* 0x70 */ /*│pqrstuvwxyz     │*/
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x80 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x90 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0xa0 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0xb0 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0xc0 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0xd0 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0xe0 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0xf0 */
};

#define XDEF            0x782D646566000000 /* "x-def"  */ 
#define XUSE            0x782D757365000000 /* "x-use"  */
#define XSLOT           0x782D736C6F740000 /* "x-slot" */
#define XFILL           0x782D66696C6C0000 /* "x-fill" */
#define XVAL            0x782D76616C000000 /* "x-val"  */
#define XFOR            0x782D666F72000000 /* "x-for"  */

typedef struct {
    uint32_t start;
    uint32_t end;
} Range;

typedef struct {
    uint8_t xdef;
    uint8_t xuse;
    uint8_t xfill;
    uint8_t xfor;
} OpenClose;

typedef struct Template Template;
struct Template {
    Template *next;
    char *name;
    Range markup;
    uint32_t file_idx;
};

typedef enum {
    TOKEN_INVALID,
    TOKEN_XDEF,
    TOKEN_XUSE,
    TOKEN_XSLOT,
    TOKEN_XFILL,
    TOKEN_XVAL,
    TOKEN_XFOR,
    TOKEN_PLACEHOLDER,
    TOKEN_EOF
} TokenKind;
typedef struct {
    enum { TAG_INVALID, TAG_OPENING, TAG_CLOSING, TAG_SELFCLOSING } type;
    Range markup;
} Tag;

typedef struct {
    TokenKind kind;
    Tag tag; /* when is TokenKind is TOKEN_PLACEHOLDER this can be just left empty */
    uint64_t identifier; /* This type won't work for TOKEN_PLACEHOLDER */
} Token;

typedef struct {
    String file_path;
    String file_content;
    uint32_t cursor;
    uint32_t bol; /* beginning of line */
    uint32_t line_number;
} Lexer;

uint32_t find_eol(uint32_t pos, char *text, uint32_t text_length) {    
    for (; pos < text_length; ++pos) {
        if (text[pos] == '\n') {
            return pos;
        }
    }

    return 0;
}

uint32_t find_bol(uint32_t pos, char *text) {    
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

void print_tag_error(String file_path, String file_content, uint32_t line_number, uint32_t bol, Range identifier, char error_msg[] ) {
    printf("%s:\n", error_msg);
    printf("    file: %.*s\n", file_path.length, file_path.data);
    printf("    line: %d\n", line_number);
    printf("\n");

    /* print previous line */
    if (line_number > 1) {
        uint32_t prev_bol = find_bol(bol - 2, file_content.data);
        uint32_t prev_line_length = (bol - 1) - prev_bol;
        printf("    %d|  %.*s\n", line_number - 1, prev_line_length, &file_content.data[prev_bol]);
    }
    
    /* current line */
    uint32_t eol = find_eol(bol, file_content.data, file_content.length);
    uint32_t current_line_length = eol - bol;
    printf("    %d|  %.*s\033[31m%.*s\033[0m%.*s\n", line_number, (identifier.start - bol), &file_content.data[bol], (identifier.end - identifier.start), &file_content.data[identifier.start], (eol - identifier.end), &file_content.data[identifier.end]);

    /* next line */
    if (eol < file_content.length) {
        uint32_t next_bol = eol + 1;
        uint32_t next_eol = find_eol(next_bol, file_content.data, file_content.length);
        uint32_t next_line_length = next_eol - next_bol;
        printf("    %d|  %.*s\n", line_number + 1, next_line_length, &file_content.data[next_bol]);
    }

    printf("\n");
}

void print_list(Template* head) {
    Template* current = head;
    while (current != NULL) {
        printf("%s -> ", current->name);
        current = current->next;
    }
    printf("\n");
}

/*
- DONE: Error if x-slot, x-val are not self-closing.
- DONE: Error if x-def, x-fill, x-for do not have a pair (open + closing) xtags.

- DONE: Error if x-def has a parent.
- DONE: Error if x-fill has other parent than x-use.
- DONE: Error if x-use has other child than x-fill.

- DONE: Error if any opening or self-closing xtag does not have a name attribute.
- DONE: Error if any xtag except x-use opening or self-closing has more than the name attribute.

- Error if recursive import.
*/
int ark_validate_templates(char **array_of_filepaths, char **file_content_array, uint32_t array_length) {
    uint32_t i;

    Memory *memory = reserve_memory(PAGE_SIZE);

    uint32_t *html_file_mask = NULL;

    uint32_t mask_size = array_length / U32_BITS;
    mask_size = mask_size < 1 ? 1 : mask_size; /* ensure a minimum */

    html_file_mask = memory_alloc(memory, mask_size * sizeof(uint32_t));

    for (i = 0; i < array_length; i++) {
        char *file_path = array_of_filepaths[i];
        
        uint64_t ext = *(uint64_t *)(file_path + strlen(file_path) - 5 /* .html */);
        
        if ((__builtin_bswap64(ext) & MASK_5) == HTML_EXTENSION) {
            TURN_ON_U32_BIT(html_file_mask, i);
        }
    }

    uint64_t parent_identifier = 0;

    Template *template_head = NULL;
    Template *prev_template = NULL;
    Template *template = NULL;

    for (i = 0; i < array_length; i++) {
        if (IS_U32_BIT_OFF(html_file_mask, i)) {
            continue;
        }

        char *file_path = array_of_filepaths[i];
        char *file_content = file_content_array[i];

        Lexer lexer = {0};

        lexer.file_path.data = file_path;
        lexer.file_path.length = strlen(file_path);
        lexer.file_content.data = file_content;
        lexer.file_content.length = strlen(file_content);
        lexer.cursor = 0;
        lexer.bol = 0;
        lexer.line_number = 1;

        OpenClose oc = {0};
        boolean inside_xuse = false;

        while (lexer.cursor < lexer.file_content.length) {
            Token token = {0};
            String name = {0};

            uint8_t ch = 0; /* character */
            uint8_t prev_ch = 0;

            while (true) {
                if (++lexer.cursor == lexer.file_content.length) {
                    token.kind = TOKEN_EOF;
                    goto done_token;
                }

                ch = lexer.file_content.data[lexer.cursor];
                if ((prev_ch == 'x' && ch == '-') || (prev_ch == '{' && ch == '{')) {
                    if (prev_ch == '{' && ch == '{') {
                        token.tag.markup.start = lexer.cursor - 1;
                        token.kind = TOKEN_PLACEHOLDER;

                        uint8_t _ch = ch;
                        uint8_t _prev_ch = prev_ch;
                        while (true) {
                            if (++lexer.cursor == lexer.file_content.length) {
                                token.kind = TOKEN_EOF;
                                goto done_token;
                            }
                            
                            _ch = lexer.file_content.data[lexer.cursor];

                            if (_prev_ch == '}' && _ch == '}') {
                                token.tag.markup.end = lexer.cursor;
                                lexer.cursor++; /* skip second '}' */
                                break;
                            }
                            _prev_ch = _ch;
                        }
                    }

                    uint8_t sec_prev_ch = lexer.file_content.data[lexer.cursor - 2];
                    uint8_t thr_prev_ch = lexer.cursor > 2 ? lexer.file_content.data[lexer.cursor - 3] : 0;

                    if ((thr_prev_ch == '<') && (sec_prev_ch == '/')) { /* if true we encounter </x- which indicates TAG_CLOSING */
                        token.tag.markup.start = lexer.cursor - 3;
                        
                        lexer.cursor--;
                        uint64_t kind = *(uint64_t *)(lexer.file_content.data + lexer.cursor);

                        if ((__builtin_bswap64(kind) & MASK_5) == XDEF) {
                            token.kind = TOKEN_XDEF;
                            token.identifier = XDEF;
                        } 
                        else if ((__builtin_bswap64(kind) & MASK_5) == XUSE) {
                            token.kind = TOKEN_XUSE;
                            token.identifier = XUSE;
                        }
                        else if ((__builtin_bswap64(kind) & MASK_6) == XSLOT) {
                            token.kind = TOKEN_XSLOT;
                            token.identifier = XSLOT;
                        }
                        else if ((__builtin_bswap64(kind) & MASK_6) == XFILL) {
                            token.kind = TOKEN_XFILL;
                            token.identifier = XFILL;
                        }
                        else if ((__builtin_bswap64(kind) & MASK_5) == XVAL) {
                            token.kind = TOKEN_XVAL;
                            token.identifier = XVAL;
                        }
                        else if ((__builtin_bswap64(kind) & MASK_5) == XFOR) {
                            token.kind = TOKEN_XFOR;
                            token.identifier = XFOR;
                        }

                        uint8_t identifier_length = strlen_u64(token.identifier);
                        lexer.cursor += identifier_length;

                        uint8_t _ch = lexer.file_content.data[lexer.cursor];
                        while (true) {
                            if (isspace(_ch)) {
                                if (++lexer.cursor == lexer.file_content.length) {
                                    token.kind = TOKEN_EOF;
                                    goto done_token;
                                }

                                _ch = lexer.file_content.data[lexer.cursor];
                            } else {
                                break;
                            }
                        }

                        assert(_ch == '>');

                        token.tag.type = TAG_CLOSING;
                        lexer.cursor++; /* skip '>' */
                        token.tag.markup.end = lexer.cursor;
                    } else if (sec_prev_ch == '<') { /* if true we encounter <x- which indicates TAG_OPENING or TAG_SELFCLOSING */
                        token.tag.markup.start = lexer.cursor - 2;

                        lexer.cursor--;
                        uint64_t kind = *(uint64_t *)(lexer.file_content.data + lexer.cursor);

                        if ((__builtin_bswap64(kind) & MASK_5) == XDEF) {
                            token.kind = TOKEN_XDEF;
                            token.identifier = XDEF;
                        } 
                        else if ((__builtin_bswap64(kind) & MASK_5) == XUSE) {
                            token.kind = TOKEN_XUSE;
                            token.identifier = XUSE;
                        }
                        else if ((__builtin_bswap64(kind) & MASK_6) == XSLOT) {
                            token.kind = TOKEN_XSLOT;
                            token.identifier = XSLOT;
                        }
                        else if ((__builtin_bswap64(kind) & MASK_6) == XFILL) {
                            token.kind = TOKEN_XFILL;
                            token.identifier = XFILL;
                        }
                        else if ((__builtin_bswap64(kind) & MASK_5) == XVAL) {
                            token.kind = TOKEN_XVAL;
                            token.identifier = XVAL;
                        }
                        else if ((__builtin_bswap64(kind) & MASK_5) == XFOR) {
                            token.kind = TOKEN_XFOR;
                            token.identifier = XFOR;
                        }

                        uint8_t identifier_length = strlen_u64(token.identifier);
                        lexer.cursor += identifier_length;

                        uint8_t _ch = lexer.file_content.data[lexer.cursor];
                        uint8_t _prev_ch = 0;
                        
                        uint8_t attr_tk = 0;
                        uint32_t str_start = 0;
                        uint8_t quotation = 0;

                        while (true) {
                            String attr_name = {0};
                            String attr_value = {0};
            
                            while (true) {
                                _ch = lexer.file_content.data[lexer.cursor] & 255;
                                switch (attr_tk) {
                                    case HTML_ATTR_START: {
                                        while (true) {
                                            uint8_t _next_ch = lexer.file_content.data[lexer.cursor + 1];
                                            if (_ch == '>') {
                                                attr_tk = HTML_ATTR_END;
                                                break;
                                            } else if (_ch == '/' && _next_ch == '>') {
                                                lexer.cursor++; /* skip '/' */
                                                attr_tk = HTML_ATTR_END;
                                                break;
                                            } else if (html_attr_key_token[_ch]) {
                                                attr_tk = HTML_ATTR_START_KEY;
                                                break;
                                            } else if (isspace(_ch)) {
                                                if (++lexer.cursor == lexer.file_content.length) {
                                                    token.kind = TOKEN_EOF;
                                                    goto done_token;
                                                }
                                                
                                                _ch = lexer.file_content.data[lexer.cursor] & 255;
                                            } else {
                                                assert(0);
                                                return -1;
                                            }
                                        }
                                        break;
                                    }
                                    case HTML_ATTR_START_KEY: {
                                        while (true) {
                                            if (html_attr_key_token[_ch]) {
                                                attr_name.data = &lexer.file_content.data[lexer.cursor];
                                                attr_tk = HTML_ATTR_KEY;
                                                str_start = lexer.cursor;
                                                break;
                                            } else if (isspace(_ch)) {
                                                if (++lexer.cursor == lexer.file_content.length) {
                                                    token.kind = TOKEN_EOF;
                                                    goto done_token;
                                                }

                                                _ch = lexer.file_content.data[lexer.cursor] & 255;
                                            } else {
                                                assert(0);
                                                return -1;
                                            }
                                        }
                                        break;
                                    }
                                    case HTML_ATTR_KEY: {
                                        while (true) {
                                            if (isspace(_ch) || _ch == '=') {
                                                attr_name.length = lexer.cursor - str_start;
                                                attr_tk = HTML_ATTR_EQUALS;
                                                break;
                                            } else if (html_attr_key_token[_ch]) {
                                                if (++lexer.cursor == lexer.file_content.length) {
                                                    token.kind = TOKEN_EOF;
                                                    goto done_token;
                                                }
                                                
                                                _ch = lexer.file_content.data[lexer.cursor] & 255;
                                            }   else {
                                                assert(0);
                                                return -1;
                                            }
                                        }
                                        break;
                                    }
                                    case HTML_ATTR_EQUALS: {
                                        while (true) {
                                            if (_ch == '=') {
                                                lexer.cursor++; /* skip '=' */
                                                attr_tk = HTML_ATTR_START_VALUE;
                                                break;
                                            } else if (isspace(_ch)) {
                                                if (++lexer.cursor == lexer.file_content.length) {
                                                    token.kind = TOKEN_EOF;
                                                    goto done_token;
                                                }

                                                _ch = lexer.file_content.data[lexer.cursor] & 255;
                                            } else {
                                                assert(0);
                                                return -1;
                                            }
                                        }
                                        break;
                                    }
                                    case HTML_ATTR_START_VALUE: {
                                        while (true) {
                                            if (_ch == '"' ||  _ch == '\'') {
                                                quotation = _ch;
                                                lexer.cursor++; /* skip quotation */
                                                attr_value.data = &lexer.file_content.data[lexer.cursor];
                                                attr_tk = HTML_ATTR_VALUE;
                                                str_start = lexer.cursor;
                                                break;
                                            } else if (isspace(_ch)) {
                                                if (++lexer.cursor == lexer.file_content.length) {
                                                    token.kind = TOKEN_EOF;
                                                    goto done_token;
                                                }

                                                _ch = lexer.file_content.data[lexer.cursor] & 255;
                                            }   else {
                                                assert(0);
                                                return -1;
                                            }
                                        }
                                        break;
                                    }
                                    case HTML_ATTR_VALUE: {
                                        char prev = lexer.file_content.data[lexer.cursor - 1] & 255;
                                        while (true) {
                                            if (_ch == quotation && prev != '\\') {
                                                attr_value.length = lexer.cursor - str_start;
                                                lexer.cursor++; /* skip quotation */
                                                attr_tk = HTML_ATTR_END;
                                                break;
                                            } else if (_ch & 255) {
                                                prev = _ch;

                                                if (++lexer.cursor == lexer.file_content.length) {
                                                    token.kind = TOKEN_EOF;
                                                    goto done_token;
                                                }

                                                _ch = lexer.file_content.data[lexer.cursor] & 255;
                                            }   else {
                                                assert(0);
                                                return -1;
                                            }
                                        }
                                        break;
                                    }
                                    case HTML_ATTR_END: {
                                        attr_tk = HTML_ATTR_START;
                                        goto exit_attr_parsing;
                                    }
                                }

                                _prev_ch = _ch;
                            }
                            
                            exit_attr_parsing:;
                            if (attr_name.data == NULL && attr_value.data == NULL) {
                                /** no more attrs */
                                if (!name.data) {
                                    /* print error! */
                                    assert(0);
                                }

                                break;
                            }

                            if (attr_name.length == 4 &&
                                attr_name.data[0] == 'n' && 
                                attr_name.data[1] == 'a' && 
                                attr_name.data[2] == 'm' && 
                                attr_name.data[3] == 'e') 
                            {
                                name = attr_value;
                            } else {
                                if (token.kind != TOKEN_XUSE) {
                                    assert(0);
                                    /* print error! */
                                }
                            }
                        }

                        assert(_ch == '>');
                        
                        if (_prev_ch == '/') {
                            token.tag.type = TAG_SELFCLOSING;
                            lexer.cursor++; /* skip '>' */
                            token.tag.markup.end = lexer.cursor;
                        } else {
                            token.tag.type = TAG_OPENING;
                            lexer.cursor++; /* skip '>' */
                            token.tag.markup.end = lexer.cursor;
                        }

                        _prev_ch = _ch;
                    }

                    if (token.kind != TOKEN_PLACEHOLDER) {
                        assert(token.tag.type);
                    }

                    if (token.tag.type && token.kind) {
                        break;
                    }
                }
                prev_ch = ch;
            }

            done_token:;

            switch (token.kind) {
                case TOKEN_EOF: {
                    break;
                }
                case TOKEN_INVALID: {
                    break;
                }
                case TOKEN_PLACEHOLDER: {
                    break;
                }
                case TOKEN_XDEF: {
                    if (token.tag.type == TAG_OPENING) {
                        if (oc.xdef) {
                            assert(0);
                        }
                        if (inside_xuse) {
                            assert(0);
                            /* Error: x-use can only have direct child x-fill */
                            inside_xuse = false;
                        }
                        oc.xdef += 1;
                        template = memory_alloc(memory, sizeof(Template));
                        template->file_idx = i;
                        template->markup.start = token.tag.markup.start;
                        template->name = memory_alloc(memory, name.length + 1);
                        memcpy(template->name, name.data, name.length);                        
                        if (!prev_template) {
                            template_head = template;
                            prev_template = template;
                        } else {
                            prev_template->next = template;
                            prev_template = template;
                        }
                    } else if (token.tag.type == TAG_CLOSING) {
                        oc.xdef -= 1;
                        template->markup.end = token.tag.markup.end;
                    }
                    break;
                }
                case TOKEN_XUSE: {
                    if (token.tag.type == TAG_OPENING) {
                        oc.xuse += 1;
                        if (inside_xuse) {
                            assert(0);
                            /* Error: x-use can only have direct child x-fill */
                        }
                        inside_xuse = true;
                    } else if (token.tag.type == TAG_CLOSING) {
                        oc.xuse -= 1;
                        inside_xuse = false;
                    }
                    break;
                }
                case TOKEN_XSLOT: {
                    if (token.tag.type != TAG_SELFCLOSING) {
                        /* print error! */
                        assert(0);
                    }
                    if (inside_xuse) {
                        assert(0);
                        /* Error: x-use can only have direct child x-fill */
                    }
                    break;
                }
                case TOKEN_XFILL: {
                    if (token.tag.type == TAG_OPENING) {
                        oc.xfill += 1;
                        if (!inside_xuse) {
                            assert(0);
                            /* Error: x-fill must be first child of x-use */
                        }
                        inside_xuse = false;
                    } else if (token.tag.type == TAG_CLOSING) {
                        oc.xfill -= 1;
                    }
                    break;
                }
                case TOKEN_XVAL: {
                    if (token.tag.type != TAG_SELFCLOSING) {
                        /* print error! */
                        assert(0);
                    }
                    if (inside_xuse) {
                        assert(0);
                        /* Error: x-use can only have direct child x-fill */
                    }
                    break;
                }
                case TOKEN_XFOR: {
                    if (token.tag.type == TAG_OPENING) {
                        oc.xfor += 1;
                        if (inside_xuse) {
                            assert(0);
                            /* Error: x-use can only have direct child x-fill */
                            inside_xuse = false;
                        }
                    } else if (token.tag.type == TAG_CLOSING) {
                        oc.xfor -= 1;
                    }
                    break;
                }
                default: {
                    break;
                }
            }
        }
    
        if (oc.xdef | oc.xuse | oc.xfill | oc.xfor) {
            if (oc.xdef) {
                assert(0);
                /* print error: forgot to close oc.xdef amount of xdef tag */
            }
            if (oc.xuse) {
                assert(0);
                /* print error: forgot to close oc.xuse amount of xuse tag */
            }
            if (oc.xfill) {
                assert(0);
                /* print error: forgot to close oc.xfill amount of xfill tag */
            }
            if (oc.xfor) {
                assert(0);
                /* print error: forgot to close oc.xfor amount of xfor tag */
            }
        }
    }

    print_list(template_head);

    return 0;
}

int ark_compile_templates(char **array_of_filepaths, char **file_content_array, uint32_t array_length) {
    return 0;
}