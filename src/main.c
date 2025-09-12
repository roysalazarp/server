#define _XOPEN_SOURCE 700  // Required for strptime on Linux

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <sqlite3.h>
#include <argon2.h>
#include <time.h>

#include "ark/shared.h"
#include "ark/server.h"
#include "ark/memory.h"
#include "ark/url.h"
#include "ark/http.h"
#include "ark/template.h"
#include "ark/utils.h"
#include "ark/task.h"

#define PORT 8081

typedef enum {
    ERR_MIN_PASSWORD_LENGTH,
    ERR_INVALID_EMAIL,
} UserErrorCodes;

char *error_messages[] = {
    [ERR_MIN_PASSWORD_LENGTH] = "Password should be at least 4 characters",
    [ERR_INVALID_EMAIL] = "Invalid email format",
};

#define IS_ASCII_LETTER(c) (((c) >= 'A' && (c) <= 'Z') || ((c) >= 'a' && (c) <= 'z'))
#define IS_EMAIL_TOKEN(c)  ((IS_ASCII_LETTER(c)) || ((c) == '-') || ((c) == '_') || ((c) == '+'))

#define ARK_MAX_HTTP_REQUEST_SIZE MB(300)
#define ARK_MAX_QUEUED_HTTP_REQUESTS 500

#define MYDIR "./assets"

#define PAGE_SIZE KB(4)
#define MAX_REQUEST_MSG_SIZE KB(2)

#define HASH_LENGTH 32
#define SALT_LENGTH 16
#define PASSWORD_BUFFER 255

#define UrlEqual(S) \
  SlicesEqual(request + msg->uri.a, msg->uri.b - msg->uri.a, S, strlen(S))

#define UrlStartsWith(S)                   \
  (msg->uri.b - msg->uri.a >= strlen(S) && \
   !memcmp(request + msg->uri.a, S, strlen(S)))

typedef struct {
    char server[20];
    char uri[20];
    Channel *done;
} ServerUri;

typedef struct {
    Memory *memory;
    int fd;
    struct HttpMessage *msg;
    char *request;
} RequestCtx;

char **component_name_arr;
char **component_content_arr;
uint32_t component_arr_length;

sqlite3 *db;

int check_password(char *errors[1], uint32_t *errors_count, char *password, uint32_t password_length) {
    if (password_length < 4) {
        errors[*errors_count] = error_messages[ERR_MIN_PASSWORD_LENGTH];
        *errors_count += 1;
    }

    return 0;
}

#define EMAIL_START 0
#define EMAIL_USERNAME 1
#define EMAIL_MAIL_SERVER 2
#define EMAIL_DOMAIN 3

typedef struct {
    struct {
        uint32_t start;
        uint32_t end;
    } username;
    struct {
        uint32_t start;
        uint32_t end;
    } mail_server;
    struct {
        uint32_t start;
        uint32_t end;
    } domain;
} Email;

int parse_email(Email *e, char *email, uint32_t email_length) {
    uint32_t cursor = 0;
    int ch;

    int start;
    
    uint32_t t = 0;
    for (; cursor < email_length;) {
        ch = email[cursor];
        switch (t) {
            case EMAIL_START: {
                for (;;) {
                    if (IS_EMAIL_TOKEN(ch)) {
                        start = cursor;
                        t = EMAIL_USERNAME;
                        break;
                    }
                    if (ch != ' ') {
                        return -1;
                    }
                    if (++cursor == email_length) {
                        break;
                    }
                    ch = email[cursor];
                }
                break;
            }
            case EMAIL_USERNAME: {
                for (;;) {
                    if (ch == '@') {
                        e->username.start = start;
                        e->username.end = cursor - 1;
                        
                        if (!IS_ASCII_LETTER(email[e->username.start]) || !IS_ASCII_LETTER(email[e->username.end])) {
                            return -1;
                        }

                        cursor += 1; /* skip @ */
                        start = cursor;
                        t = EMAIL_MAIL_SERVER;
                        break;
                    }
                    if (!IS_EMAIL_TOKEN(ch)) {
                        return -1;
                    }
                    if (++cursor == email_length) {
                        break;
                    }
                    ch = email[cursor];
                }
                break;
            }
            case EMAIL_MAIL_SERVER: {
                for (;;) {
                    if (ch == '.') {
                        e->mail_server.start = start;
                        e->mail_server.end = cursor - 1;

                        if (!IS_ASCII_LETTER(email[e->mail_server.start]) || !IS_ASCII_LETTER(email[e->mail_server.end])) {
                            return -1;
                        }
                        
                        cursor += 1; /* skip . */
                        start = cursor;
                        t = EMAIL_DOMAIN;
                        break;
                    }
                    if (!IS_EMAIL_TOKEN(ch)) {
                        return -1;
                    }
                    if (++cursor == email_length) {
                        break;
                    }
                    ch = email[cursor];
                }
                break;
            }
            case EMAIL_DOMAIN: {
                for (;;) {
                    if (!IS_ASCII_LETTER(ch)) {
                        return -1;
                    }
                    if (++cursor == email_length) {
                        e->domain.start = start;
                        e->domain.end = cursor - 1;
                        
                        break;
                    }
                    ch = email[cursor];
                }
                break;
            }
            default: {
                break;
            }
        }
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

void make_request(void *v) {
    ServerUri *server_uri = (ServerUri *)v;

    int filedescriptor, n;
	char buf[512];
	
    if((filedescriptor = netdial(TCP, server_uri->server, 3000)) < 0){
        fprintf(stderr, "dial %s: %s (%s)\n", server_uri->server, strerror(errno), taskgetstate());
        assert(0);
    }
    snprintf(buf, sizeof buf, "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", server_uri->uri, server_uri->server);
    ark_fd_write(filedescriptor, buf, strlen(buf));
    while((n = ark_fd_read(filedescriptor, buf, sizeof buf)) > 0)
        ;
    close(filedescriptor);
    chansend(server_uri->done, NULL);
}

void get_a(void *v) {
    RequestCtx *request_ctx = (RequestCtx *)v;

	ServerUri *server_uri = memory_alloc(request_ctx->memory, sizeof(ServerUri));
    memcpy(server_uri->server, "localhost", strlen("localhost"));
    memcpy(server_uri->uri, "/a", strlen("/a"));

    int stack_size = KB(8);
    
    printf("get_a req to /a\n");
    Channel *done = chancreate(sizeof(void*), 0);
    server_uri->done = done;
    ark_task_create(make_request, server_uri, stack_size);
    chanrecv(done, NULL);
    printf("get_a req to /a DONE\n");

	ServerUri *server_uri_1 = memory_alloc(request_ctx->memory, sizeof(ServerUri));
    memcpy(server_uri_1->server, "localhost", strlen("localhost"));
    memcpy(server_uri_1->uri, "/b", strlen("/b"));

    printf("get_a req to /b\n");
    Channel *done_1 = chancreate(sizeof(void*), 0);
    server_uri_1->done = done_1;
    ark_task_create(make_request, server_uri_1, stack_size);
    chanrecv(done_1, NULL);
    printf("get_a req to /b DONE\n");

    char resbuf[1000];
    snprintf(resbuf, sizeof resbuf, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 11\r\n\r\nHello World");
    int len = strlen(resbuf);
    ark_fd_write(request_ctx->fd, resbuf, len);
}

void get_b(void *v) {
    RequestCtx *request_ctx = (RequestCtx *)v;

	ServerUri *server_uri = memory_alloc(request_ctx->memory, sizeof(ServerUri));
    memcpy(server_uri->server, "localhost", strlen("localhost"));
    memcpy(server_uri->uri, "/b", strlen("/b"));

    int stack_size = KB(8);
    
    printf("get_b req to /b\n");
    Channel *done = chancreate(sizeof(void*), 0);
    server_uri->done = done;
    ark_task_create(make_request, server_uri, stack_size);
    chanrecv(done, NULL);
    printf("get_b req to /b DONE\n");

	ServerUri *server_uri_1 = memory_alloc(request_ctx->memory, sizeof(ServerUri));
    memcpy(server_uri_1->server, "localhost", strlen("localhost"));
    memcpy(server_uri_1->uri, "/a", strlen("/a"));

    printf("get_b req to /a\n");
    Channel *done_1 = chancreate(sizeof(void*), 0);
    server_uri_1->done = done_1;
    ark_task_create(make_request, server_uri_1, stack_size);
    chanrecv(done_1, NULL);
    printf("get_b req to /a DONE\n");

    char resbuf[1000];
    snprintf(resbuf, sizeof resbuf, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 11\r\n\r\nHello World");
    int len = strlen(resbuf);
    ark_fd_write(request_ctx->fd, resbuf, len);
}

int string_compare(char *str, char *str2, size_t str_length) {
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

void home(void *v) {
    RequestCtx *request_ctx = (RequestCtx *)v;

    uint32_t index = 0;
    find_element_index(&index, component_name_arr, "home", component_arr_length);
    char *component = component_content_arr[index];
    uint32_t component_length = strlen(component);
    assert(component_length);

    char response[KB(2)];
    bzero(response, KB(2));
    sprintf(response,
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: %d\r\n"
        "Content-Type: text/html\r\n\r\n"
        "%s",
        component_length, component);
    
    int response_length = strlen(response);
    ark_fd_write(request_ctx->fd, response, response_length);
    
    close(request_ctx->fd);
    memory_free(request_ctx->memory);
}

void signup_create_account(void *v) {
    RequestCtx *request_ctx = (RequestCtx *)v;
    char *request = request_ctx->request;
    struct HttpMessage *msg = request_ctx->msg;

    struct UrlParams h;
    ParseParams(request + msg->i, -1, &h);

    char *errors[1] = {0};
    uint32_t errors_count = 0;
    char *email = h.p[0].val.p;
    uint32_t email_length = h.p[0].val.n;
    check_email(errors, &errors_count, email, email_length);
    
    memset(errors, 0, sizeof(errors));
    errors_count = 0;
    char *password = h.p[1].val.p;
    uint32_t password_length = h.p[1].val.n;
    check_password(errors, &errors_count, h.p[1].val.p, h.p[1].val.n);

    uint8_t salt[SALT_LENGTH];
    memset(salt, 0, SALT_LENGTH);

    assert(generate_salt(salt, SALT_LENGTH) != -1);

    uint32_t t_cost = 2;
    uint32_t m_cost = (1 << 16);
    uint32_t parallelism = 1;

    uint8_t hash[HASH_LENGTH];
    memset(hash, 0, HASH_LENGTH);

    char hashed_password[PASSWORD_BUFFER];
    memset(hashed_password, 0, PASSWORD_BUFFER);
    if (argon2i_hash_raw(t_cost, m_cost, parallelism, password, password_length, salt, SALT_LENGTH, hash, HASH_LENGTH) != ARGON2_OK) {
        printf("Failed to create hash from password\n");
        assert(0);
    }

    if (argon2i_hash_encoded(t_cost, m_cost, parallelism, password, password_length, salt, SALT_LENGTH, HASH_LENGTH, hashed_password, PASSWORD_BUFFER) != ARGON2_OK) {
        printf("Failed to encode hash\n");
        assert(0);
    }

    if (argon2i_verify(hashed_password, password, password_length) != ARGON2_OK) {
        printf("Failed to verify password\n");
        assert(0);
    }

    const char *sql = "INSERT INTO users (email, password) VALUES (?, ?) RETURNING id;";
    sqlite3_stmt *stmt = NULL;

    sqlite3_prepare_v2((sqlite3 *)db, sql, strlen(sql) + 1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, email, email_length, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, hashed_password, strlen(hashed_password), SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        assert(0);
    }
    
    int user_id = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);

    sql = "INSERT OR REPLACE INTO sessions (user_id, expires_at, updated_at) "
                  "VALUES (?, datetime('now', '+1 hour'), datetime('now')) "
                  "RETURNING id, expires_at;";
    
    sqlite3_prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, user_id);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        assert(0);
    }

    int session_id = sqlite3_column_int(stmt, 0);
    const unsigned char *expires_at = sqlite3_column_text(stmt, 1);
    
    struct tm tm;
    // Parse ISO 8601 datetime
    if (strptime((const char *)expires_at, "%Y-%m-%d %H:%M:%S", &tm) == NULL) {
        fprintf(stderr, "Failed to parse datetime\n");
        assert(0);
    }

    // Convert to time_t (seconds since epoch)
    time_t t = timegm(&tm);  // timegm treats tm as UTC

    // Format as HTTP Date header
    char http_date[100];
    strftime(http_date, sizeof(http_date), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&t));

    printf("HTTP Date: %s\n", http_date);

    sqlite3_finalize(stmt);

    char response[KB(4)];
    bzero(response, KB(4));
    sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Set-Cookie: session_id=%d; Path=/; HttpOnly; Secure; SameSite=Strict; Expires=%s\r\n"
            "Content-Length: 0\r\n"
            "HX-Redirect: /\r\n\r\n",
            session_id, http_date);
    
    int response_length = strlen(response);
    ark_fd_write(request_ctx->fd, response, response_length);
    
    close(request_ctx->fd);
    memory_free(request_ctx->memory);
}

void signup(void *v) {
    RequestCtx *request_ctx = (RequestCtx *)v;

    uint32_t index = 0;
    find_element_index(&index, component_name_arr, "signup", component_arr_length);
    char *component = component_content_arr[index];
    uint32_t component_length = strlen(component);
    assert(component_length);

    char response[KB(4)];
    bzero(response, KB(4));
    sprintf(response,
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: %d\r\n"
        "Content-Type: text/html\r\n\r\n"
        "%s",
        component_length, component);
    
    int response_length = strlen(response);
    ark_fd_write(request_ctx->fd, response, response_length);
    
    close(request_ctx->fd);
    memory_free(request_ctx->memory);
}

void not_found(void *v) {
    RequestCtx *request_ctx = (RequestCtx *)v;

    uint32_t index = 0;
    find_element_index(&index, component_name_arr, "not_found", component_arr_length);
    char *component = component_content_arr[index];
    uint32_t component_length = strlen(component);
    assert(component_length);

    char response[KB(2)];
    bzero(response, KB(2));
    sprintf(response,
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: %d\r\n"
        "Content-Type: text/html\r\n\r\n"
        "%s",
        component_length, component);
    
    int response_length = strlen(response);
    ark_fd_write(request_ctx->fd, response, response_length);
    
    close(request_ctx->fd);
    memory_free(request_ctx->memory);
}

void handler(void *arg) {
    int fd = *(int *)arg;

    for (;;) {
        ark_fd_await(fd, 'r');
        
        Memory *memory = reserve_memory(PAGE_SIZE * 15);
        RequestCtx *request_ctx = memory_alloc(memory, sizeof(RequestCtx));
        request_ctx->memory = memory;
        
        ark_client_accept(&request_ctx->fd, fd);
        ark_fd_await(request_ctx->fd, 'r');

        char *request = memory_alloc(memory, sizeof(char) * MAX_REQUEST_MSG_SIZE);
        request_ctx->request = request;
        ark_fd_read(request_ctx->fd, request, MAX_REQUEST_MSG_SIZE);
        size_t request_length = strlen(request);

        int inmsglen;
        ssize_t got;

        struct HttpMessage *msg = memory_alloc(memory, sizeof(struct HttpMessage));
        request_ctx->msg = msg;

        InitHttpMessage(msg, kHttpRequest);
        
        if ((inmsglen = ParseHttpMessage(msg, request, request_length, request_length)) <= 0) {
            if (!inmsglen) {
                printf("client sent fragmented message\n");
            } else {
                printf("client sent bad message\n");
            }
            break;
        }

        printf("\n");
        
        if (UrlEqual("/")) {
            ark_task_create(home, request_ctx, KB(8));
        } else if (UrlEqual("/signup")) {
            ark_task_create(signup, request_ctx, KB(16));
        } else if (UrlStartsWith("/signup/create-account")) {
            ark_task_create(signup_create_account, request_ctx, KB(16));
        } else {
            ark_task_create(not_found, request_ctx, KB(8));
        }

        // if (strncmp(uri, "/a", uri_length) == 0 && uri_length == strlen("/a")) {
        //     ark_task_create(get_a, request_ctx, KB(8));
        // } else if (strncmp(uri, "/b", uri_length) == 0 && uri_length == strlen("/b")) {
        //     ark_task_create(get_b, request_ctx, KB(8));
        // } else {
        //     ark_task_create(not_found, request_ctx, KB(8));
        // }

    }
}

int main() {
    setbuf(stdout, NULL);

    if (sqlite3_open("/workspaces/server/dev.sqlite", &db) != SQLITE_OK) {
        fprintf(stderr, "Cannot open DB: %s\n", sqlite3_errmsg(db));
        assert(0);
    }

    const char *sql = "SELECT id, email, created_at FROM users;";
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare: %s\n", sqlite3_errmsg(db));
        assert(0);
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const unsigned char *email = sqlite3_column_text(stmt, 1);
        const unsigned char *created = sqlite3_column_text(stmt, 2);

        printf("id=%d, email=%s, created_at=%s\n", id, email, created);
    }

    sqlite3_finalize(stmt);

    uint32_t i;
    
    uint32_t num_of_filepaths = 0;
    ark_count_files(&num_of_filepaths, MYDIR);
    
    Memory *memory = reserve_memory(PAGE_SIZE * 50);
    
    size_t *array_of_lengths = memory_alloc(memory, num_of_filepaths * sizeof(size_t));
    ark_list_file_sizes(array_of_lengths, num_of_filepaths, MYDIR);
    
    char **array_of_filepaths = memory_alloc(memory, num_of_filepaths * sizeof(char *));
    for (i = 0; i < num_of_filepaths; i++) {
        array_of_filepaths[i] = (char *)memory_alloc(memory, array_of_lengths[i] * sizeof(char));
    }

    ark_list_files(array_of_filepaths, num_of_filepaths, MYDIR);

    char **array_of_file_contents = memory_alloc(memory, num_of_filepaths * sizeof(char *));

    for (i = 0; i < num_of_filepaths; i++) {
        char *filepath = array_of_filepaths[i];
        
        size_t file_size = ark_get_file_size(filepath);
        char *file_content = memory_alloc(memory, file_size + 1);
            
        FILE *file = fopen(filepath, "r");
        size_t read_size = fread(file_content, sizeof(char), file_size, file);
        assert(read_size == file_size);
        file_content[file_size + 1] = '\0';
        fclose(file);

        array_of_file_contents[i] = file_content;
    }
    
    uint32_t mask_size = (num_of_filepaths / U32_BITS) ? (num_of_filepaths / U32_BITS) : 1; /* ensure a minimum */
    uint32_t *html_file_mask = memory_alloc(memory, mask_size * sizeof(uint32_t));

    ark_make_html_file_mask(html_file_mask, array_of_filepaths, num_of_filepaths);
    
    ark_validate_templates(html_file_mask, array_of_filepaths, array_of_file_contents, num_of_filepaths);
    
    ark_templates_count(&component_arr_length, html_file_mask, array_of_filepaths, array_of_file_contents, num_of_filepaths);

    uint32_t *names_lengths = memory_alloc(memory, component_arr_length * sizeof(uint32_t *));
    uint32_t *templates_lengths = memory_alloc(memory, component_arr_length * sizeof(uint32_t *));
    
    ark_get_templates_info(names_lengths, templates_lengths, component_arr_length, html_file_mask, array_of_filepaths, array_of_file_contents, num_of_filepaths);
    
    component_name_arr = memory_alloc(memory, component_arr_length * sizeof(char *));
    char **template_content_array = memory_alloc(memory, component_arr_length * sizeof(char *));
    for (i = 0; i < component_arr_length; i++) {
        component_name_arr[i] = memory_alloc(memory, names_lengths[i]);
        template_content_array[i] = memory_alloc(memory, templates_lengths[i]);
    }
    
    ark_make_templates_array(component_name_arr, template_content_array, component_arr_length, html_file_mask, array_of_filepaths, array_of_file_contents, num_of_filepaths);

    uint32_t *compiled_templates_lengths = memory_alloc(memory, component_arr_length * sizeof(uint32_t *));
    ark_get_compile_templates_lengths(compiled_templates_lengths, component_name_arr, template_content_array, component_arr_length);
    
    component_content_arr = memory_alloc(memory, component_arr_length * sizeof(char *));
    for (i = 0; i < component_arr_length; i++) {
        component_content_arr[i] = memory_alloc(memory, compiled_templates_lengths[i]);
    }
    
    ark_compile_templates(component_content_arr, component_name_arr, template_content_array, component_arr_length);
    
    int fd;
    ark_setup_server(&fd, PORT);

    ark_task_create(handler, &fd, KB(8)); /** NOTE: Keep an eye that the stack size does not become a problem */
	ark_task_run_scheduler();
    
    /** NOTE: Code bellow will never run */
    
    sqlite3_close(db);

    return 0;
}