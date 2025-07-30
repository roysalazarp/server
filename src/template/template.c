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

#define NAMEATTRKEY     0x6E616D65 /* "name"  */

#define XOPEN           0x3C782D00 /* "<x-"  */ 
#define XCLOSE          0x3C2F782D /* "</x-"  */ 
#define DOCB            0x7B7B0000 /* "{{"  */ /* double opening curly braces */
#define DCCB            0x7D7D0000 /* "}}"  */ /* double closing curly braces */

#define XDEF            0x782D646566000000 /* "x-def"  */ 
#define XUSE            0x782D757365000000 /* "x-use"  */
#define XSLOT           0x782D736C6F740000 /* "x-slot" */
#define XFILL           0x782D66696C6C0000 /* "x-fill" */
#define XVAL            0x782D76616C000000 /* "x-val"  */
#define XFOR            0x782D666F72000000 /* "x-for"  */

typedef struct {
    String name;
    String value;
} Attr;

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
    TOKEN_END
} TokenKind;

typedef struct {
    enum { TAG_INVALID, TAG_OPENING, TAG_CLOSING, TAG_SELFCLOSING } type;
    Range markup;
} Tag;
typedef struct {
    TokenKind kind;
    Tag tag; /* NOT used when TokenKind is TOKEN_PLACEHOLDER */
    uint64_t identifier; /* NOT used when TokenKind is TOKEN_PLACEHOLDER */
} Token;

typedef struct {
    TokenKind kind;
    uint64_t identifier;
} TokenType;

typedef struct {
    Token token;
    String name;
} NextToken;

typedef struct {
    String file_path;
    String file_content;
    uint32_t cursor;
    uint32_t bol; /* beginning of line */
    uint32_t line_number;
} Lexer;

typedef struct {
    String component;
    uint32_t cursor;
} CompileLexer;

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

/* returns 1 if reached EOF */
int parse_next_tag_attr(Attr *attr, char *html, uint32_t html_length, uint32_t *cursor) {    
    uint8_t attr_tk = 0;
    uint32_t str_start = 0;
    uint8_t quotation = 0;

    uint8_t _ch = html[*cursor];
    uint8_t _prev_ch = 0;

    while (true) {
        _ch = html[*cursor] & 255;
        switch (attr_tk) {
            case HTML_ATTR_START: {
                while (true) {
                    uint8_t _next_ch = html[*cursor + 1];
                    if (_ch == '>') {
                        attr_tk = HTML_ATTR_END;
                        break;
                    } else if (_ch == '/' && _next_ch == '>') {
                        (*cursor)++; /* skip '/' */
                        attr_tk = HTML_ATTR_END;
                        break;
                    } else if (html_attr_key_token[_ch]) {
                        attr_tk = HTML_ATTR_START_KEY;
                        break;
                    } else if (isspace(_ch)) {
                        if (++(*cursor) == html_length) {
                            return 1;
                        }
                        
                        _ch = html[*cursor] & 255;
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
                        attr->name.data = &html[*cursor];
                        attr_tk = HTML_ATTR_KEY;
                        str_start = *cursor;
                        break;
                    } else if (isspace(_ch)) {
                        if (++(*cursor) == html_length) {
                            return 1;
                        }

                        _ch = html[*cursor] & 255;
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
                        attr->name.length = *cursor - str_start;
                        attr_tk = HTML_ATTR_EQUALS;
                        break;
                    } else if (html_attr_key_token[_ch]) {
                        if (++(*cursor) == html_length) {
                            return 1;
                        }
                        
                        _ch = html[*cursor] & 255;
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
                        (*cursor)++; /* skip '=' */
                        attr_tk = HTML_ATTR_START_VALUE;
                        break;
                    } else if (isspace(_ch)) {
                        if (++(*cursor) == html_length) {
                            return 1;
                        }

                        _ch = html[*cursor] & 255;
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
                        (*cursor)++; /* skip quotation */
                        attr->value.data = &html[*cursor];
                        attr_tk = HTML_ATTR_VALUE;
                        str_start = *cursor;
                        break;
                    } else if (isspace(_ch)) {
                        if (++(*cursor) == html_length) {
                            return 1;
                        }

                        _ch = html[*cursor] & 255;
                    }   else {
                        assert(0);
                        return -1;
                    }
                }
                break;
            }
            case HTML_ATTR_VALUE: {
                char prev = html[*cursor - 1] & 255;
                while (true) {
                    if (_ch == quotation && prev != '\\') {
                        attr->value.length = *cursor - str_start;
                        (*cursor)++; /* skip quotation */
                        attr_tk = HTML_ATTR_END;
                        break;
                    } else if (_ch & 255) {
                        prev = _ch;

                        if (++(*cursor) == html_length) {
                            return 1;
                        }

                        _ch = html[*cursor] & 255;
                    }   else {
                        assert(0);
                        return -1;
                    }
                }
                break;
            }
            case HTML_ATTR_END: {
                return 0;
            }
        }

        _prev_ch = _ch;
    }

    __builtin_unreachable();
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

TokenType token_type(uint64_t x) {
    TokenType tt = {0};

    if ((__builtin_bswap64(x) & MASK64_5) == XDEF) {
        tt.kind = TOKEN_XDEF;
        tt.identifier = XDEF;
    } 
    else if ((__builtin_bswap64(x) & MASK64_5) == XUSE) {
        tt.kind = TOKEN_XUSE;
        tt.identifier = XUSE;
    }
    else if ((__builtin_bswap64(x) & MASK64_6) == XSLOT) {
        tt.kind = TOKEN_XSLOT;
        tt.identifier = XSLOT;
    }
    else if ((__builtin_bswap64(x) & MASK64_6) == XFILL) {
        tt.kind = TOKEN_XFILL;
        tt.identifier = XFILL;
    }
    else if ((__builtin_bswap64(x) & MASK64_5) == XVAL) {
        tt.kind = TOKEN_XVAL;
        tt.identifier = XVAL;
    }
    else if ((__builtin_bswap64(x) & MASK64_5) == XFOR) {
        tt.kind = TOKEN_XFOR;
        tt.identifier = XFOR;
    }

    return tt;
}

/* returns 1 if reached the end, 0 if success */
int skip_whitespace(char *text, uint32_t text_length, uint32_t *cursor) {
    uint8_t ch = text[*cursor];
    while (true) {
        if (isspace(ch)) {
            if (++(*cursor) == text_length) {
                return 1;
            }

            ch = text[*cursor];
        } else {
            return 0;
        }
    }

    __builtin_unreachable();
}

int get_token(NextToken *next_token, char *html, uint32_t html_length, uint32_t *cursor) {
    uint32_t tk = *(uint32_t *)(html + (*cursor));
    
    if ((__builtin_bswap32(tk) & MASK32_2) == DOCB) {
        next_token->token.tag.markup.start = (*cursor);
        next_token->token.kind = TOKEN_PLACEHOLDER;

        while (true) {
            if (++(*cursor) == html_length) {
                next_token->token.kind = TOKEN_END;
                return 1;
            }
            
            uint32_t dccb = *(uint32_t *)(html + (*cursor));
            if ((__builtin_bswap32(dccb) & MASK32_2) == DCCB) {
                (*cursor) += 2;
                next_token->token.tag.markup.end = (*cursor);
                break;
            }
        }

        return 0;
    }

    if ((__builtin_bswap32(tk) & MASK32_3) == XOPEN) {
        next_token->token.tag.markup.start = (*cursor);
        (*cursor) += 1;
        
        uint64_t kind = *(uint64_t *)(html + (*cursor));

        TokenType tt = token_type(kind);
        next_token->token.kind = tt.kind;
        next_token->token.identifier = tt.identifier;

        (*cursor) += strlen_u64(next_token->token.identifier);

        while (true) {                            
            Attr attr = {0};
            parse_next_tag_attr(&attr, html, html_length, cursor);

            if (attr.name.data == NULL && attr.value.data == NULL) {
                /** no more attrs */
                break;
            }

            uint32_t attr_key = *(uint32_t *)(attr.name.data);
            
            if ((__builtin_bswap32(attr_key) & MASK32_4) == NAMEATTRKEY && attr.name.length == 4) {
                next_token->name = attr.value;
            } else {
                if (next_token->token.kind != TOKEN_XUSE) {
                    assert(0);
                    /* print error! */
                }
            }
        }

        if (!next_token->name.data) {
            if (next_token->token.kind == TOKEN_XFILL || next_token->token.kind == TOKEN_XSLOT) {
                /* it's ok */
            } else {
                /* print error! */
                assert(0);
            }
        }

        uint8_t ch = html[(*cursor)];
        uint8_t prev_ch = html[(*cursor) - 1];
        
        assert(ch == '>');

        if (prev_ch == '/') {
            next_token->token.tag.type = TAG_SELFCLOSING;
            (*cursor)++; /* skip '>' */
            next_token->token.tag.markup.end = (*cursor);
        } else {
            next_token->token.tag.type = TAG_OPENING;
            (*cursor)++; /* skip '>' */
            next_token->token.tag.markup.end = (*cursor);
        }
    } else if ((__builtin_bswap32(tk) & MASK32_4) == XCLOSE) {
        next_token->token.tag.markup.start = (*cursor);
        (*cursor) += 2;

        uint64_t kind = *(uint64_t *)(html + (*cursor));

        TokenType tt = token_type(kind);
        next_token->token.kind = tt.kind;
        next_token->token.identifier = tt.identifier;

        (*cursor) += strlen_u64(next_token->token.identifier);

        int r = skip_whitespace(html, html_length, &(*cursor));
        if (r == 1) {
            next_token->token.kind = TOKEN_END;
            return 1;
        }

        assert(html[(*cursor)] == '>');

        next_token->token.tag.type = TAG_CLOSING;
        (*cursor)++; /* skip '>' */
        next_token->token.tag.markup.end = (*cursor);
    }

    return 0;
}

int recursive_compile_template(Template *tmpl_head, char **file_content_array, char *component, uint32_t component_length, char **ct, uint32_t *cursor) {    
    CompileLexer lexer = {0};
    lexer.component.data = component;
    lexer.component.length = component_length;
    lexer.cursor = 0;

    boolean started = false;

    while (lexer.cursor < lexer.component.length) {
        if (started) { /* TODO: think of a way to remove this check */
            if (++lexer.cursor == lexer.component.length) {
                break;
            }
        } else {
            started = true;
        }        

        NextToken nt = {0};

        get_token(&nt, lexer.component.data, lexer.component.length, &lexer.cursor);
        
        (*ct)[(*cursor)++] = lexer.component.data[lexer.cursor];

        switch (nt.token.kind) {
            case TOKEN_END: {
                break;
            }
            case TOKEN_INVALID: {
                break;
            }
            case TOKEN_PLACEHOLDER: {
                break;
            }
            case TOKEN_XUSE: {
                Template *t = tmpl_head;
                while (t != NULL) {
                    if (strncmp(t->name, nt.name.data, nt.name.length) == 0 && strlen(t->name) == nt.name.length) {
                        if (nt.token.tag.type == TAG_SELFCLOSING) {
                            recursive_compile_template(tmpl_head, file_content_array, (file_content_array[t->file_idx] + t->markup.start), (t->markup.end - t->markup.start), ct, cursor);
                        } else {
                            /* x-use constains x-fill(s) */
                            /* 
                                [LEFT OFF HERE]
                                IMPORTANT!
                                NOTE: maybe to make parsing more simple, x-slot and x-fill tags 
                                do not have a name attr, but are just rendered in order, meaning
                                first x-fill corresponds to first x-slot and so on and so forth
                            */
                        }
                        
                        break;
                    }
                    t = t->next;
                }
                break;
            }
            case TOKEN_XSLOT: {
                break;
            }
            case TOKEN_XFILL: {
                break;
            }
            case TOKEN_XVAL: {
                break;
            }
            case TOKEN_XFOR: {
                break;
            }
            default: {
                break;
            }
        }
    }

    return 0;
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
int ark_compile_templates(char **array_of_filepaths, char **file_content_array, uint32_t array_length) {
    uint32_t i;

    Memory *memory = reserve_memory(PAGE_SIZE * 10);

    uint32_t *html_file_mask = NULL;

    uint32_t mask_size = array_length / U32_BITS;
    mask_size = mask_size < 1 ? 1 : mask_size; /* ensure a minimum */

    html_file_mask = memory_alloc(memory, mask_size * sizeof(uint32_t));

    for (i = 0; i < array_length; i++) {
        char *file_path = array_of_filepaths[i];
        
        uint64_t ext = *(uint64_t *)(file_path + strlen(file_path) - 5 /* .html */);
        
        if ((__builtin_bswap64(ext) & MASK64_5) == HTML_EXTENSION) {
            TURN_ON_U32_BIT(html_file_mask, i);
        }
    }

    Template *tmpl_head = NULL;
    Template *prev_tmpl = NULL;
    Template *tmpl = NULL;

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
        boolean started = false;

        while (lexer.cursor < lexer.file_content.length) {
            if (started) { /* TODO: think of a way to remove this check */
                if (++lexer.cursor == lexer.file_content.length) {
                    break;
                }
            } else {
                started = true;
            }
            
            NextToken nt = {0};

            get_token(&nt, lexer.file_content.data, lexer.file_content.length, &lexer.cursor);

            switch (nt.token.kind) {
                case TOKEN_END: {
                    break;
                }
                case TOKEN_INVALID: {
                    break;
                }
                case TOKEN_PLACEHOLDER: {
                    break;
                }
                case TOKEN_XDEF: {
                    if (nt.token.tag.type == TAG_OPENING) {
                        if (oc.xdef) {
                            assert(0);
                        }
                        if (inside_xuse) {
                            assert(0);
                            /* Error: x-use can only have direct child x-fill */
                            inside_xuse = false;
                        }
                        oc.xdef += 1;
                        tmpl = memory_alloc(memory, sizeof(Template));
                        tmpl->file_idx = i;
                        tmpl->markup.start = nt.token.tag.markup.end;
                        tmpl->name = memory_alloc(memory, nt.name.length + 1);
                        memcpy(tmpl->name, nt.name.data, nt.name.length);
                        if (!prev_tmpl) {
                            tmpl_head = tmpl;
                            prev_tmpl = tmpl;
                        } else {
                            prev_tmpl->next = tmpl;
                            prev_tmpl = tmpl;
                        }
                    } else if (nt.token.tag.type == TAG_CLOSING) {
                        oc.xdef -= 1;
                        tmpl->markup.end = nt.token.tag.markup.start;
                    }
                    break;
                }
                case TOKEN_XUSE: {
                    if (nt.token.tag.type == TAG_OPENING) {
                        oc.xuse += 1;
                        if (inside_xuse) {
                            assert(0);
                            /* Error: x-use can only have direct child x-fill */
                        }
                        inside_xuse = true;
                    } else if (nt.token.tag.type == TAG_CLOSING) {
                        oc.xuse -= 1;
                        inside_xuse = false;
                    }
                    break;
                }
                case TOKEN_XSLOT: {
                    if (nt.token.tag.type != TAG_SELFCLOSING) {
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
                    if (nt.token.tag.type == TAG_OPENING) {
                        oc.xfill += 1;
                        if (!inside_xuse) {
                            assert(0);
                            /* Error: x-fill must be first child of x-use */
                        }
                        inside_xuse = false;
                    } else if (nt.token.tag.type == TAG_CLOSING) {
                        oc.xfill -= 1;
                    }
                    break;
                }
                case TOKEN_XVAL: {
                    if (nt.token.tag.type != TAG_SELFCLOSING) {
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
                    if (nt.token.tag.type == TAG_OPENING) {
                        oc.xfor += 1;
                        if (inside_xuse) {
                            assert(0);
                            /* Error: x-use can only have direct child x-fill */
                            inside_xuse = false;
                        }
                    } else if (nt.token.tag.type == TAG_CLOSING) {
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

    print_list(tmpl_head);

    Template* t = tmpl_head;
    while (t != NULL) {
        char *ct = LOCK_MEMORY(memory); /* compiled template */
        uint32_t cursor = 0;

        recursive_compile_template(tmpl_head, file_content_array, file_content_array[t->file_idx] + t->markup.start, t->markup.end - t->markup.start, &ct, &cursor);

        UNLOCK_MEMORY(memory, &ct[cursor]);

        t = t->next;
    }

    return 0;
}
