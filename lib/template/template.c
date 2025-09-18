#include <ctype.h>
#include <regex.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>

#include "../../include/ark/shared.h"
#include "../../include/ark/memory.h"
#include "../../include/ark/utils.h"
#include "../../include/ark/template.h"

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

#define NAMELABEL       0x6E616D653A000000 /* "name:" */
#define FORLABEL        0x666F723A /* "for:" */

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
    Attr *attr;
    uint32_t count;
} AttrsArray;

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

typedef struct {
    uint8_t xdef;
    uint8_t xuse;
    uint8_t xfill;
} OpenTagsCount;

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

typedef enum  { TAG_INVALID, TAG_OPENING, TAG_CLOSING, TAG_SELFCLOSING } TagType;

typedef struct {
    TagType type;
    Range markup;
    uint32_t attr_start;
} Tag;

typedef struct {
    TokenKind kind;
    Tag tag; /* NOT used when TokenKind is TOKEN_PLACEHOLDER */
    // uint64_t identifier; /* NOT used when TokenKind is TOKEN_PLACEHOLDER */
} Token;

typedef struct {
    TokenKind kind;
    struct {
        TagType type;
        Range markup;
    } tag;
} Xtag;

typedef struct {
    TokenKind kind;
    uint64_t identifier;
} TokenType;

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

uint8_t strlen_u32(uint32_t x) {
    unsigned char *bytes = (unsigned char *)&x;
    uint8_t count = 0;

    uint8_t i;
    for (i = 0; i < sizeof(uint32_t); i++) {
        if (bytes[i] != 0) {
            count++;
        }
    }

    return count;
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

    uint8_t ch = html[*cursor];

    while (true) {
        ch = html[*cursor] & 255;
        switch (attr_tk) {
            case HTML_ATTR_START: {
                while (true) {
                    uint8_t _next_ch = html[*cursor + 1];
                    if (ch == '>') {
                        attr_tk = HTML_ATTR_END;
                        break;
                    } else if (ch == '/' && _next_ch == '>') {
                        (*cursor)++; /* skip '/' */
                        attr_tk = HTML_ATTR_END;
                        break;
                    } else if (html_attr_key_token[ch]) {
                        attr_tk = HTML_ATTR_START_KEY;
                        break;
                    } else if (isspace(ch)) {
                        if (++(*cursor) == html_length) {
                            return 1;
                        }
                        
                        ch = html[*cursor] & 255;
                    } else {
                        assert(0);
                        return -1;
                    }
                }
                break;
            }
            case HTML_ATTR_START_KEY: {
                while (true) {
                    if (html_attr_key_token[ch]) {
                        attr->name.data = &html[*cursor];
                        attr_tk = HTML_ATTR_KEY;
                        str_start = *cursor;
                        break;
                    } else if (isspace(ch)) {
                        if (++(*cursor) == html_length) {
                            return 1;
                        }

                        ch = html[*cursor] & 255;
                    } else {
                        assert(0);
                        return -1;
                    }
                }
                break;
            }
            case HTML_ATTR_KEY: {
                while (true) {
                    if (isspace(ch) || ch == '=') {
                        attr->name.length = *cursor - str_start;
                        attr_tk = HTML_ATTR_EQUALS;
                        break;
                    } else if (html_attr_key_token[ch]) {
                        if (++(*cursor) == html_length) {
                            return 1;
                        }
                        
                        ch = html[*cursor] & 255;
                    }   else {
                        assert(0);
                        return -1;
                    }
                }
                break;
            }
            case HTML_ATTR_EQUALS: {
                while (true) {
                    if (ch == '=') {
                        (*cursor)++; /* skip '=' */
                        attr_tk = HTML_ATTR_START_VALUE;
                        break;
                    } else if (isspace(ch)) {
                        if (++(*cursor) == html_length) {
                            return 1;
                        }

                        ch = html[*cursor] & 255;
                    } else {
                        assert(0);
                        return -1;
                    }
                }
                break;
            }
            case HTML_ATTR_START_VALUE: {
                while (true) {
                    if (ch == '"' ||  ch == '\'') {
                        quotation = ch;
                        (*cursor)++; /* skip quotation */
                        attr->value.data = &html[*cursor];
                        attr_tk = HTML_ATTR_VALUE;
                        str_start = *cursor;
                        break;
                    } else if (isspace(ch)) {
                        if (++(*cursor) == html_length) {
                            return 1;
                        }

                        ch = html[*cursor] & 255;
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
                    if (ch == quotation && prev != '\\') {
                        attr->value.length = *cursor - str_start;
                        (*cursor)++; /* skip quotation */
                        attr_tk = HTML_ATTR_END;
                        break;
                    } else if (ch & 255) {
                        prev = ch;

                        if (++(*cursor) == html_length) {
                            return 1;
                        }

                        ch = html[*cursor] & 255;
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

    return tt;
}

/* returns 1 if reached the end, 0 if success */
int skip_whitespace(char *text, uint32_t text_length, uint32_t *cursor) {
    uint8_t ch = text[*cursor];
    while (true) {
        if (isspace(ch)) {
            ++(*cursor);

            if (text_length) {
                if (*cursor == text_length) {
                    return 1;
                }
            } else {
                if (text[*cursor] == '\0') {
                    return 1;
                }
            }

            ch = text[*cursor];
        } else {
            return 0;
        }
    }

    __builtin_unreachable();
}

int get_next_tag(String *tag, char *ptr, uint64_t identifier, TagType tag_type) {
    if (tag_type == TAG_INVALID) {
        return -1;
    }

    uint32_t open_num = 0;

    while (++ptr) {
        uint32_t tk = *(uint32_t *)(ptr);

        if (tag_type == TAG_CLOSING) {
            if ((__builtin_bswap32(tk) & MASK32_2) == DOCB) {
                char *tmp = ptr;
                ptr += 2; /** skip "{{" */
                
                uint32_t whitespace_count = 0;
                skip_whitespace(ptr, 0, &whitespace_count);
                ptr += whitespace_count;
    
                while (++ptr) {
                    uint32_t dccb = *(uint32_t *)(ptr);
                    if ((__builtin_bswap32(dccb) & MASK32_2) == DCCB) {
                        ptr += 2; /** skip "}}" */
                        tag->data = tmp;
                        tag->length = ptr - tmp;
                        return 0;
                    }
                }
            }
        } else {
            if ((__builtin_bswap32(tk) & MASK32_3) == XOPEN) {
                char *tmp = ptr;
                ptr++; /** skip '<' */
                uint64_t x = *(uint64_t *)(ptr);
                TokenType tt = token_type(x);
                if (tt.identifier == identifier) {
                    ptr += strlen_u64(tt.identifier);
                    
                    uint32_t whitespace_count = 0;
                    skip_whitespace(ptr, 0, &whitespace_count);
                    
                    ptr += whitespace_count;
    
                    if (tag_type == TAG_SELFCLOSING) {
                        ptr += 2; /** skip '/>' */
                        tag->data = tmp;
                        tag->length = ptr - tmp;
                        return 0;
                    }
                    
                    if (tag_type == TAG_OPENING) {
                        ptr++; /** skip '>' */
                        tag->data = tmp;
                        tag->length = ptr - tmp;
                        return 0;
                    }
    
                    open_num++;
                }
            } else if ((__builtin_bswap32(tk) & MASK32_4) == XCLOSE) {
                char *tmp = ptr;
                ptr += 2; /** skip "</" */
                uint64_t x = *(uint64_t *)(ptr);
                TokenType tt = token_type(x);
                if (tt.identifier == identifier) {
                    if (open_num == 0) {
                        ptr += strlen_u64(tt.identifier);
                        
                        uint32_t whitespace_count = 0;
                        skip_whitespace(ptr, 0, &whitespace_count);
    
                        ptr += whitespace_count;
                        ptr++; /** skip '<' */
    
                        if (tag_type == TAG_CLOSING) {
                            tag->data = tmp;
                            tag->length = ptr - tmp;
                            
                            return 0;
                        }
                    } else {
                        open_num--;
                    }
                }
            }
        }
    }

    return -1;
}

int tag_attr_count(uint32_t *count, uint32_t attr_start, char *html, uint32_t html_length) {
    assert(attr_start);

    while (true) {
        Attr attr = {0};
        parse_next_tag_attr(&attr, html, html_length, &attr_start);

        if (attr.name.data == NULL && attr.value.data == NULL) {
            /** no more attrs */
            break;
        }
        
        (*count) += 1;
    }

    return 0;
}

int tag_attrs_lengths(uint32_t *names_lengths, uint32_t *values_lengths, uint32_t attr_count, uint32_t attr_start, char *html, uint32_t html_length) {
    assert(attr_start);

    uint32_t i;
    for (i = 0; i < attr_count; i++) {
        Attr attr = {0};
        parse_next_tag_attr(&attr, html, html_length, &attr_start);

        if (attr.name.data == NULL && attr.value.data == NULL) {
            /** no more attrs */
            break;
        }

        names_lengths[i] = attr.name.length + 1;
        values_lengths[i] = attr.value.length + 1;
    }

    return 0;
}

int get_tag_attrs(AttrsArray *attrs_array, uint32_t attr_start, char *html, uint32_t html_length) {
    assert(attr_start);

    uint32_t i;
    for (i = 0; i < attrs_array->count; i++) {
        Attr attr = {0};
        parse_next_tag_attr(&attr, html, html_length, &attr_start);

        if (attr.name.data == NULL && attr.value.data == NULL) {
            /** no more attrs */
            break;
        }

        attrs_array->attr[i].name.length = attr.name.length;
        memcpy(attrs_array->attr[i].name.data, attr.name.data, attr.name.length);
        attrs_array->attr[i].name.data[attr.name.length] = '\0';
        
        attrs_array->attr[i].value.length = attr.value.length;
        memcpy(attrs_array->attr[i].value.data, attr.value.data, attr.value.length);
        attrs_array->attr[i].value.data[attr.value.length] = '\0';
    }

    return 0;
}

int locate_tag_name_attr(uint32_t *name_attr_index, AttrsArray *attrs_array) {
    uint32_t i;
    for (i = 0; i < attrs_array->count; i++) {

        uint32_t attr_key = *(uint32_t *)(attrs_array->attr[i].name.data);
        // if ((__builtin_bswap32(attr_key) & MASK32_4) == NAMELABEL && attrs_array->attr[i].name.length == 4) {
        //     *name_attr_index = i;
        //     return 0;
        // }
    }
    
    return -1;
}

int get_xtag(Xtag *t, char *html, uint32_t html_length, uint32_t *cursor) {
    uint32_t tk = *(uint32_t *)(html + (*cursor));
    
    if ((__builtin_bswap32(tk) & MASK32_3) == XOPEN) {
        t->tag.markup.start = (*cursor);
        (*cursor) += 1; /* skip '<' */
        
        uint64_t kind = *(uint64_t *)(html + (*cursor));

        TokenType tt = token_type(kind);
        t->kind = tt.kind;

        (*cursor) += strlen_u64(tt.identifier);

        char c = html[*cursor];
        char prev_c = 0;
        for (;;) {
            if (c == '>') {
                if (prev_c == '/') {
                    t->tag.type = TAG_SELFCLOSING;
                } else {
                    t->tag.type = TAG_OPENING;
                }

                (*cursor)++; /* skip '>' */
                t->tag.markup.end = (*cursor);
                
                break;
            }

            if (*cursor == html_length) {
                assert(0);
                return -1;
            }
            
            prev_c = c;
            (*cursor)++;
            c = html[*cursor];
        }
    } else if ((__builtin_bswap32(tk) & MASK32_4) == XCLOSE) {
        t->tag.markup.start = (*cursor);
        (*cursor) += 2; /* skip "</" */

        uint64_t kind = *(uint64_t *)(html + (*cursor));

        TokenType tt = token_type(kind);
        t->kind = tt.kind;

        (*cursor) += strlen_u64(tt.identifier);

        char c = html[*cursor];
        for (;;) {
            if (c == '>') {
                t->tag.type = TAG_CLOSING;
                (*cursor)++; /* skip '>' */
                t->tag.markup.end = (*cursor);
                
                break;
            }

            if (*cursor == html_length) {
                assert(0);
                return -1;
            }
            
            (*cursor)++;
            c = html[*cursor];
        }
    }

    return 0;
}

int get_token(Token *next_token, char *html, uint32_t html_length, uint32_t *cursor) {
    uint32_t tk = *(uint32_t *)(html + (*cursor));
    
    if ((__builtin_bswap32(tk) & MASK32_2) == DOCB) {
        next_token->tag.markup.start = (*cursor);
        next_token->kind = TOKEN_PLACEHOLDER;

        while (true) {
            if (++(*cursor) == html_length) {
                next_token->kind = TOKEN_END;
                return 1;
            }
            
            uint32_t dccb = *(uint32_t *)(html + (*cursor));
            if ((__builtin_bswap32(dccb) & MASK32_2) == DCCB) {
                (*cursor) += 2;
                next_token->tag.markup.end = (*cursor);
                break;
            }
        }

        return 0;
    }

    if ((__builtin_bswap32(tk) & MASK32_3) == XOPEN) {
        next_token->tag.markup.start = (*cursor);
        (*cursor) += 1;
        
        uint64_t kind = *(uint64_t *)(html + (*cursor));

        TokenType tt = token_type(kind);
        next_token->kind = tt.kind;
        // next_token->identifier = tt.identifier;

        (*cursor) += strlen_u64(tt.identifier);

        next_token->tag.attr_start = *cursor;

        /** skip attributes */
        while (true) {
            Attr attr = {0};
            parse_next_tag_attr(&attr, html, html_length, cursor);

            if (attr.name.data == NULL && attr.value.data == NULL) {
                /** no more attrs */
                break;
            }
        }

        uint8_t ch = html[(*cursor)];
        uint8_t prev_ch = html[(*cursor) - 1];
        
        assert(ch == '>');

        if (prev_ch == '/') {
            next_token->tag.type = TAG_SELFCLOSING;
            (*cursor)++; /* skip '>' */
            next_token->tag.markup.end = (*cursor);
        } else {
            next_token->tag.type = TAG_OPENING;
            (*cursor)++; /* skip '>' */
            next_token->tag.markup.end = (*cursor);
        }
    } else if ((__builtin_bswap32(tk) & MASK32_4) == XCLOSE) {
        next_token->tag.markup.start = (*cursor);
        (*cursor) += 2;

        uint64_t kind = *(uint64_t *)(html + (*cursor));

        TokenType tt = token_type(kind);
        next_token->kind = tt.kind;
        // next_token->identifier = tt.identifier;

        (*cursor) += strlen_u64(tt.identifier);

        int r = skip_whitespace(html, html_length, &(*cursor));
        if (r == 1) {
            next_token->kind = TOKEN_END;
            return 1;
        }

        assert(html[(*cursor)] == '>');

        next_token->tag.type = TAG_CLOSING;
        (*cursor)++; /* skip '>' */
        next_token->tag.markup.end = (*cursor);
    }

    return 0;
}

int find_tag_label_value(uint32_t *value_start, uint32_t *value_length, uint64_t key, char *html, uint32_t start_location, uint32_t html_length) {
    uint32_t cursor = start_location;

    *value_length = 0;
    
    uint64_t tk = *(uint64_t *)(html + cursor);
    for (;;) {
        if ((__builtin_bswap64(tk) & MASK64_5) == key) {                                
            cursor += strlen_u64(key); /* skip key */
            
            *value_start = cursor;

            char c = html[cursor];
            for (;;) {
                if (c == '>' || (c == '/' && html[cursor + 1] == '>') || isspace(c)) {
                    break;
                }

                if (cursor == html_length) {
                    assert(0);
                    return -1;
                }

                (*value_length)++;
                cursor++;
                c = html[cursor];
            }
            
            break;
        }
        
        if (cursor == html_length) {
            assert(0);
            return -1;
        }

        cursor++;
        tk = *(uint64_t *)(html + cursor);
    }

    return 0;
}

int ark_get_templates_info(uint32_t *templates_names_lengths, uint32_t *templates_lengths, uint32_t template_count, uint32_t *html_file_mask, char **array_of_filepaths, char **file_content_array, uint32_t array_length) {
    uint32_t i;
    
    uint32_t count = 0;
    
    for (i = 0; i < array_length; i++) {
        if (IS_U32_BIT_OFF(html_file_mask, i)) {
            continue;
        }

        char *file_path = array_of_filepaths[i];
        char *file_content = file_content_array[i];

        Lexer lexer = {0};

        lexer.file_content.data = file_content;
        lexer.file_content.length = strlen(file_content);
        lexer.cursor = 0;

        char *start = NULL;

        while (lexer.cursor < lexer.file_content.length) {
            Xtag t = {0};

            get_xtag(&t, lexer.file_content.data, lexer.file_content.length, &lexer.cursor);

            switch (t.kind) {
                case TOKEN_XDEF: {
                    if (t.tag.type == TAG_OPENING) {
                        uint32_t cursor = t.tag.markup.start + 6 ;// skip 6 chars for "<x-def"

                        uint32_t value_start = 0;
                        uint32_t value_length = 0;
                        uint64_t key = NAMELABEL;
                        find_tag_label_value(&value_start, &value_length, key, lexer.file_content.data, cursor, lexer.file_content.length);
                        
                        templates_names_lengths[count] = value_length;

                        start = lexer.file_content.data + t.tag.markup.end;

                        assert(templates_names_lengths[count] > 0 && "x-tag does not have a name assigned\n");

                        break;
                    }
                    
                    if (t.tag.type == TAG_CLOSING) {
                        char *end = lexer.file_content.data + t.tag.markup.start;
                        templates_lengths[count] = end - start;

                        count++;
                        
                        start = NULL;
                        break;
                    }

                    assert(0);
                }       
                default: {
                    break;
                }
            }

            lexer.cursor++;
        }
    }

    if (count != template_count) {
        assert(0);
    }

    return 0;
}

int ark_make_templates_array(char **template_name_array, char **template_content_array, uint32_t template_count, uint32_t *html_file_mask, char **array_of_filepaths, char **file_content_array, uint32_t array_length) {
    uint32_t i;
    
    uint32_t count = 0;
    
    for (i = 0; i < array_length; i++) {
        if (IS_U32_BIT_OFF(html_file_mask, i)) {
            continue;
        }

        char *file_path = array_of_filepaths[i];
        char *file_content = file_content_array[i];

        Lexer lexer = {0};

        lexer.file_content.data = file_content;
        lexer.file_content.length = strlen(file_content);
        lexer.cursor = 0;

        char *start = NULL;

        while (lexer.cursor < lexer.file_content.length) {
            Xtag t = {0};

            get_xtag(&t, lexer.file_content.data, lexer.file_content.length, &lexer.cursor);

            switch (t.kind) {
                case TOKEN_XDEF: {
                    if (t.tag.type == TAG_OPENING) {
                        uint32_t cursor = t.tag.markup.start + 6 ;// skip 6 chars for "<x-def"

                        uint32_t value_start = 0;
                        uint32_t value_length = 0;
                        uint64_t key = NAMELABEL;
                        find_tag_label_value(&value_start, &value_length, key, lexer.file_content.data, cursor, lexer.file_content.length);
                        
                        memcpy(template_name_array[count], lexer.file_content.data + value_start, value_length);

                        start = lexer.file_content.data + t.tag.markup.end;

                        assert(strlen(template_name_array[count]) > 0 && "x-tag does not have a name assigned\n");

                        break;
                    } 
                    
                    if (t.tag.type == TAG_CLOSING) {
                        char *end = lexer.file_content.data + t.tag.markup.start;
                        
                        uint32_t length = end - start;
                        memcpy(template_content_array[count], start, length);

                        count++;

                        start = NULL;
                        break;
                    }

                    assert(0);
                }       
                default: {
                    break;
                }
            }

            lexer.cursor++;
        }
    }

    if (count != template_count) {
        assert(0);
    }

    return 0;
}

int ark_templates_count(uint32_t *count, uint32_t *html_file_mask, char **array_of_filepaths, char **file_content_array, uint32_t array_length) {
    uint32_t i;
    for (i = 0; i < array_length; i++) {
        if (IS_U32_BIT_OFF(html_file_mask, i)) {
            continue;
        }

        char *file_path = array_of_filepaths[i];
        char *file_content = file_content_array[i];

        Lexer lexer = {0};

        lexer.file_content.data = file_content;
        lexer.file_content.length = strlen(file_content);
        lexer.cursor = 0;

        while (lexer.cursor < lexer.file_content.length) {
            Xtag t = {0};
            get_xtag(&t, lexer.file_content.data, lexer.file_content.length, &lexer.cursor);

            switch (t.kind) {
                case TOKEN_XDEF: {
                    if (t.tag.type == TAG_OPENING) {
                        *count += 1;
                    }
                    break;
                }       
                default: {
                    break;
                }
            }

            lexer.cursor++;
        }
    }

    return 0;
}

int ark_make_html_file_mask(uint32_t *html_file_mask, char **array_of_filepaths, uint32_t array_length) {
    uint32_t i;
    for (i = 0; i < array_length; i++) {
        char *file_path = array_of_filepaths[i];
        
        uint64_t ext = *(uint64_t *)(file_path + strlen(file_path) - 5 /* .html */);
        
        if ((__builtin_bswap64(ext) & MASK64_5) == HTML_EXTENSION) {
            TURN_ON_U32_BIT(html_file_mask, i);
        }
    }

    return 0;
}

int ark_validate_templates(uint32_t *html_file_mask, char **array_of_filepaths, char **file_content_array, uint32_t array_length) {
    uint32_t i;

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

        OpenTagsCount open_tags_count = {0};
        boolean inside_xuse = false;

        while (lexer.cursor < lexer.file_content.length) {
            Xtag t = {0};
            get_xtag(&t, lexer.file_content.data, lexer.file_content.length, &lexer.cursor);
            
            switch (t.kind) {
                case TOKEN_XDEF: {
                    switch (t.tag.type) {
                        case TAG_OPENING: {
                            if (inside_xuse) {
                                assert(0 && "x-use can only have direct child x-fill\n");
                            }
                            
                            if (open_tags_count.xdef > 0) {
                                assert(0 && "can't nest x-def\n");
                            }

                            open_tags_count.xdef += 1;
                            break;
                        }
                        case TAG_CLOSING: {
                            if (inside_xuse) {
                                assert(0 && "x-use can only have direct child x-fill\n");
                            }

                            if (open_tags_count.xdef < 1) {
                                assert(0 && "no x-def tag open, so can't close it\n");
                            }

                            open_tags_count.xdef -= 1;
                            break;
                        }
                        default: {
                            assert(0 && "t.tag.type must be TAG_OPENING or TAG_CLOSING\n");
                        }
                    }

                    break;
                }
                case TOKEN_XUSE: {
                    switch (t.tag.type) {
                        case TAG_OPENING: {
                            if (inside_xuse) {
                                assert(0 && "x-use can only have direct child x-fill\n");
                            }
                            
                            inside_xuse = true;
                            open_tags_count.xuse += 1;
                            break;
                        }
                        case TAG_CLOSING: {
                            if (!inside_xuse) {
                                assert(0 && "inside_xuse when x-use close\n");
                            }

                            if (open_tags_count.xuse < 1) {
                                assert(0 && "no x-use tag open, so can't close it\n");
                            }

                            inside_xuse = false;
                            open_tags_count.xuse -= 1;
                            break;
                        }
                        case TAG_SELFCLOSING: {
                            if (inside_xuse) {
                                assert(0 && "x-use can only have direct child x-fill\n");
                            }
                            
                            break;
                        }
                        default: {
                            assert(0 && "t.tag.type must be TAG_OPENING, TAG_CLOSING or TAG_SELFCLOSING\n");
                        }
                    }

                    break;
                }
                case TOKEN_XSLOT: {
                    switch (t.tag.type) {
                        case TAG_SELFCLOSING: {
                            if (inside_xuse) {
                                assert(0 && "x-use can only have direct child x-fill\n");
                            }
                            
                            break;
                        }
                        default: {
                            assert(0 && "t.tag.type must be TAG_SELFCLOSING\n");
                        }
                    }

                    break;
                }
                case TOKEN_XFILL: {
                    switch (t.tag.type) {
                        case TAG_OPENING: {
                            if (!inside_xuse) {
                                assert(0 && "x-fill must be first child of x-use\n");
                            }

                            inside_xuse = false;
                            open_tags_count.xfill += 1;
                            break;
                        }
                        case TAG_CLOSING: {
                            if (inside_xuse) {
                                assert(0 && "x-use (opening tag) can only have direct child x-fill\n");
                            }
                            
                            inside_xuse = true;
                            open_tags_count.xfill -= 1;
                            break;
                        }
                        default: {
                            assert(0 && "t.tag.type must be TAG_OPENING or TAG_CLOSING\n");
                        }
                    }

                    break;
                }
                default: {
                    break;
                }
            }
            
            lexer.cursor++;
        }
        
        if (open_tags_count.xdef | open_tags_count.xuse | open_tags_count.xfill) {
            if (open_tags_count.xdef) {
                assert(0 && "forgot to close open_tags_count.xdef amount of xdef tag\n");
            }
            if (open_tags_count.xuse) {
                assert(0 && "forgot to close open_tags_count.xuse amount of xuse tag\n");
            }
            if (open_tags_count.xfill) {
                assert(0 && "forgot to close open_tags_count.xfill amount of xfill tag\n");
            }
        }
    }

    return 0;
}


/*
- Error if x-slot are not self-closing.
- Error if x-def, x-fill do not have a pair (open + closing) xtags.

- Error if x-def has a parent.
- Error if x-fill has other parent than x-use.
- Error if x-use has other child than x-fill.

- Error if any opening or self-closing xtag does not have a name attribute.
- Error if any xtag except x-use opening or self-closing has more than the name attribute.

- Error if recursive import.
*/
int ark_validate_templates_old(uint32_t *html_file_mask, char **array_of_filepaths, char **file_content_array, uint32_t array_length) {
    uint32_t i;

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
            Token nt = {0};

            get_token(&nt, lexer.file_content.data, lexer.file_content.length, &lexer.cursor);

            switch (nt.kind) {
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
                    if (nt.tag.type == TAG_OPENING) {
                        if (oc.xdef) {
                            assert(0);
                        }
                        if (inside_xuse) {
                            assert(0);
                            /* Error: x-use can only have direct child x-fill */
                            inside_xuse = false;
                        }
                        oc.xdef += 1;
                    } else if (nt.tag.type == TAG_CLOSING) {
                        oc.xdef -= 1;
                    }
                    break;
                }
                case TOKEN_XUSE: {
                    if (nt.tag.type == TAG_OPENING) {
                        oc.xuse += 1;
                        if (inside_xuse) {
                            assert(0);
                            /* Error: x-use can only have direct child x-fill */
                        }
                        inside_xuse = true;
                    } else if (nt.tag.type == TAG_CLOSING) {
                        oc.xuse -= 1;
                        inside_xuse = false;
                    }
                    break;
                }
                case TOKEN_XSLOT: {
                    if (nt.tag.type != TAG_SELFCLOSING) {
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
                    if (nt.tag.type == TAG_OPENING) {
                        oc.xfill += 1;
                        if (!inside_xuse) {
                            assert(0);
                            /* Error: x-fill must be first child of x-use */
                        }
                        inside_xuse = false;
                    } else if (nt.tag.type == TAG_CLOSING) {
                        oc.xfill -= 1;
                        inside_xuse = true;
                    }
                    break;
                }
                // case TOKEN_XVAL: {
                //     if (nt.tag.type != TAG_SELFCLOSING) {
                //         /* print error! */
                //         assert(0);
                //     }
                //     if (inside_xuse) {
                //         assert(0);
                //         /* Error: x-use can only have direct child x-fill */
                //     }
                //     break;
                // }
                // case TOKEN_XFOR: {
                //     if (nt.tag.type == TAG_OPENING) {
                //         oc.xfor += 1;
                //         if (inside_xuse) {
                //             assert(0);
                //             /* Error: x-use can only have direct child x-fill */
                //             inside_xuse = false;
                //         }
                //     } else if (nt.tag.type == TAG_CLOSING) {
                //         oc.xfor -= 1;
                //     }
                //     break;
                // }
                default: {
                    break;
                }
            }

            lexer.cursor++;
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

    return 0;
}

int compile_template(char *rendering, char **template_name_array, char **template_content_array, uint32_t template_count) {
    Lexer lexer = {0};
    lexer.file_content.data = rendering;
    lexer.cursor = 0;

    uint8_t xfill_open_tags_count = 0;
    uint32_t saved_location = 0;

    String import = {0};
    uint32_t import_cursor = 0;

    go_again:;
    boolean repeat = false;

    while (lexer.file_content.data[lexer.cursor]) {
        Xtag t = {0};
        get_xtag(&t, lexer.file_content.data, lexer.file_content.length, &lexer.cursor);

        if (t.kind == TOKEN_XUSE) {
            repeat = true;
        }

        switch (t.kind) {
            case TOKEN_XUSE: {
                if (xfill_open_tags_count) break;
                switch (t.tag.type) {
                    case TAG_OPENING: {
                        uint32_t cursor = t.tag.markup.start + 6 ;// skip 6 chars for "<x-use"

                        uint32_t name_value_start = 0;
                        uint32_t name_value_length = 0;
                        uint64_t key = NAMELABEL;
                        find_tag_label_value(&name_value_start, &name_value_length, key, lexer.file_content.data, cursor, lexer.file_content.length);

                        uint32_t j;
                        for (j = 0; j < template_count; j++) {
                            char *name = template_name_array[j];
                            uint32_t name_length = strlen(name);
                            
                            if (strncmp(name, lexer.file_content.data + name_value_start, name_value_length) == 0 && name_length == name_value_length) {
                                import.data = template_content_array[j];
                                import.length = strlen(template_content_array[j]);
                                break;
                            }
                        }
                        
                        Range opening_tag_markup = t.tag.markup;

                        saved_location = opening_tag_markup.start;
                            
                        char *dest = lexer.file_content.data + opening_tag_markup.start;
                        char *src = lexer.file_content.data + opening_tag_markup.end;
                        size_t chunk_length = strlen(src) + 1;
                        memmove(dest, src, chunk_length);

                        lexer.cursor = opening_tag_markup.start;
                        
                        break;
                    }
                    case TAG_CLOSING: {
                        Range closing_tag_markup = t.tag.markup;
                    
                        char *insert_location = lexer.file_content.data + closing_tag_markup.start;
                        
                        size_t substring_to_remove_length = closing_tag_markup.end - closing_tag_markup.start;

                        char *substring_to_insert = import.data + import_cursor;
                        size_t substring_to_insert_length = import.length - import_cursor;

                        /** Expand/Shrink space where new substring will inserted so it fits perfectly */
                        char *dest = insert_location + substring_to_insert_length;
                        char *src = insert_location + substring_to_remove_length;
                        size_t chunk_length = strlen(lexer.file_content.data + closing_tag_markup.end) + 1;
                        memmove(dest, src, chunk_length);

                        memcpy(insert_location, substring_to_insert, substring_to_insert_length);
                        
                        lexer.cursor = saved_location;
                        
                        bzero(&import, sizeof(String));
                        import_cursor = 0;
                        saved_location = 0;
                        
                        break;
                    }
                    case TAG_SELFCLOSING: {
                        uint32_t cursor = t.tag.markup.start + 6 ;// skip 6 chars for "<x-use"

                        uint32_t name_value_start = 0;
                        uint32_t name_value_length = 0;
                        uint64_t key = NAMELABEL;
                        find_tag_label_value(&name_value_start, &name_value_length, key, lexer.file_content.data, cursor, lexer.file_content.length);

                        String to_import = {0};

                        uint32_t j;
                        for (j = 0; j < template_count; j++) {
                            char *name = template_name_array[j];
                            uint32_t name_length = strlen(name);
                            
                            if (strncmp(name, lexer.file_content.data + name_value_start, name_value_length) == 0 && name_length == name_value_length) {
                                to_import.data = template_content_array[j];
                                to_import.length = strlen(template_content_array[j]);
                                break;
                            }
                        }

                        Range tag_markup = t.tag.markup;

                        char *insert_location = lexer.file_content.data + tag_markup.start;
                        
                        size_t substring_to_remove_length = tag_markup.end - tag_markup.start;

                        char *substring_to_insert = to_import.data;
                        size_t substring_to_insert_length = to_import.length;

                        /** Expand/Shrink space where new substring will inserted so it fits perfectly */
                        char *dest = insert_location + substring_to_insert_length;
                        char *src = insert_location + substring_to_remove_length;
                        size_t chunk_length = strlen(lexer.file_content.data + tag_markup.end) + 1;
                        memmove(dest, src, chunk_length);

                        memcpy(insert_location, substring_to_insert, substring_to_insert_length);

                        lexer.cursor = tag_markup.start; // the newly imported component may have components to import, so let's go back
                    }
                    default: {
                        break;
                    }
                }
                break;
            }
            case TOKEN_XFILL: {
                switch (t.tag.type) {
                    case TAG_OPENING: {
                        if (xfill_open_tags_count == 0) {
                            Range opening_tag_markup = t.tag.markup;
                    
                            char *insert_location = lexer.file_content.data + opening_tag_markup.start;
                            
                            size_t substring_to_remove_length = opening_tag_markup.end - opening_tag_markup.start;

                            char *substring_to_insert = (import.data + import_cursor);
                            String xslot_selfclosing_tag = {0};
                            get_next_tag(&xslot_selfclosing_tag, (import.data + import_cursor), XSLOT, TAG_SELFCLOSING);
                            size_t substring_to_insert_length = xslot_selfclosing_tag.data - substring_to_insert;

                            import_cursor += substring_to_insert_length + xslot_selfclosing_tag.length;

                            /* Expand/Shrink space where new substring will inserted so it fits perfectly */
                            char *dest = insert_location + substring_to_insert_length;
                            char *src = insert_location + substring_to_remove_length;
                            size_t chunk_length = strlen(lexer.file_content.data + opening_tag_markup.end) + 1;
                            memmove(dest, src, chunk_length);

                            memcpy(insert_location, substring_to_insert, substring_to_insert_length);

                            saved_location = (insert_location + substring_to_insert_length) - lexer.file_content.data;
                            lexer.cursor = saved_location;
                        }

                        xfill_open_tags_count++;
                        
                        break;
                    }
                    case TAG_CLOSING: {
                        xfill_open_tags_count--;

                        if (xfill_open_tags_count == 0) {
                            Range closing_tag_markup = t.tag.markup;

                            char *dest = lexer.file_content.data + closing_tag_markup.start;
                            char *src = lexer.file_content.data + closing_tag_markup.end;
                            size_t chunk_length = strlen(src) + 1;
                            memmove(dest, src, chunk_length);

                            lexer.cursor -= 1;
                        }
                        
                        break;
                    }
                    default: {
                        break;
                    }
                }
                break;
            }
            default: {
                break;
            }
        }
        
        lexer.cursor++;
    }

    if (repeat) {
        lexer.cursor = 0;
        xfill_open_tags_count = 0;
        
        goto go_again;
    }

    rendering[lexer.cursor] = '\0';
    
    return 0;
}

int compile_template_old(char *rendering, char **template_name_array, char **template_content_array, uint32_t template_count) {
    Lexer lexer = {0};
    lexer.file_content.data = rendering;
    lexer.cursor = 0;

    String import = {0};
    uint32_t import_cursor = 0;

    uint32_t saved_location = 0;
    
    uint32_t xfill_count = 0;
    
    AttrsArray attrs_array = {0};
    boolean attrs_array_in_use = false;
    uint32_t name_attr_index = 0;
    uint32_t attrs_rendered = 0;

    go_again:;
    boolean repeat = false;
    
    while (lexer.file_content.data[lexer.cursor]) {
        Token nt = {0};

        get_token(&nt, lexer.file_content.data, lexer.file_content.length, &lexer.cursor);

        if (nt.kind == TOKEN_XUSE && nt.tag.type != TAG_CLOSING && !attrs_array_in_use) {
            uint32_t attr_count = 0;
            tag_attr_count(&attr_count, nt.tag.attr_start, lexer.file_content.data, lexer.file_content.length);
            
            uint32_t *names_lengths = malloc(attr_count * sizeof(uint32_t));
            uint32_t *values_lengths = malloc(attr_count * sizeof(uint32_t));
            tag_attrs_lengths(names_lengths, values_lengths, attr_count, nt.tag.attr_start, lexer.file_content.data, lexer.file_content.length);
            
            attrs_array.count = attr_count;
            attrs_array.attr = malloc(attr_count * sizeof(Attr));

            uint32_t j;

            for (j = 0; j < attr_count; j++) {
                attrs_array.attr[j].name.data = malloc(names_lengths[j] * sizeof(char));
                attrs_array.attr[j].value.data = malloc(values_lengths[j] * sizeof(char));
            }

            free(names_lengths);
            free(values_lengths);

            get_tag_attrs(&attrs_array, nt.tag.attr_start, lexer.file_content.data, lexer.file_content.length);
            locate_tag_name_attr(&name_attr_index, &attrs_array);

            if (attr_count > 1) {
                attrs_array_in_use = true;
            }
        }

        if (nt.kind == TOKEN_XUSE) {
            repeat = true;
        }

        switch (nt.kind) {
            // case TOKEN_PLACEHOLDER: {
            //     String name = {0};

            //     char *p = lexer.file_content.data + nt.tag.markup.start;
            //     p += 2; /** skip "{{" */
            //     uint32_t whitespace_count = 0;
            //     skip_whitespace(p, 0, &whitespace_count);
            //     p += whitespace_count;

            //     name.data = p;
            //     while (!isspace(*p)) {
            //         name.length++;
            //         p++;
            //     }
                
            //     uint32_t j;
            //     for (j = 0; j < attrs_array.count; j++) {
            //         if (strncmp(attrs_array.attr[j].name.data, name.data, name.length) == 0) {
            //             Range placeholder = nt.tag.markup;
                    
            //             char *insert_location = lexer.file_content.data + placeholder.start;
                        
            //             size_t substring_to_remove_length = placeholder.end - placeholder.start;

            //             char *substring_to_insert = attrs_array.attr[j].value.data;
            //             size_t substring_to_insert_length = attrs_array.attr[j].value.length;

            //             {
            //                 /** Expand/Shrink space where new substring will inserted so it fits perfectly */
            //                 char *dest = insert_location + substring_to_insert_length;
            //                 char *src = insert_location + substring_to_remove_length;
            //                 size_t chunk_length = strlen(lexer.file_content.data + placeholder.end) + 1;
            //                 memmove(dest, src, chunk_length);
            //             }

            //             memcpy(insert_location, substring_to_insert, substring_to_insert_length);
            //             attrs_rendered++;

            //             if (attrs_rendered == attrs_array.count) {
            //                 uint32_t k;
            //                 for (k = 0; k < attrs_array.count; k++) {
            //                     free(attrs_array.attr[k].name.data);
            //                     attrs_array.attr[k].name.data = NULL;
            //                     free(attrs_array.attr[k].value.data);
            //                     attrs_array.attr[k].value.data = NULL;
            //                 }
                            
            //                 free(attrs_array.attr);
            //                 attrs_array.attr = NULL;
                            
            //                 bzero(&attrs_array, sizeof(AttrsArray));
            //                 attrs_rendered = 0;

            //                 name_attr_index = 0;
            //                 attrs_array_in_use = false;
            //             }

            //             break;
            //         }
            //     }

            //     printf("\n");

            //     break;
            // }
            case TOKEN_XUSE: {
                if (xfill_count) {
                    break;
                }

                uint32_t j;
                for (j = 0; j < template_count; j++) {
                    char *name = template_name_array[j];
                    uint32_t name_length = strlen(name);

                    String attr_value = attrs_array.attr[name_attr_index].value;
                    
                    if (strncmp(name, attr_value.data, attr_value.length) == 0 && name_length == attr_value.length) {
                        import.data = template_content_array[j];
                        import.length = strlen(template_content_array[j]);
                        break;
                    }
                }
                
                if (nt.tag.type == TAG_SELFCLOSING) {                                                
                    Range tag_markup = nt.tag.markup;
                    
                    saved_location = tag_markup.start;

                    char *insert_location = lexer.file_content.data + tag_markup.start;
                    
                    size_t substring_to_remove_length = tag_markup.end - tag_markup.start;

                    char *substring_to_insert = import.data;
                    size_t substring_to_insert_length = import.length;

                    {
                        /** Expand/Shrink space where new substring will inserted so it fits perfectly */
                        char *dest = insert_location + substring_to_insert_length;
                        char *src = insert_location + substring_to_remove_length;
                        size_t chunk_length = strlen(lexer.file_content.data + tag_markup.end) + 1;
                        memmove(dest, src, chunk_length);
                    }

                    memcpy(insert_location, substring_to_insert, substring_to_insert_length);

                    lexer.cursor = tag_markup.start;
                }

                if (nt.tag.type == TAG_OPENING) {
                    Range opening_tag_markup = nt.tag.markup;

                    saved_location = opening_tag_markup.start;
                        
                    char *dest = lexer.file_content.data + opening_tag_markup.start;
                    char *src = lexer.file_content.data + opening_tag_markup.end;
                    size_t chunk_length = strlen(src) + 1;
                    memmove(dest, src, chunk_length);

                    lexer.cursor = opening_tag_markup.start;
                }

                if (nt.tag.type == TAG_CLOSING) {
                    Range closing_tag_markup = nt.tag.markup;
                    
                    char *insert_location = lexer.file_content.data + closing_tag_markup.start;
                    
                    size_t substring_to_remove_length = closing_tag_markup.end - closing_tag_markup.start;

                    char *substring_to_insert = import.data + import_cursor;
                    size_t substring_to_insert_length = import.length - import_cursor;

                    {
                        /** Expand/Shrink space where new substring will inserted so it fits perfectly */
                        char *dest = insert_location + substring_to_insert_length;
                        char *src = insert_location + substring_to_remove_length;
                        size_t chunk_length = strlen(lexer.file_content.data + closing_tag_markup.end) + 1;
                        memmove(dest, src, chunk_length);
                    }

                    memcpy(insert_location, substring_to_insert, substring_to_insert_length);
                    
                    lexer.cursor = saved_location;
                    
                    bzero(&import, sizeof(String));
                    import_cursor = 0;
                    saved_location = 0;

                    attrs_array_in_use = false;
                }

                break;
            }
            case TOKEN_XFILL: {
                if (nt.tag.type == TAG_OPENING) {
                    if (xfill_count == 0) {
                        Range opening_tag_markup = nt.tag.markup;
                    
                        char *insert_location = lexer.file_content.data + opening_tag_markup.start;
                        
                        size_t substring_to_remove_length = opening_tag_markup.end - opening_tag_markup.start;

                        char *substring_to_insert = (import.data + import_cursor);
                        String xslot_selfclosing_tag = {0};
                        get_next_tag(&xslot_selfclosing_tag, (import.data + import_cursor), XSLOT, TAG_SELFCLOSING);
                        size_t substring_to_insert_length = xslot_selfclosing_tag.data - substring_to_insert;

                        import_cursor += substring_to_insert_length + xslot_selfclosing_tag.length;

                        {
                            /* Expand/Shrink space where new substring will inserted so it fits perfectly */
                            char *dest = insert_location + substring_to_insert_length;
                            char *src = insert_location + substring_to_remove_length;
                            size_t chunk_length = strlen(lexer.file_content.data + opening_tag_markup.end) + 1;
                            memmove(dest, src, chunk_length);
                        }

                        memcpy(insert_location, substring_to_insert, substring_to_insert_length);


                        saved_location = (insert_location + substring_to_insert_length) - lexer.file_content.data;
                        lexer.cursor = saved_location;
                    }

                    xfill_count++;
                }
                
                if (nt.tag.type == TAG_CLOSING) {
                    xfill_count--;

                    if (xfill_count == 0) {
                        Range closing_tag_markup = nt.tag.markup;

                        char *dest = lexer.file_content.data + closing_tag_markup.start;
                        char *src = lexer.file_content.data + closing_tag_markup.end;
                        size_t chunk_length = strlen(src) + 1;
                        memmove(dest, src, chunk_length);

                        lexer.cursor -= 1;
                    }
                }
            }
            default: {
                break;
            }
        }

        lexer.cursor++;
    }

    if (repeat) {
        lexer.cursor = 0;
        xfill_count = 0;
        
        goto go_again;
    }
    
    rendering[lexer.cursor] = '\0';

    return 0;
}

int ark_get_compile_templates_lengths(uint32_t *compiled_templates_lengths, char **template_name_array, char **template_content_array, uint32_t template_count) {
    Memory *memory = reserve_memory(PAGE_SIZE * 5);
    
    uint32_t i;
    
    for (i = 0; i < template_count; i++) {
        char *rendering = LOCK_MEMORY(memory);
        memcpy(rendering, template_content_array[i], strlen(template_content_array[i]));

        compile_template(rendering, template_name_array, template_content_array, template_count);

        uint32_t length = strlen(rendering);
        UNLOCK_MEMORY(memory, rendering + length + 1);

        compiled_templates_lengths[i] = strlen(rendering);

        memory_reset(memory, (uint8_t *)rendering);
    }

    return 0;
}

int ark_compile_templates(char **compiled_template_content_array, char **template_name_array, char **template_content_array, uint32_t template_count) {    
    uint32_t i;
    
    for (i = 0; i < template_count; i++) {
        char *rendering = compiled_template_content_array[i];
        memcpy(rendering, template_content_array[i], strlen(template_content_array[i]));
        compile_template(rendering, template_name_array, template_content_array, template_count);
    }

    return 0;
}

/*

LEFT OFF HERE!

count the amount of :for and for each for have a Range (start, end), for rendering you 
pick the for you want to render in the order you want and when rendering that for if you are 
rendering the amount of iterations the renderer basically multiplies the code section by the 
amount of times you want, if you are rendering the repleaceable from a for the renderer
will take care of understanding when its out of the current for scope and when its inside so 
it doesnt render a repleaceable outside if the current for scope. So we are not really using
a tree but a array.

*/

int ark_count_template_rendering_ctx_amount(uint32_t *template_rendering_ctx_count, char **template_content_array, uint32_t template_count) {
    uint32_t i;
    for (i = 0; i < template_count; i++) {
        uint32_t cursor = 0;

        char *component = template_content_array[i];
        uint32_t component_length = strlen(component);
        
        uint32_t x = *(uint32_t *)(component + cursor);
        for (;;) {
            if ((__builtin_bswap32(x) & MASK32_4) == FORLABEL) {
                template_rendering_ctx_count[i] += 1;
            }

            if (cursor == component_length) {
                break;
            }
            
            cursor++;
            x = *(uint32_t *)(component + cursor);
        }
    }

    return 0;
}


int ark_get_single_template_rendering_ctx_lengths_info(uint32_t *ctx_names_lengths, uint32_t ctx_arr_length, char *template_content, uint32_t template_content_length) {
    uint32_t cursor = 0;
    
    uint32_t count = 0;

    uint32_t x = *(uint32_t *)(template_content + cursor);
    for (;;) {
        if ((__builtin_bswap32(x) & MASK32_4) == FORLABEL) {
            cursor += strlen_u32(FORLABEL); /* skip "for:" */

            char c = template_content[cursor];
            for (;;) {
                if (c == '>' || (c == '/' && template_content[cursor + 1] == '>') || isspace(c)) {
                    break;
                }

                if (cursor == template_content_length) {
                    assert(0);
                    return -1;
                }

                ctx_names_lengths[count]++;
                cursor++;
                c = template_content[cursor];
            }
            
            count++;
        }

        if (cursor == template_content_length) {
            break;
        }
        
        cursor++;
        x = *(uint32_t *)(template_content + cursor);
    }

    if (count != ctx_arr_length) {
        assert(0);
    }

    return 0;
}

int ark_get_single_template_rendering_ctx(char **ctx_names, uint64_t *ctx_arr, uint32_t ctx_arr_length, char *template_content, uint32_t template_content_length) {
    // uint64_t *ctx_arr -> high 32bits for start, low 32bits for end

    uint32_t cursor = 0;
    
    uint32_t count = 0;

    uint32_t x = *(uint32_t *)(template_content + cursor);
    for (;;) {
        if ((__builtin_bswap32(x) & MASK32_4) == FORLABEL) {
            cursor += strlen_u32(FORLABEL); /* skip "for:" */

            char *name = &template_content[cursor];
            uint32_t name_length = 0;

            char c = template_content[cursor];
            for (;;) {
                if (c == '>' || (c == '/' && template_content[cursor + 1] == '>') || isspace(c)) {
                    break;
                }

                if (cursor == template_content_length) {
                    assert(0);
                    return -1;
                }

                name_length++;
                cursor++;
                c = template_content[cursor];
            }

            memcpy(ctx_names[count], name, name_length);
            count++;
        }

        if (cursor == template_content_length) {
            break;
        }
        
        cursor++;
        x = *(uint32_t *)(template_content + cursor);
    }

    if (count != ctx_arr_length) {
        assert(0);
    }

    return 0;
}


