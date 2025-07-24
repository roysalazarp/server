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

#define COMPONENT_DEFINITION_IDENTIFIER "x-def"
#define COMPONENT_IMPORT_IDENTIFIER "x-use"
#define SLOT_IDENTIFIER "x-slot"
#define FILL_IDENTIFIER "x-fill"
#define VAL_IDENTIFIER "x-val"
#define FOR_IDENTIFIER "x-for"

#define HTML_ATTR_START       0
#define HTML_ATTR_START_KEY   1
#define HTML_ATTR_KEY         2
#define HTML_ATTR_EQUALS      3
#define HTML_ATTR_START_VALUE 4
#define HTML_ATTR_VALUE       5
#define HTML_ATTR_END         6

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

typedef enum {
    TOKEN_EOF = 0,

    TOKEN_COMPONENT_DEFINITION = 1,
    TOKEN_COMPONENT_IMPORT = 2,
    TOKEN_SLOT = 3,
    TOKEN_FILL = 4,
    TOKEN_VAL = 5,
    TOKEN_FOR = 6,

    TOKEN_PLACEHOLDER,

    TOKEN_INVALID
} TokenKind;

typedef struct {
    enum { TAG_OPENING, TAG_CLOSING, TAG_SELFCLOSING } type;
    String markup;
} Tag;

typedef struct {
    TokenKind kind;
    Tag tag; /* when is TokenKind is TOKEN_PLACEHOLDER this can be just left empty */
    String identifier;
} Token;

typedef struct {
    String file_path;
    String file_content;
    uint32_t cursor;
    uint32_t line_number;
} Lexer;

typedef struct {
    String name;
    String value;
} Attribute;

typedef struct {
    Attribute *attributes;
    uint32_t name_attr_index;
    uint32_t count;
} AttributeArray;

typedef struct Node Node;
struct Node {
    Token token;
    uint32_t line_number;
    AttributeArray attribute_array;
    Node *parent;
    String block;
};

typedef struct ASTNode ASTNode;
struct ASTNode {
    Token token;
    uint32_t line_number;
    AttributeArray attribute_array;
    String block;
    ASTNode *first_child;
    ASTNode *next_sibling;
};

typedef struct {
    Node **data;
    uint8_t count;
} NodeArray;

typedef struct {
    Node *node;
    TokenKind kind;
} TagStackElement;

typedef struct {
    TagStackElement *data;
    uint8_t count;
} TagStack;


#ifndef ARK_MAX_TEMPLATE_NAME_LENGTH
#define ARK_MAX_TEMPLATE_NAME_LENGTH 32
#endif

typedef char Name[ARK_MAX_TEMPLATE_NAME_LENGTH]; /* 2 of these fit nicely in a 64 bytes cache line */

typedef struct {
    Name *names;
    ASTNode **ast_nodes;
    uint32_t count;
} SlotSOA;

typedef struct {
    Name *names;
    uint32_t count;
} PlaceholderSOA;

typedef struct {
    Name *names;
    ASTNode **ast_nodes;
    uint32_t count;
} FillSOA;

typedef struct {
    Name *names;
    ASTNode **ast_nodes;
    FillSOA *fills_soa;
    uint32_t count;
} ImportSOA;

typedef struct {
    Name *names;
    String *components;
    ASTNode **ast_nodes;
    PlaceholderSOA **placeholders_soa;
    ImportSOA **imports_soa;
    SlotSOA **slots_soa;
    String *file_paths;
    String *contents;
    uint32_t count;
} ComponentSOA;

char *_compiled_component = NULL;

typedef struct {
    uint32_t i;
    ASTNode *node;
    String name;
    size_t pos_start;
} ComplComp;

#define HTML_EXTENSION           0x2E68746D6C000000 /* ".html" */

boolean is_html_path(char *path, size_t path_length) {
    uint64_t ext = *(uint64_t *)(path + path_length - 5 /* .html */);

    if ((__builtin_bswap64(ext) & MASK_5) == HTML_EXTENSION) {
        return true;
    }

    return false;
}

char *peek(Lexer *lexer) { return &lexer->file_content.data[lexer->cursor]; }
void advance_cursor(Lexer *lexer) { lexer->cursor += 1; }

TokenKind get_token_kind(String identifier) {
    if (strncmp(COMPONENT_DEFINITION_IDENTIFIER, identifier.data, identifier.length) == 0 && identifier.length == (array_count(COMPONENT_DEFINITION_IDENTIFIER) - 1)) {
        return TOKEN_COMPONENT_DEFINITION;
    } else if (strncmp(COMPONENT_IMPORT_IDENTIFIER, identifier.data, identifier.length) == 0 && identifier.length == (array_count(COMPONENT_IMPORT_IDENTIFIER) - 1)) {
        return TOKEN_COMPONENT_IMPORT;
    } else if (strncmp(SLOT_IDENTIFIER, identifier.data, identifier.length) == 0 && identifier.length == (array_count(SLOT_IDENTIFIER) - 1)) {
        return TOKEN_SLOT;
    } else if (strncmp(FILL_IDENTIFIER, identifier.data, identifier.length) == 0 && identifier.length == (array_count(FILL_IDENTIFIER) - 1)) {
        return TOKEN_FILL;
    } else if (strncmp(VAL_IDENTIFIER, identifier.data, identifier.length) == 0 && identifier.length == (array_count(VAL_IDENTIFIER) - 1)) {
        return TOKEN_VAL;
    } else if (strncmp(FOR_IDENTIFIER, identifier.data, identifier.length) == 0 && identifier.length == (array_count(FOR_IDENTIFIER) - 1)) {
        return TOKEN_FOR;
    } else {
        return TOKEN_INVALID;
    }
}

Token get_next_token(Lexer *lexer) {
    Token token = {0};
    while (lexer->cursor < lexer->file_content.length) {
        char *c = peek(lexer);

        if (*c == 'x' && *(c + 1) == '-') {
            if (*(c - 1) == '<') { /* opening tag */
                char *p = NULL;

                token.tag.markup.data = c - 1;

                p = c;
                String identifier = {0};
                identifier.data = p;
                while (*p != '\0' && !isspace(*p) && *p != '>') {
                    identifier.length += 1;
                    p += 1;
                }

                /* check if tag is self closing */
                p = identifier.data;
                while (*p != '\0') {
                    if (*p == '>') {
                        token.tag.type = TAG_OPENING;
                        token.tag.markup.length = p + 1 - token.tag.markup.data;
                        break;
                    }

                    if (*p == '/' && *(p + 1) == '>') {
                        token.tag.type = TAG_SELFCLOSING;
                        token.tag.markup.length = p + 2 - token.tag.markup.data;
                        break;
                    }

                    p++;
                }

                token.identifier = identifier;
                token.kind = get_token_kind(token.identifier);

                lexer->cursor = (identifier.data + identifier.length) - lexer->file_content.data;

                return token;
            }

            if (*(c - 1) == '/' && *(c - 2) == '<') { /* closing tag */
                char *p = NULL;

                token.tag.markup.data = c - 2;

                p = c;
                String identifier = {0};
                identifier.data = p;

                while (*p != '\0') {
                    if (*p == '>') {
                        token.tag.markup.length = p + 1 - token.tag.markup.data;
                        break;
                    }

                    /* NOTE: Think of a better way to do this */
                    if (!isspace(*p)) {
                        identifier.length += 1;
                    }

                    p += 1;
                }

                token.tag.type = TAG_CLOSING;
                token.kind = get_token_kind(identifier);

                lexer->cursor = (identifier.data + identifier.length) - lexer->file_content.data;

                return token;
            }
        }

        if (*c == '%') {
            char *p = NULL;

            p = c + 1;
            if (*p != '%') {
                while (*p != '\0') {
                    if (isspace(*p)) {
                        break;
                    }

                    if (*p == '%') {
                        String identifier = {0};
                        identifier.data = c + 1;
                        identifier.length = p - identifier.data;

                        token.identifier = identifier;
                        token.kind = TOKEN_PLACEHOLDER;

                        lexer->cursor = (identifier.data + identifier.length + 1) - lexer->file_content.data;

                        return token;
                    }

                    p += 1;
                }
            }
        }

        if (*c == '\n') {
            lexer->line_number += 1;
        }

        advance_cursor(lexer);
    }

    return token;
}

char *find_eol(char *p, char *text_end) {
    char *eol = p;
    while (eol < text_end) {
        if (*eol == '\n') {
            return eol;
        }
        eol += 1;
    }

    return text_end;
}

char *find_bol(char *p, char *text_start) {
    char *bol = p;
    while (bol > text_start) {
        if (*bol == '\n') {
            bol += 1;
            return bol;
        }
        bol -= 1;
    }

    return text_start;
}

Node *create_node(Memory *memory, Token token, String file_path, uint32_t line_number, AttributeArray attribute_array, Node *parent) {
    Node *new_node = memory_alloc(memory, sizeof(Node));
    if (new_node == NULL) {
        printf("Memory allocation failed\n");
        return NULL;
    }
    new_node->token = token;
    new_node->line_number = line_number;
    new_node->attribute_array = attribute_array;
    new_node->parent = parent;
    return new_node;
}

ASTNode *create_ast_node(Memory *memory, Token token, uint32_t line_number, AttributeArray attribute_array, String block) {
    ASTNode *new_node = memory_alloc(memory, sizeof(ASTNode));
    if (new_node == NULL) {
        printf("Memory allocation failed\n");
        return NULL;
    }
    new_node->token = token;
    new_node->line_number = line_number;
    new_node->attribute_array = attribute_array;
    new_node->block = block;
    new_node->first_child = NULL;
    new_node->next_sibling = NULL;
    return new_node;
}

Node *find_parent_tree_root(Node *node) {
    while (node->parent != NULL) {
        node = node->parent;
    }
    return node;
}

ASTNode *convert_to_ast(Memory *memory, Node **nodes, uint32_t count) {
    ASTNode **ast_nodes = memory_alloc(memory, count * sizeof(ASTNode *));

    uint32_t i;

    /* Create corresponding ChildSiblingNodes */
    for (i = 0; i < count; i++) {
        ast_nodes[i] = create_ast_node(memory, nodes[i]->token, nodes[i]->line_number, nodes[i]->attribute_array, nodes[i]->block);
    }

    /* Arrange first_child and next_sibling pointers */
    for (i = 0; i < count; i++) {
        Node *_parent = nodes[i]->parent;
        if (_parent != NULL) {
            uint32_t parent_index;
            for (parent_index = 0; parent_index < count; parent_index++) {
                if (nodes[parent_index] == _parent)
                    break;
            }

            ASTNode *parent = ast_nodes[parent_index];

            /* Fill node as a child of its parent */
            if (parent->first_child == NULL) {
                parent->first_child = ast_nodes[i];
            } else {
                ASTNode *temp = parent->first_child;
                while (temp->next_sibling != NULL) {
                    temp = temp->next_sibling;
                }
                temp->next_sibling = ast_nodes[i];
            }
        }
    }

    /* Find the root of the Child-Sibling tree */
    Node *root = find_parent_tree_root(nodes[0]); /* Any node can be used to find the root */
    ASTNode *ast_root = NULL;

    for (i = 0; i < count; i++) {
        if (nodes[i] == root) {
            ast_root = ast_nodes[i];
            break;
        }
    }

    return ast_root;
}

const char *token_kind_to_string(TokenKind kind) {
    switch (kind) {
        case TOKEN_COMPONENT_DEFINITION:
            return "COMPONENT_DEFINITION";
        case TOKEN_COMPONENT_IMPORT:
            return "COMPONENT_IMPORT";
        case TOKEN_SLOT:
            return "SLOT";
        case TOKEN_FILL:
            return "FILL";
        case TOKEN_VAL:
            return "VAL";
        case TOKEN_FOR:
            return "FOR";
        default:
            return "INVALID";
    }
}

void print_ast_tree(ASTNode *node, uint32_t depth) {
    uint32_t i;
    
    if (node == NULL) {
        return;
    }

    /* Print node with indentation */
    for (i = 0; i < depth; i++) {
        printf("    ");
    }

    printf("%s ", token_kind_to_string(node->token.kind));
    for (i = 0; i < node->attribute_array.count; i++) {
        printf("%.*s=\"%.*s\" ", (int)(node->attribute_array.attributes[i].name.length), node->attribute_array.attributes[i].name.data, (int)(node->attribute_array.attributes[i].value.length), node->attribute_array.attributes[i].value.data);
    }
    printf("\n");

    /* Print first child and siblings */
    print_ast_tree(node->first_child, depth + 1);
    print_ast_tree(node->next_sibling, depth);
}

void print_tag_error(String content, char error[], String file_path, String identifier, uint32_t line_number) {
    char *content_end = content.data + content.length;

    printf("%s:\n", error);
    printf("    file: %.*s\n", (int)file_path.length, file_path.data);
    printf("    line: %d\n", line_number);
    printf("\n");
    char *bol = find_bol(identifier.data, content.data);
    if (line_number > 1) {
        char *prev_bol = find_bol(bol - 2, content.data);
        uint32_t prev_line_length = (bol - 1) - prev_bol;
        printf("    %d|  %.*s\n", line_number - 1, prev_line_length, prev_bol);
    }
    char *eol = find_eol(bol, content_end);

    printf("    %d|  ", line_number);
    char *p = bol;
    while (p < eol) {
        if (p >= identifier.data && p < identifier.data + identifier.length) {
            /* ANSI escape codes for highlighting (red background) */
            printf("\033[41m%c\033[0m", *p);
        } else {
            printf("%c", *p);
        }
        p += 1;
    }

    printf("\n");

    if (eol < content_end) {
        char *next_bol = eol + 1;
        char *next_eol = find_eol(next_bol, content.data + content.length);
        uint32_t next_line_length = next_eol - next_bol;
        printf("    %d|  %.*s\n", line_number + 1, next_line_length, next_bol);
    }

    printf("\n");
}

void print_tag_attr_error(String content, ASTNode *node, Attribute attribute) {
    char *content_end = content.data + content.length;

    char *bol = find_bol(node->token.identifier.data, content.data);
    if (node->line_number > 1) {
        char *prev_bol = find_bol(bol - 2, content.data);
        uint32_t prev_line_length = (bol - 1) - prev_bol;
        printf("    %d|  %.*s\n", node->line_number - 1, prev_line_length, prev_bol);
    }
    char *eol = find_eol(bol, content_end);

    printf("    %d|  ", node->line_number);
    char *p = bol;
    while (p < eol) {
        if (p >= attribute.name.data && p < attribute.value.data + attribute.value.length + 1) {
            printf("\033[41m%c\033[0m", *p);
        } else {
            printf("%c", *p);
        }
        p += 1;
    }

    printf("\n");

    if (eol < content_end) {
        char *next_bol = eol + 1;
        char *next_eol = find_eol(next_bol, content.data + content.length);
        uint32_t next_line_length = next_eol - next_bol;
        printf("    %d|  %.*s\n", node->line_number + 1, next_line_length, next_bol);
    }

    printf("\n");
}

void validate_ast_tag_hierarchy(String content, String file_path, ASTNode *node, uint32_t *import_count, uint32_t *slot_count, uint32_t *error_count) {
    switch (node->token.kind) {
        case TOKEN_COMPONENT_DEFINITION: {
            ASTNode *child = node->first_child;
            while (child) {
                if (child->token.kind == TOKEN_FILL) {
                    char error[] = "x-fill can not be direct child of x-def, it can only be direct child of x-use";

                    print_tag_error(content, error, file_path, child->token.identifier, child->line_number);
                    *error_count += 1;
                }

                child = child->next_sibling;
            }

            break;
        }
        case TOKEN_COMPONENT_IMPORT: {
            ASTNode *child = node->first_child;
            while (child) {
                if (child->token.kind != TOKEN_FILL) {
                    char error[] = "x-use can only have direct child x-fill";

                    print_tag_error(content, error, file_path, child->token.identifier, child->line_number);
                    *error_count += 1;
                }

                child = child->next_sibling;
            }

            *import_count += 1;

            break;
        }
        case TOKEN_SLOT: {
            ASTNode *child = node->first_child;
            if (child) {
                char error[] = "x-slot can't have any children";

                print_tag_error(content, error, file_path, node->token.identifier, node->line_number);
                *error_count += 1;
            }

            *slot_count += 1;

            break;
        }
        case TOKEN_FILL: {
            ASTNode *child = node->first_child;
            while (child) {
                if (child->token.kind == TOKEN_FILL) {
                    char error[] = "x-fill can not be direct child of x-fill, it can only be direct child of x-use";

                    print_tag_error(content, error, file_path, child->token.identifier, child->line_number);
                    *error_count += 1;
                }

                child = child->next_sibling;
            }

            break;
        }
        case TOKEN_VAL: {
            ASTNode *child = node->first_child;
            if (child) {
                char error[] = "x-val can't have any children";

                print_tag_error(content, error, file_path, node->token.identifier, node->line_number);
                *error_count += 1;
            }

            break;
        }
        case TOKEN_FOR: {
            ASTNode *child = node->first_child;
            while (child) {
                if (child->token.kind == TOKEN_FILL) {
                    char error[] = "x-fill can not be direct child of x-for, it can only be direct child of x-use";

                    print_tag_error(content, error, file_path, child->token.identifier, child->line_number);
                    *error_count += 1;
                }

                child = child->next_sibling;
            }

            break;
        }
        default: {
            assert(0);
            break;
        }
    }

    if (node->next_sibling) {
        validate_ast_tag_hierarchy(content, file_path, node->next_sibling, import_count, slot_count, error_count);
    }

    if (node->first_child) {
        validate_ast_tag_hierarchy(content, file_path, node->first_child, import_count, slot_count, error_count);
    }
}

void ast_to_soa(Memory *memory, ASTNode *node, ComponentSOA *components_soa) {
    uint32_t current_component_index = components_soa->count;

    if (node->token.kind == TOKEN_SLOT) {
        SlotSOA *slots_soa = components_soa->slots_soa[current_component_index];

        memcpy(slots_soa->names[slots_soa->count], node->attribute_array.attributes[node->attribute_array.name_attr_index].value.data, node->attribute_array.attributes[node->attribute_array.name_attr_index].value.length);
        slots_soa->ast_nodes[slots_soa->count] = node;

        slots_soa->count += 1;
    }

    if (node->token.kind == TOKEN_COMPONENT_IMPORT) {
        ImportSOA *imports_soa = components_soa->imports_soa[current_component_index];

        memcpy(imports_soa->names[imports_soa->count], node->attribute_array.attributes[node->attribute_array.name_attr_index].value.data, node->attribute_array.attributes[node->attribute_array.name_attr_index].value.length);
        imports_soa->ast_nodes[imports_soa->count] = node;

        uint32_t child_count = 0;
        ASTNode *child = node->first_child;
        while (child) {
            child_count += 1;
            child = child->next_sibling;
        }

        FillSOA *fills_soa = &(imports_soa->fills_soa[imports_soa->count]);

        if (child_count) {
            fills_soa->names = memory_alloc(memory, sizeof(Name) * child_count);
            fills_soa->ast_nodes = memory_alloc(memory, sizeof(ASTNode *) * child_count);

            child = node->first_child;
            while (child) {

                memcpy(fills_soa->names[fills_soa->count], child->attribute_array.attributes[child->attribute_array.name_attr_index].value.data, child->attribute_array.attributes[child->attribute_array.name_attr_index].value.length);
                fills_soa->ast_nodes[fills_soa->count] = child;
                fills_soa->count += 1;

                child = child->next_sibling;
            }
        }

        imports_soa->count += 1;
    }

    if (node->next_sibling) {
        ast_to_soa(memory, node->next_sibling, components_soa);
    }

    if (node->first_child) {
        ast_to_soa(memory, node->first_child, components_soa);
    }
}

void print_components_soa(ComponentSOA *components_soa, uint32_t i) {
    printf("Component %s\n", components_soa->names[i]);

    if (components_soa->slots_soa[i]) {
        SlotSOA *slots_soa = components_soa->slots_soa[i];
        printf("    has %d slots: ", slots_soa->count);
        uint32_t j;
        for (j = 0; j < slots_soa->count; j++) {
            printf("%s", slots_soa->names[j]);

            if ((j + 1) != slots_soa->count) {
                printf(", ");
            }
        }
        printf("\n");
    }

    if (components_soa->imports_soa[i]) {
        ImportSOA *imports_soa = components_soa->imports_soa[i];
        printf("    has %d imports: ", imports_soa->count);
        uint32_t j;
        for (j = 0; j < imports_soa->count; j++) {
            printf("%s", imports_soa->names[j]);
            if (imports_soa->fills_soa[j].count) {
                FillSOA *fills_soa = &(imports_soa->fills_soa[j]);
                printf("(");
                uint32_t k;
                for (k = 0; k < fills_soa->count; k++) {
                    printf("%s", fills_soa->names[k]);

                    if ((k + 1) != fills_soa->count) {
                        printf(", ");
                    }
                }
                printf(")");
            }

            if ((j + 1) != imports_soa->count) {
                printf(", ");
            }
        }
        printf("\n");
    }

    printf("\n");
}

void print_no_attribute_allowed_error(String content, char *error, String file_path, String identifier, uint32_t line_number, Attribute *attribute) {
    char *content_end = content.data + content.length;
    printf("Invalid attribute for %.*s tag, only x-use tag can have attributes other than the name attribute:\n", (int)identifier.length, identifier.data);
    printf("    file: %.*s\n", (int)file_path.length, file_path.data);
    printf("    line: %d\n", line_number);
    printf("\n");

    /* print here */

    /*
     uint32_t i;
     for (i = 0; i < attributes.count; i++) {
         Attribute attribute = attributes.attribute[i];
         if (strncmp("name", attribute.name.data, attribute.name.length) == 0 && strlen("name") == attributes.attribute[i].name.length) {
             continue;
         }

          TODO
          print_tag_attr_error(content, node, attribute);

          *error_count += 1;
     }
    */
}

Lexer lexer_init(char *file_path, size_t file_path_length, char *content, size_t content_length) {
    Lexer lexer = {0};
    lexer.file_path.data = file_path;
    lexer.file_path.length = file_path_length;
    lexer.file_content.data = content;
    lexer.file_content.length = content_length;
    lexer.line_number = 1;

    return lexer;
}

int ark_validate_components(char **array_of_filepaths, char **file_content_array, uint32_t array_length) {
    uint32_t i;

    Memory *memory = reserve_memory(PAGE_SIZE * 100);

    size_t mask_size = array_length / U32_BITS;
    mask_size = mask_size < 1 ? 1 : mask_size; /* ensure minimum */

    uint32_t *is_html_file_mask = memory_alloc(memory, mask_size * sizeof(uint32_t));

    for (i = 0; i < array_length; i++) {
        if (is_html_path(array_of_filepaths[i], strlen(array_of_filepaths[i]))) {
            TURN_ON_U32_BIT(is_html_file_mask, i);
        }
    }

    uint32_t components_count = 0;
    for (i = 0; i < array_length; i++) {
        if (IS_U32_BIT_OFF(is_html_file_mask, i)) {
            continue;
        }

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

            char *file_path = array_of_filepaths[i];
            char *file_content = file_content_array[i];
    
            Lexer lexer = lexer_init(file_path, strlen(file_path), file_content, strlen(file_content));
    
            String opening_tag = {0};
            uint32_t opening_tag_line_number = 0; /* Used for error reporting */
    
            while (lexer.cursor < lexer.file_content.length) {
                char *c = peek(&lexer);
                if (strncmp("<x-def ", c, strlen("<x-def ")) == 0) {
                    if (opening_tag.data) {
                        char error[] = "x-def can't ever be a child tag, it must always be placed at the top level. "
                                       "If you think you already placed x-def at the top level, check that you did't "
                                       "leave open a x-def earlier in the file";
                        print_tag_error(lexer.file_content, error, lexer.file_path, opening_tag, opening_tag_line_number);
    
                        return -1;
                    }
    
                    advance_cursor(&lexer); /* skip '<' */
                    opening_tag.data = peek(&lexer);
                    opening_tag.length = strlen("x-def");
                    opening_tag_line_number = lexer.line_number;
    
                    components_count += 1;
                } else if (strncmp("</x-def>", c, strlen("</x-def>")) == 0) {
                    memset(&opening_tag, 0, sizeof(opening_tag));
                } else if (*c == '\n') {
                    lexer.line_number += 1;
                }
    
                advance_cursor(&lexer);
            }
        }
    }
    
    /*
     Used to store the amount of %placeholders% each component 
     contains, this info will later be needed for allocations.
     */
    uint32_t *placeholder_amounts = memory_alloc(memory, sizeof(uint32_t) * components_count); 

    uint32_t component_index = 0; 
    boolean inside_component = false;

    /*
     Used to store the max amount of xtags: 
     opening (<x-), closing (</x-), and self-closing (<x-)
     This info will later be used to allocate a stack 
     that assists in creating the component AST.
     */
    uint32_t tag_stack_max = 0; 
    uint32_t tag_stack_count = 0;

    for (i = 0; i < array_length; i++) {
        if (IS_U32_BIT_OFF(is_html_file_mask, i)) {
            continue;
        }

        char *file_path = array_of_filepaths[i];
        char *file_content = file_content_array[i];

        Lexer lexer = lexer_init(file_path, strlen(file_path), file_content, strlen(file_content));

        while (lexer.cursor < lexer.file_content.length) {
            char *c = peek(&lexer);
            if (strncmp("<x-def ", c, strlen("<x-def ")) == 0) {
                inside_component = true;
            } else if (strncmp("</x-def>", c, strlen("</x-def>")) == 0) {
                component_index += 1;
                inside_component = false;

                if (tag_stack_count > tag_stack_max) {
                    tag_stack_max = tag_stack_count;
                }

                tag_stack_count = 0;
            }
            
            if (inside_component) {
                /*
                 Check that we are not immediately closing the placeholder with no name, like '%%',
                 in which case it wouldn't be considered a valid placeholder. 
                 
                 ATTENTIONS: A placeholder may be placed anywhere within the component definition.
                */
                if (*c == '%' && *(c + 1) != '%') {
                    uint32_t placeholder_name_length = 0;

                    char *end = file_content + strlen(file_content);

                    char *p = c + 1;
                    while (p != end) {
                        if (isspace(*p)) {
                            /*
                             It wasn't a placeholder after all, because the placeholder format 
                             should be '%some_name%' without any whitespace in between.
                            */
                            break;
                        }

                        if (*p == '%') {
                            if (placeholder_name_length > ARK_MAX_TEMPLATE_NAME_LENGTH) {
                                /* TODO: Print usefull error message */
                                return -1;
                            }

                            placeholder_amounts[component_index] += 1;
                            break;
                        }

                        placeholder_name_length += 1;
                        p += 1;
                    }
                }

                /*
                 This simple check could give false positives, but it doesn't matter much,
                 as it would just make us allocate a few extra slots in the stack that we 
                 wouldn't use.
                */
                if (strncmp("<x-", c, strlen("<x-")) == 0 || strncmp("</x-", c, strlen("</x-")) == 0) {
                    tag_stack_count += 1;
                }
            }

            if (*c == '\n') {
                lexer.line_number += 1;
            }

            advance_cursor(&lexer);
        }
    }

    ComponentSOA components_soa = {0};
    components_soa.names = memory_alloc(memory, sizeof(Name) * components_count);
    components_soa.components = memory_alloc(memory, sizeof(String) * components_count);
    components_soa.ast_nodes = memory_alloc(memory, sizeof(ASTNode *) * components_count);
    components_soa.placeholders_soa = memory_alloc(memory, sizeof(PlaceholderSOA *) * components_count);
    components_soa.imports_soa = memory_alloc(memory, sizeof(ImportSOA *) * components_count);
    components_soa.slots_soa = memory_alloc(memory, sizeof(SlotSOA *) * components_count);
    components_soa.file_paths = memory_alloc(memory, sizeof(String) * components_count);
    components_soa.contents = memory_alloc(memory, sizeof(String) * components_count);

    for (i = 0; i < components_count; i++) {
        components_soa.placeholders_soa[i] = memory_alloc(memory, sizeof(PlaceholderSOA));
        components_soa.placeholders_soa[i]->names = memory_alloc(memory, sizeof(Name) * placeholder_amounts[i]);
    }

    TagStack tag_stack = {0}; /* Used to keep track of open xtags, ensuring proper closing order. */
    tag_stack.data = memory_alloc(memory, sizeof(TagStackElement) * tag_stack_max);

    NodeArray node_array = {0}; /* used to temporarily hold nodes while parsing component template and later used to construct the component AST. */
    node_array.data = memory_alloc(memory, sizeof(Node *) * tag_stack_max);

    for (i = 0; i < array_length; i++) {
        if (IS_U32_BIT_OFF(is_html_file_mask, i)) {
            continue;
        }

        char *file_path = array_of_filepaths[i];
        char *file_content = file_content_array[i];

        Lexer lexer = lexer_init(file_path, strlen(file_path), file_content, strlen(file_content));

        Token token = get_next_token(&lexer);
        while (token.kind != TOKEN_EOF) {
            if (token.kind == TOKEN_INVALID) {
                /* TODO: Print usefull error message */
                return -1;
            }

            if (token.kind == TOKEN_PLACEHOLDER) {
                uint32_t count = components_soa.placeholders_soa[components_soa.count]->count;
                memcpy(components_soa.placeholders_soa[components_soa.count]->names[count], token.identifier.data, token.identifier.length);
                components_soa.placeholders_soa[components_soa.count]->count += 1;

                goto proceed_to_next_token;
            }

            AttributeArray attribute_array = {0};
            
            {   /* Attr Parser */
                size_t curs = lexer.cursor;
                uint8_t c = 0; /** ASCII character */
                uint8_t tk = 0;
                size_t str_start = 0;
                uint8_t quotation = 0;
                while (true) {
                    String key = {0};
                    String value = {0};
    
                    for (;;) {
                        c = lexer.file_content.data[curs] & 255;
                        switch (tk) {
                            case HTML_ATTR_START: {
                                for (;;) {
                                    if (c == '>') {
                                        tk = HTML_ATTR_END;
                                        break;
                                    } else if (html_attr_key_token[c]) {
                                        tk = HTML_ATTR_START_KEY;
                                        break;
                                    } else if (c == ' ') {
                                        curs++;
                                        c = lexer.file_content.data[curs] & 255;
                                    } else {
                                        assert(0);
                                        return -1;
                                    }
                                }
                                break;
                            }
                            case HTML_ATTR_START_KEY: {
                                for (;;) {
                                    if (html_attr_key_token[c]) {
                                        key.data = &lexer.file_content.data[curs];
                                        tk = HTML_ATTR_KEY;
                                        str_start = curs;
                                        break;
                                    } else if (c == ' ') {
                                        curs++;
                                        c = lexer.file_content.data[curs] & 255;
                                    } else {
                                        assert(0);
                                        return -1;
                                    }
                                }
                                break;
                            }
                            case HTML_ATTR_KEY: {
                                for (;;) {
                                    if (c == ' ' || c == '=') {
                                        key.length = curs - str_start;
                                        tk = HTML_ATTR_EQUALS;
                                        break;
                                    } else if (html_attr_key_token[c]) {
                                        curs++;
                                        c = lexer.file_content.data[curs] & 255;
                                    }   else {
                                        assert(0);
                                        return -1;
                                    }
                                }
                                break;
                            }
                            case HTML_ATTR_EQUALS: {
                                for (;;) {
                                    if (c == '=') {
                                        curs++; /* skip '=' */
                                        tk = HTML_ATTR_START_VALUE;
                                        break;
                                    } else if (c == ' ') {
                                        curs++;
                                        c = lexer.file_content.data[curs] & 255;
                                    } else {
                                        assert(0);
                                        return -1;
                                    }
                                }
                                break;
                            }
                            case HTML_ATTR_START_VALUE: {
                                for (;;) {
                                    if (c == '"' ||  c == '\'') {
                                        quotation = c;
                                        curs++; /* pass quotation */
                                        value.data = &lexer.file_content.data[curs];
                                        tk = HTML_ATTR_VALUE;
                                        str_start = curs;
                                        break;
                                    } else if (c == ' ') {
                                        curs++;
                                        c = lexer.file_content.data[curs] & 255;
                                    }   else {
                                        assert(0);
                                        return -1;
                                    }
                                }
                                break;
                            }
                            case HTML_ATTR_VALUE: {
                                char prev = lexer.file_content.data[curs - 1] & 255;
                                for (;;) {
                                    if (c == quotation && prev != '\\') {
                                        value.length = curs - str_start;
                                        curs++;
                                        tk = HTML_ATTR_END;
                                        break;
                                    } else if (c & 255) {
                                        prev = c;
                                        curs++;
                                        c = lexer.file_content.data[curs] & 255;
                                    }   else {
                                        assert(0);
                                        return -1;
                                    }
                                }
                                break;
                            }
                            case HTML_ATTR_END: {
                                goto exit_attr_parsing;
                            }
                        }
                    }
                    
                exit_attr_parsing:;
    
                    if (key.data == NULL && value.data == NULL) {
                        break;
                    }
    
                    Attribute *attribute = memory_alloc(memory, sizeof(Attribute));
                    attribute->name = key;
                    attribute->value = value;
    
                    if (attribute_array.count == 0) {
                        attribute_array.attributes = attribute;
                    }
    
                    attribute_array.count += 1;
                }
            }

            boolean has_name_attr = false;
            uint32_t name_attr_index = 0;

            uint32_t j;
            for (j = 0; j < attribute_array.count; j++) {
                if (strncmp("name", attribute_array.attributes[j].name.data, attribute_array.attributes[j].name.length) == 0) {
                    has_name_attr = true;
                    name_attr_index = j;

                    assert(attribute_array.attributes[j].name.length < ARK_MAX_TEMPLATE_NAME_LENGTH);
                }
            }

            switch (token.kind) {
                case TOKEN_COMPONENT_DEFINITION: {
                    switch (token.tag.type) {
                        case TAG_SELFCLOSING: {
                            char error[] = "x-def can NOT be self-closing";
                            print_tag_error(lexer.file_content, error, lexer.file_path, token.identifier, lexer.line_number);

                            assert(0); /* TODO: remove */
                            return -1;
                        }
                        case TAG_OPENING: {
                            if (!has_name_attr) {
                                char error[] = "All tags must contain a name attribute";
                                print_tag_error(lexer.file_content, error, lexer.file_path, token.identifier, lexer.line_number);

                                assert(0); /* TODO: remove */
                                return -1;
                            }

                            for (j = 0; j < attribute_array.count; j++) {
                                if (j == name_attr_index)
                                    continue;

                                char error[] = "Invalid attribute for x-def tag";
                                print_no_attribute_allowed_error(lexer.file_content, error, lexer.file_path, token.identifier, lexer.line_number, &attribute_array.attributes[j]);

                                assert(0); /* TODO: remove */
                                return -1;
                            }

                            components_soa.components[components_soa.count].data = token.tag.markup.data + token.tag.markup.length;

                            Node *root = create_node(memory, token, lexer.file_path, lexer.line_number, attribute_array, NULL);

                            tag_stack.data[tag_stack.count].kind = token.kind;
                            tag_stack.data[tag_stack.count].node = root;
                            tag_stack.count += 1;

                            node_array.data[node_array.count] = root;
                            node_array.count += 1;

                            goto proceed_to_next_token;
                        }
                        case TAG_CLOSING: {
                            if (attribute_array.count) {
                                print_tag_error(lexer.file_content, "Closing tag is not allowed to have attributes", lexer.file_path, token.identifier, lexer.line_number);

                                assert(0); /* TODO: remove */
                                return -1;
                            }

                            components_soa.components[components_soa.count].length = token.tag.markup.data - components_soa.components[components_soa.count].data;

                            ASTNode *ast_root = convert_to_ast(memory, node_array.data, node_array.count);
                            /* print_ast_tree(ast_root, 0); */

                            uint32_t error_count = 0;

                            uint32_t import_count = 0;
                            uint32_t slot_count = 0;

                            validate_ast_tag_hierarchy(lexer.file_content, lexer.file_path, ast_root, &import_count, &slot_count, &error_count);

                            if (error_count) {
                                printf("%d errors.\n", error_count);

                                assert(0); /* TODO: remove */
                                return -1;
                            }

                            memcpy(components_soa.names[components_soa.count], ast_root->attribute_array.attributes[ast_root->attribute_array.name_attr_index].value.data, ast_root->attribute_array.attributes[ast_root->attribute_array.name_attr_index].value.length);
                            components_soa.file_paths[components_soa.count] = lexer.file_path;
                            components_soa.contents[components_soa.count] = lexer.file_content;

                            components_soa.ast_nodes[components_soa.count] = ast_root;

                            if (import_count) {
                                components_soa.imports_soa[components_soa.count] = memory_alloc(memory, sizeof(ImportSOA));
                                components_soa.imports_soa[components_soa.count]->names = memory_alloc(memory, sizeof(Name) * import_count);
                                components_soa.imports_soa[components_soa.count]->ast_nodes = memory_alloc(memory, sizeof(ASTNode *) * import_count);
                                components_soa.imports_soa[components_soa.count]->fills_soa = memory_alloc(memory, sizeof(FillSOA) * import_count);
                            }

                            if (slot_count) {
                                components_soa.slots_soa[components_soa.count] = memory_alloc(memory, sizeof(ImportSOA));
                                components_soa.slots_soa[components_soa.count]->names = memory_alloc(memory, sizeof(Name) * slot_count);
                                components_soa.slots_soa[components_soa.count]->ast_nodes = memory_alloc(memory, sizeof(ASTNode *) * slot_count);
                            }

                            ast_to_soa(memory, ast_root, &components_soa);

                            components_soa.count += 1;

                            tag_stack.count = 0;
                            memset(tag_stack.data, 0, sizeof(TagStackElement) * tag_stack_max);

                            node_array.count = 0;
                            memset(node_array.data, 0, sizeof(Node *) * tag_stack_max);

                            goto proceed_to_next_token;
                        }
                        default: {
                            assert(0);
                            break;
                        }
                    }

                    assert(0);
                    break;
                }
                case TOKEN_COMPONENT_IMPORT: {
                    switch (token.tag.type) {
                        case TAG_SELFCLOSING: {
                            if (!has_name_attr) {
                                char error[] = "All tags must contain a name attribute";
                                print_tag_error(lexer.file_content, error, lexer.file_path, token.identifier, lexer.line_number);

                                assert(0); /* TODO: remove */
                                return -1;
                            }

                            Node *parent = tag_stack.data[tag_stack.count - 1].node;
                            Node *node = create_node(memory, token, lexer.file_path, lexer.line_number, attribute_array, parent);

                            node_array.data[node_array.count] = node;
                            node_array.count += 1;

                            goto proceed_to_next_token;
                        }
                        case TAG_OPENING: {
                            if (!has_name_attr) {
                                char error[] = "All tags must contain a name attribute";
                                print_tag_error(lexer.file_content, error, lexer.file_path, token.identifier, lexer.line_number);

                                assert(0); /* TODO: remove */
                                return -1;
                            }

                            Node *parent = tag_stack.data[tag_stack.count - 1].node;
                            Node *node = create_node(memory, token, lexer.file_path, lexer.line_number, attribute_array, parent);

                            tag_stack.data[tag_stack.count].kind = token.kind;
                            tag_stack.data[tag_stack.count].node = node;
                            tag_stack.count += 1;

                            node_array.data[node_array.count] = node;
                            node_array.count += 1;

                            goto proceed_to_next_token;
                        }
                        case TAG_CLOSING: {
                            if (attribute_array.count) {
                                print_tag_error(lexer.file_content, "Closing tag is not allowed to have attributes", lexer.file_path, token.identifier, lexer.line_number);

                                assert(0); /* TODO: remove */
                                return -1;
                            }

                            TagStackElement last_tag_opened = tag_stack.data[tag_stack.count - 1];
                            if (token.kind != last_tag_opened.kind) {

                                /* print something like: tag last_tag_opened.node is child of token.kind and needs to be closed before closing the parent token.kind */

                                char error[] = "NOT SURE WHAT GOES HERE";
                                print_tag_error(lexer.file_content, error, lexer.file_path, last_tag_opened.node->token.identifier, last_tag_opened.node->line_number);

                                assert(0); /* TODO: remove */
                                return -1;
                            }

                            last_tag_opened.node->block.data = last_tag_opened.node->token.tag.markup.data;
                            last_tag_opened.node->block.length = (token.tag.markup.data + token.tag.markup.length) - last_tag_opened.node->block.data;
                            
                            tag_stack.data[tag_stack.count - 1].kind = 0;
                            tag_stack.data[tag_stack.count - 1].node = NULL;
                            tag_stack.count -= 1;

                            goto proceed_to_next_token;
                        }
                        default: {
                            assert(0);
                            break;
                        }
                    }

                    assert(0);
                    break;
                }
                case TOKEN_SLOT: {
                    switch (token.tag.type) {
                        case TAG_SELFCLOSING: {
                            if (!has_name_attr) {
                                char error[] = "All tags must contain a name attribute";
                                print_tag_error(lexer.file_content, error, lexer.file_path, token.identifier, lexer.line_number);

                                assert(0); /* TODO: remove */
                                return -1;
                            }

                            for (j = 0; j < attribute_array.count; j++) {
                                if (j == name_attr_index)
                                    continue;

                                char error[] = "Invalid attribute for x-slot tag";
                                print_no_attribute_allowed_error(lexer.file_content, error, lexer.file_path, token.identifier, lexer.line_number, &attribute_array.attributes[j]);

                                assert(0); /* TODO: remove */
                                return -1;
                            }

                            Node *parent = tag_stack.data[tag_stack.count - 1].node;
                            Node *node = create_node(memory, token, lexer.file_path, lexer.line_number, attribute_array, parent);

                            node_array.data[node_array.count] = node;
                            node_array.count += 1;

                            goto proceed_to_next_token;
                        }
                        case TAG_OPENING: {
                            char error[] = "x-slot must be self-closing";
                            print_tag_error(lexer.file_content, error, lexer.file_path, token.identifier, lexer.line_number);

                            assert(0); /* TODO: remove */
                            return -1;
                        }
                        case TAG_CLOSING: {
                            char error[] = "x-slot must be self-closing";
                            print_tag_error(lexer.file_content, error, lexer.file_path, token.identifier, lexer.line_number);

                            assert(0); /* TODO: remove */
                            return -1;
                        }
                        default: {
                            assert(0);
                            break;
                        }
                    }

                    assert(0);
                    break;
                }
                case TOKEN_FILL: {
                    switch (token.tag.type) {
                        case TAG_SELFCLOSING: {
                            char error[] = "x-fill can NOT be self-closing";
                            print_tag_error(lexer.file_content, error, lexer.file_path, token.identifier, lexer.line_number);

                            assert(0); /* TODO: remove */
                            return -1;
                        }
                        case TAG_OPENING: {
                            if (!has_name_attr) {
                                char error[] = "All tags must contain a name attribute";
                                print_tag_error(lexer.file_content, error, lexer.file_path, token.identifier, lexer.line_number);

                                assert(0); /* TODO: remove */
                                return -1;
                            }

                            for (j = 0; j < attribute_array.count; j++) {
                                if (j == name_attr_index)
                                    continue;

                                char error[] = "Invalid attribute for x-fill tag";
                                print_no_attribute_allowed_error(lexer.file_content, error, lexer.file_path, token.identifier, lexer.line_number, &attribute_array.attributes[j]);

                                assert(0); /* TODO: remove */
                                return -1;
                            }

                            Node *parent = tag_stack.data[tag_stack.count - 1].node;
                            Node *node = create_node(memory, token, lexer.file_path, lexer.line_number, attribute_array, parent);

                            tag_stack.data[tag_stack.count].kind = token.kind;
                            tag_stack.data[tag_stack.count].node = node;
                            tag_stack.count += 1;

                            node_array.data[node_array.count] = node;
                            node_array.count += 1;

                            goto proceed_to_next_token;
                        }
                        case TAG_CLOSING: {
                            if (attribute_array.count) {
                                print_tag_error(lexer.file_content, "Closing tag is not allowed to have attributes", lexer.file_path, token.identifier, lexer.line_number);

                                assert(0); /* TODO: remove */
                                return -1;
                            }

                            TagStackElement last_tag_opened = tag_stack.data[tag_stack.count - 1];
                            if (token.kind != last_tag_opened.kind) {

                                char error[] = "NOT SURE WHAT GOES HERE";
                                print_tag_error(lexer.file_content, error, lexer.file_path, last_tag_opened.node->token.identifier, last_tag_opened.node->line_number);

                                assert(0); /* TODO: remove */
                                return -1;
                            }

                            last_tag_opened.node->block.data = last_tag_opened.node->token.tag.markup.data;
                            last_tag_opened.node->block.length = (token.tag.markup.data + token.tag.markup.length) - last_tag_opened.node->block.data;

                            tag_stack.data[tag_stack.count - 1].kind = 0;
                            tag_stack.data[tag_stack.count - 1].node = NULL;
                            tag_stack.count -= 1;

                            goto proceed_to_next_token;
                        }
                        default: {
                            assert(0);
                            break;
                        }
                    }

                    assert(0);
                    break;
                }
                case TOKEN_VAL: {
                    switch (token.tag.type) {
                        case TAG_SELFCLOSING: {
                            if (!has_name_attr) {
                                char error[] = "All tags must contain a name attribute";
                                print_tag_error(lexer.file_content, error, lexer.file_path, token.identifier, lexer.line_number);

                                assert(0); /* TODO: remove */
                                return -1;
                            }

                            for (j = 0; j < attribute_array.count; j++) {
                                if (j == name_attr_index)
                                    continue;

                                char error[] = "Invalid attribute for x-val tag";
                                print_no_attribute_allowed_error(lexer.file_content, error, lexer.file_path, token.identifier, lexer.line_number, &attribute_array.attributes[j]);

                                assert(0); /* TODO: remove */
                                return -1;
                            }

                            Node *parent = tag_stack.data[tag_stack.count - 1].node;
                            Node *node = create_node(memory, token, lexer.file_path, lexer.line_number, attribute_array, parent);

                            node_array.data[node_array.count] = node;
                            node_array.count += 1;

                            goto proceed_to_next_token;
                        }
                        case TAG_OPENING: {
                            char error[] = "x-val must be self-closing";
                            print_tag_error(lexer.file_content, error, lexer.file_path, token.identifier, lexer.line_number);

                            assert(0); /* TODO: remove */
                            return -1;
                        }
                        case TAG_CLOSING: {
                            char error[] = "x-val must be self-closing";
                            print_tag_error(lexer.file_content, error, lexer.file_path, token.identifier, lexer.line_number);

                            assert(0); /* TODO: remove */
                            return -1;
                        }
                        default: {
                            assert(0);
                            break;
                        }
                    }

                    assert(0);
                    break;
                }
                case TOKEN_FOR: {
                    switch (token.tag.type) {
                        case TAG_SELFCLOSING: {
                            char error[] = "x-for can NOT be self-closing";
                            print_tag_error(lexer.file_content, error, lexer.file_path, token.identifier, lexer.line_number);

                            assert(0); /* TODO: remove */
                            return -1;
                        }
                        case TAG_OPENING: {
                            if (!has_name_attr) {
                                char error[] = "All tags must contain a name attribute";
                                print_tag_error(lexer.file_content, error, lexer.file_path, token.identifier, lexer.line_number);

                                assert(0); /* TODO: remove */
                                return -1;
                            }

                            for (j = 0; j < attribute_array.count; j++) {
                                if (j == name_attr_index)
                                    continue;

                                char error[] = "Invalid attribute for x-for tag";
                                print_no_attribute_allowed_error(lexer.file_content, error, lexer.file_path, token.identifier, lexer.line_number, &attribute_array.attributes[j]);

                                assert(0); /* TODO: remove */
                                return -1;
                            }

                            Node *parent = tag_stack.data[tag_stack.count - 1].node;
                            Node *node = create_node(memory, token, lexer.file_path, lexer.line_number, attribute_array, parent);

                            tag_stack.data[tag_stack.count].kind = token.kind;
                            tag_stack.data[tag_stack.count].node = node;
                            tag_stack.count += 1;

                            node_array.data[node_array.count] = node;
                            node_array.count += 1;

                            goto proceed_to_next_token;
                        }
                        case TAG_CLOSING: {
                            if (attribute_array.count) {
                                print_tag_error(lexer.file_content, "Closing tag is not allowed to have attributes", lexer.file_path, token.identifier, lexer.line_number);

                                assert(0); /* TODO: remove */
                                return -1;
                            }

                            TagStackElement last_tag_opened = tag_stack.data[tag_stack.count - 1];
                            if (token.kind != last_tag_opened.kind) {

                                char error[] = "NOT SURE WHAT GOES HERE";
                                print_tag_error(lexer.file_content, error, lexer.file_path, last_tag_opened.node->token.identifier, last_tag_opened.node->line_number);

                                assert(0); /* TODO: remove */
                                return -1;
                            }

                            last_tag_opened.node->block.data = last_tag_opened.node->token.tag.markup.data;
                            last_tag_opened.node->block.length = (token.tag.markup.data + token.tag.markup.length) - last_tag_opened.node->block.data;

                            tag_stack.data[tag_stack.count - 1].kind = 0;
                            tag_stack.data[tag_stack.count - 1].node = NULL;
                            tag_stack.count -= 1;

                            goto proceed_to_next_token;
                        }
                        default: {
                            assert(0);
                            break;
                        }
                    }

                    assert(0);
                    break;
                }
                default: {
                    assert(0);
                    break;
                }
            }

        proceed_to_next_token:;
            token = get_next_token(&lexer);
        }
    }

    /*
     TODO: (Maybe) Warn the user if they are using a component import as a self-closing tag, but the corresponding 
           component definition contains slots. Same idea for component import attributes and placeholders.
    */

    for (i = 0; i < components_soa.count; i++) {
        print_components_soa(&components_soa, i);
        char *component_name = components_soa.names[i];

        if (components_soa.imports_soa[i]) { /* Does the component have any imports? If the component at index `i` in the SOA has no imports, this should be NULL because memory was never allocated. */
            uint32_t num_imports = components_soa.imports_soa[i]->count;

            uint32_t j;
            for (j = 0; j < num_imports; j++) {
                char *import_name = components_soa.imports_soa[i]->names[j];
                if (strncmp(component_name, import_name, strlen(import_name)) == 0) {
                    char error[] = "Recursive import. Component is importing itself";
                    printf("%s:\n", error);
                    printf("    file: %.*s\n", (int)components_soa.file_paths[i].length, components_soa.file_paths[i].data);
                    printf("    line: %d\n", components_soa.imports_soa[i]->ast_nodes[j]->line_number);
                    printf("\n");
                    print_tag_attr_error(components_soa.contents[i], components_soa.imports_soa[i]->ast_nodes[j], components_soa.imports_soa[i]->ast_nodes[j]->attribute_array.attributes[components_soa.imports_soa[i]->ast_nodes[j]->attribute_array.name_attr_index]);

                    assert(0);
                }

                /* Check that imported components actually exist */
                boolean component_exists = false;
                uint32_t imported_component_index = 0;

                ASTNode *import_ast_node = components_soa.imports_soa[i]->ast_nodes[j]; /*
                                                                                         Component AST starting at the currently iterated component import.
                                                                                         Bellow example for Component `foo` and currently iterated component import `bar`:
                                                                                         
                                                                                          <x-def name="foo">
                                                                                              <div>
                                                                                                  <div>
                                                                                                     ↓ FROM HERE
                                                                                                      <x-use name="bar">
                                                                                                          <x-fill name="default">
                                                                                                              ...
                                                                                                          </x-fill>
                                                                                                      </x-use> 
                                                                                                                    ↑ TO HERE
                                                                                                  </div>
                                                                                                  <div>
                                                                                                      <x-use name="baz">
                                                                                                          <x-fill name="default">
                                                                                                              ...
                                                                                                          </x-fill>
                                                                                                      </x-use>
                                                                                                  </div>
                                                                                              </div>
                                                                                          </x-def>
                                                                                        */

                uint32_t k;
                for (k = 0; k < components_soa.count; k++) {
                    if (strncmp(import_name, components_soa.names[k], strlen(import_name)) == 0) {
                        imported_component_index = k;
                        component_exists = true;

                        /*
                         Check that all attributes in the component import statement (except the 'name' attribute) 
                         exist as placeholders in the imported component. 
                         NOTE: Placeholders may appear anywhere within the component definition.
                         
                         component import:
                              ...
                                 <x-use name="bar" fistname="John" input_style="class='text-[22px]/[28px] font-semibold'">
                                    ...                  ^^^^^^^^^^^^^^  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
                                 </x-use>
                              ...
                        
                         imported component:
                              <x-def name="bar">
                                  ...
                                  <input value="%fistname%" %input_style% />
                              </x-def>
                        */
                        uint32_t h;
                        for (h = 0; h < import_ast_node->attribute_array.count; h++) {
                            if (h == import_ast_node->attribute_array.name_attr_index) {
                                continue;
                            }

                            boolean found = false;

                            String import_attr_name = import_ast_node->attribute_array.attributes[h].name;

                            uint32_t f;
                            for (f = 0; f < components_soa.placeholders_soa[k]->count; f++) {
                                /* TODO: make all strncmp to also check for lengths equality like in line bellow */
                                if (strncmp(components_soa.placeholders_soa[k]->names[f], import_attr_name.data, import_attr_name.length) == 0 && strlen(components_soa.placeholders_soa[k]->names[f]) == import_attr_name.length) {
                                    found = true;

                                    break;
                                }
                            }

                            if (!found) {
                                /* TODO: print error */
                                assert(0);
                            }
                        }

                        break;
                    }
                }

                if (component_exists) {
                    /*
                     Check that the component import fills match the slots in the corresponding component.
                     
                     component import:
                          ...
                             <x-use name="bar">
                                 <x-fill name="fistname">
                                      John
                                 </x-fill>
                                 <x-fill name="lastname">
                                      Doe
                                 </x-fill>
                             </x-use>
                          ...
                    
                     imported component:
                          <x-def name="bar">
                              ... 
                              <p>
                                  <x-slot name="fistname" />
                              </p>
                              <p>
                                  <x-slot name="lastname" />
                              </p>
                              ...
                          </x-def>
                    */
                    uint32_t fill_count = components_soa.imports_soa[i]->fills_soa[j].count;
                    FillSOA *fills_soa = &components_soa.imports_soa[i]->fills_soa[j];
                    uint32_t h;
                    for (h = 0; h < fill_count; h++) {
                        char *fill = fills_soa->names[h];

                        boolean slot_exists = false;
                        uint32_t imported_component_slot_index = 0;

                        if (components_soa.slots_soa[imported_component_index]) {
                            uint32_t f;
                            for (f = 0; f < components_soa.slots_soa[imported_component_index]->count; f++) {
                                if (strncmp(components_soa.slots_soa[imported_component_index]->names[f], fill, strlen(fill)) == 0) {
                                    slot_exists = true;
                                    imported_component_slot_index = f;
                                    break;
                                }
                            }
                        }

                        if (!slot_exists) {
                            /* TODO: print error */
                            assert(0);
                        }
                    }
                } else {
                    char error[] = "Component you are trying to import does not exist";
                    printf("%s:\n", error);
                    printf("    file: %.*s\n", (int)components_soa.file_paths[i].length, components_soa.file_paths[i].data);
                    printf("    line: %d\n", components_soa.imports_soa[i]->ast_nodes[j]->line_number);
                    printf("\n");
                    print_tag_attr_error(components_soa.contents[i], components_soa.imports_soa[i]->ast_nodes[j], components_soa.imports_soa[i]->ast_nodes[j]->attribute_array.attributes[components_soa.imports_soa[i]->ast_nodes[j]->attribute_array.name_attr_index]);

                    assert(0);
                }
            }
        }
    }

    return 0;
}
