#define _XOPEN_SOURCE 700  // Required for strptime on Linux

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sys/mman.h>

#include "ark/shared.h"
#include "ark/server.h"
#include "ark/memory.h"
#include "ark/url.h"
#include "ark/http.h"
#include "ark/cookies.h"
#include "ark/template.h"
#include "ark/utils.h"
#include "ark/task.h"

#include "routes/routes.h"
#include "parsers/parsers.h"
#include "globals.h"

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS 0x20
#endif

#define PORT 443

#define ARK_MAX_HTTP_REQUEST_SIZE MB(300)
#define ARK_MAX_QUEUED_HTTP_REQUESTS 500

#define MYDIR "./assets"

#define PAGE_SIZE KB(4)
#define MAX_REQUEST_MSG_SIZE KB(2)

#define CheckUrlAndMethod(s, m) \
  ((SlicesEqual(rctx.raw_request + rctx.msg->uri.a, rctx.msg->uri.b - rctx.msg->uri.a, s, strlen(s))) && (rctx.msg->method == m))

#define GET(s) \
  CheckUrlAndMethod(s, kHttpGet)

#define POST(s) \
  CheckUrlAndMethod(s, kHttpPost)

#define UrlEqual(S) \
  SlicesEqual(rctx.raw_request + rctx.msg->uri.a, rctx.msg->uri.b - rctx.msg->uri.a, S, strlen(S))

#define UrlStartsWith(S)                   \
  (msg->uri.b - msg->uri.a >= strlen(S) && \
   !memcmp(request + msg->uri.a, S, strlen(S)))

typedef struct {
    char server[20];
    char uri[20];
    Channel *done;
} ServerUri;

typedef struct {
    uint32_t start;
    uint32_t end;
} Range;

typedef struct {
    uint32_t *ctx_name_lengths_arr;
    char **ctx_name_arr;
    uint64_t *ctx_arr;
    uint32_t length;
} TemplateRenderingCtxArr;

char **component_name_arr;
char **component_content_arr;
TemplateRenderingCtxArr *component_rendering_ctx_arr;

uint32_t component_arr_length;
sqlite3 *db;

SSL_CTX *ctx;

char *error_messages[] = {
    [ERR_MIN_PASSWORD_LENGTH] = "Password should be at least 4 characters",
    [ERR_INVALID_EMAIL] = "Invalid email format",
};

int
ark_fd_read(SSL *ssl, int fd, void *buf, int n)
{
	int m;
	
	while((m=SSL_read(ssl, buf, n)) < 0) {
		int err = SSL_get_error(ssl, m);
        ark_fd_await(fd, 'r');
    }

	return m;
}

int
ark_fd_write(SSL *ssl, int fd, void *buf, int n)
{
	int m, tot;
	
	for(tot=0; tot<n; tot+=m){
		while((m=SSL_write(ssl, (char*)buf+tot, n-tot)) < 0 && errno == EAGAIN)
			ark_fd_await(fd, 'w');
		if(m < 0)
			return m;
		if(m == 0)
			break;
	}
	return tot;
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
    // ark_fd_write(filedescriptor, buf, strlen(buf));
    // while((n = ark_fd_read(filedescriptor, buf, sizeof buf)) > 0)
    //     ;
    close(filedescriptor);
    chansend(server_uri->done, NULL);
}

void get_a(void *v) {
    RequestCtx *rctx = (RequestCtx *)v;

	ServerUri *server_uri = memory_alloc(rctx->memory, sizeof(ServerUri));
    memcpy(server_uri->server, "localhost", strlen("localhost"));
    memcpy(server_uri->uri, "/a", strlen("/a"));

    int stack_size = KB(8);
    
    printf("get_a req to /a\n");
    Channel *done = chancreate(sizeof(void*), 0);
    server_uri->done = done;
    ark_task_create(make_request, server_uri, stack_size);
    chanrecv(done, NULL);
    printf("get_a req to /a DONE\n");

	ServerUri *server_uri_1 = memory_alloc(rctx->memory, sizeof(ServerUri));
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
    // ark_fd_write(rctx->fd, resbuf, len);
}

void get_b(void *v) {
    RequestCtx *rctx = (RequestCtx *)v;

	ServerUri *server_uri = memory_alloc(rctx->memory, sizeof(ServerUri));
    memcpy(server_uri->server, "localhost", strlen("localhost"));
    memcpy(server_uri->uri, "/b", strlen("/b"));

    int stack_size = KB(8);
    
    printf("get_b req to /b\n");
    Channel *done = chancreate(sizeof(void*), 0);
    server_uri->done = done;
    ark_task_create(make_request, server_uri, stack_size);
    chanrecv(done, NULL);
    printf("get_b req to /b DONE\n");

	ServerUri *server_uri_1 = memory_alloc(rctx->memory, sizeof(ServerUri));
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
    // ark_fd_write(rctx->fd, resbuf, len);
}

void test(void *v) {
    RequestCtx *rctx = (RequestCtx *)v;

    uint32_t index = 0;
    find_element_index(&index, component_name_arr, "foo", component_arr_length);
    char *component = component_content_arr[index];
    uint32_t component_length = strlen(component);
    assert(component_length);

    uint32_t cindex;

    uint32_t i;
    for (i = 0; i < component_rendering_ctx_arr[index].length; i++) {
        if (strncmp(component_rendering_ctx_arr[index].ctx_name_arr[i], "tables", strlen("tables")) == 0) {
            cindex = i;
            break;
        }
    }

    uint64_t range = component_rendering_ctx_arr[i].ctx_arr[cindex];

    char response[KB(8)];
    bzero(response, KB(8));
    sprintf(response,
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: %d\r\n"
        "Content-Type: text/html\r\n\r\n"
        "%s",
        component_length, component);
    
    int response_length = strlen(response);
    ark_fd_write(rctx->ssl, rctx->fd, response, response_length);
    
    SSL_shutdown(rctx->ssl);
    SSL_free(rctx->ssl);
    close(rctx->fd);
    memory_free(rctx->memory);
}

void handler(void *arg) {
    int fd = *(int *)arg;

    for (;;) {
        int op_res;
        ark_fd_await(fd, 'r');
        
        char err[ERR_SIZE] = {0};
        
        RequestCtx rctx = {0};

        void *mem = NULL;
        uint64_t mem_size = PAGE_SIZE * 3;

        mem = mmap(NULL, (size_t)mem_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        // TODO: this may err, return err message to web frontend
        
        Memory memory = {0};
        memory_init(&memory, mem, mem_size);

        rctx.memory = &memory;
        
        ark_client_accept(&rctx.fd, fd);
        ark_fd_await(rctx.fd, 'r');
        
        rctx.ssl = SSL_new(ctx);
        if (!rctx.ssl) {
            fprintf(stderr, "Failed to create SSL object\n");
            ERR_print_errors_fp(stderr);
            assert(0);
        }

        op_res = SSL_set_fd(rctx.ssl, rctx.fd);
        if (op_res != 1) {
            fprintf(stderr, "Failed to attach socket to SSL\n");
            ERR_print_errors_fp(stderr);
            assert(0);
        }

        try_ssl_accept_again:;
        
        op_res = SSL_accept(rctx.ssl);
        if (op_res == 1) {
            printf("handshake completed!\n");
        } else {
            int err = SSL_get_error(rctx.ssl, op_res);
            if (err == SSL_ERROR_WANT_READ) {
                ark_fd_await(rctx.fd, 'r');
                printf("Trying SSL_accept again\n");
                goto try_ssl_accept_again;
            }

            fprintf(stderr, "TLS/SSL handshake failed\n");
            ERR_print_errors_fp(stderr);
            assert(0);
        }

        rctx.raw_request = memory_alloc(rctx.memory, sizeof(char) * MAX_REQUEST_MSG_SIZE);
        ark_fd_read(rctx.ssl, rctx.fd, rctx.raw_request, MAX_REQUEST_MSG_SIZE);
        rctx.raw_request_length = strlen(rctx.raw_request);

        rctx.msg = memory_alloc(rctx.memory, sizeof(struct HttpMessage));
        
        int inmsglen;
        ssize_t got;
        InitHttpMessage(rctx.msg, kHttpRequest);
        if ((inmsglen = ParseHttpMessage(rctx.msg, rctx.raw_request, rctx.raw_request_length, rctx.raw_request_length)) <= 0) {
            if (!inmsglen) {
                printf("client sent fragmented message\n");
            } else {
                printf("client sent bad message\n");
            }
            break;
        }
        
        if (GET("/")) {
            ark_task_create(home, &rctx, KB(15));
        } else if (GET("/test")) {
            ark_task_create(test, &rctx, KB(15));
        } else if (GET("/login")) {
            ark_task_create(login, &rctx, KB(15));
        } else if (POST("/logout")) {
            ark_task_create(logout, &rctx, KB(10));
        } else if (GET("/signup")) {
            ark_task_create(signup, &rctx, KB(15));
        } else if (POST("/login/create-session")) {
            ark_task_create(login_create_session, &rctx, KB(15));
        } else if (POST("/signup/create-account")) {
            ark_task_create(signup_create_account, &rctx, KB(15));
        } else {
            ark_task_create(not_found, &rctx, KB(10));
        }

        // if (strncmp(uri, "/a", uri_length) == 0 && uri_length == strlen("/a")) {
        //     ark_task_create(get_a, rctx, KB(8));
        // } else if (strncmp(uri, "/b", uri_length) == 0 && uri_length == strlen("/b")) {
        //     ark_task_create(get_b, rctx, KB(8));
        // } else {
        //     ark_task_create(not_found, rctx, KB(8));
        // }

    }
}

int full_path(char *buffer, uint32_t buffer_size, char *relative_path) {
    char cwd[PATH_MAX];

    int res = getcwd(cwd, sizeof(cwd));
    if (!res) {
        perror("getcwd");
        return -1;
    }

    snprintf(buffer, buffer_size, "%s/%s", cwd, relative_path);    
    return 0;
}

int main() {
    setbuf(stdout, NULL);
    
    char err[ERR_SIZE] = {0};

    uint32_t i;
    int op_res;
    
    uint32_t num_of_filepaths = 0;
    ark_count_files(&num_of_filepaths, MYDIR); /* We'll need this later for memory allocation */
    
    void *mem = NULL;
    uint64_t mem_size = PAGE_SIZE * 12;

    PANIC_ON_ERR(MAP_FAILED, (mem = mmap(NULL, (size_t)mem_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)), "mmap failed");
    
    Memory memory = {0};
    memory_init(&memory, mem, mem_size);
    
    size_t *array_of_lengths = memory_alloc(&memory, num_of_filepaths * sizeof(size_t)); /* How long is each file path */
    ark_list_file_sizes(array_of_lengths, num_of_filepaths, MYDIR);
    
    char **array_of_filepaths = memory_alloc(&memory, num_of_filepaths * sizeof(char *));
    for (i = 0; i < num_of_filepaths; i++) {
        array_of_filepaths[i] = (char *)memory_alloc(&memory, array_of_lengths[i] * sizeof(char));
    }

    ark_list_files(array_of_filepaths, num_of_filepaths, MYDIR);

    char **array_of_file_contents = memory_alloc(&memory, num_of_filepaths * sizeof(char *));

    for (i = 0; i < num_of_filepaths; i++) {
        char *filepath = array_of_filepaths[i];
        
        size_t file_size = ark_get_file_size(filepath);
        char *file_content = memory_alloc(&memory, file_size + 1);
            
        FILE *file = fopen(filepath, "r");
        size_t read_size = fread(file_content, sizeof(char), file_size, file);
        assert(read_size == file_size);
        file_content[file_size + 1] = '\0';
        fclose(file);

        array_of_file_contents[i] = file_content;
    }
    
    uint32_t mask_size = (num_of_filepaths / U32_BITS) ? (num_of_filepaths / U32_BITS) : 1; /* ensure a minimum */
    uint32_t *html_file_mask = memory_alloc(&memory, mask_size * sizeof(uint32_t));

    ark_make_html_file_mask(html_file_mask, array_of_filepaths, num_of_filepaths);
    
    ark_validate_templates(html_file_mask, array_of_filepaths, array_of_file_contents, num_of_filepaths);

    ark_templates_count(&component_arr_length, html_file_mask, array_of_filepaths, array_of_file_contents, num_of_filepaths);
    
    uint32_t *names_lengths = memory_alloc(&memory, component_arr_length * sizeof(uint32_t *));
    uint32_t *templates_lengths = memory_alloc(&memory, component_arr_length * sizeof(uint32_t *));
    
    ark_get_templates_info(names_lengths, templates_lengths, component_arr_length, html_file_mask, array_of_filepaths, array_of_file_contents, num_of_filepaths);
    
    component_name_arr = memory_alloc(&memory, component_arr_length * sizeof(char *));
    char **template_content_array = memory_alloc(&memory, component_arr_length * sizeof(char *));
    for (i = 0; i < component_arr_length; i++) {
        component_name_arr[i] = memory_alloc(&memory, names_lengths[i] + 1);
        template_content_array[i] = memory_alloc(&memory, templates_lengths[i] + 1);
    }

    ark_make_templates_array(component_name_arr, template_content_array, component_arr_length, html_file_mask, array_of_filepaths, array_of_file_contents, num_of_filepaths);
    
    uint32_t *compiled_templates_lengths = memory_alloc(&memory, component_arr_length * sizeof(uint32_t *));
    
    ark_get_compile_templates_lengths(compiled_templates_lengths, component_name_arr, template_content_array, component_arr_length);
    
    component_content_arr = memory_alloc(&memory, component_arr_length * sizeof(char *));
    for (i = 0; i < component_arr_length; i++) {
        component_content_arr[i] = memory_alloc(&memory, compiled_templates_lengths[i] + 1);
    }
    
    ark_compile_templates(component_content_arr, component_name_arr, template_content_array, component_arr_length);

    uint32_t *template_rendering_ctx_count = memory_alloc(&memory, component_arr_length * sizeof(uint32_t));

    ark_count_template_rendering_ctx_amount(template_rendering_ctx_count, component_content_arr, component_arr_length);

    component_rendering_ctx_arr = memory_alloc(&memory, component_arr_length * sizeof(TemplateRenderingCtxArr));
    for (i = 0; i < component_arr_length; i++) {
        component_rendering_ctx_arr[i].length = template_rendering_ctx_count[i];
        component_rendering_ctx_arr[i].ctx_arr = memory_alloc(&memory, template_rendering_ctx_count[i] * sizeof(uint64_t));
        component_rendering_ctx_arr[i].ctx_name_lengths_arr = memory_alloc(&memory, template_rendering_ctx_count[i] * sizeof(uint32_t));
        component_rendering_ctx_arr[i].ctx_name_arr = memory_alloc(&memory, template_rendering_ctx_count[i] * sizeof(char *));
    }

    for (i = 0; i < component_arr_length; i++) {
        char *component = component_content_arr[i];
        uint32_t component_length = strlen(component);
        
        ark_get_single_template_rendering_ctx_lengths_info(component_rendering_ctx_arr[i].ctx_name_lengths_arr, component_rendering_ctx_arr[i].length, component, component_length);
    }

    for (i = 0; i < component_arr_length; i++) {
        uint32_t j;
        for (j = 0; j < component_rendering_ctx_arr[i].length; j++) {
            component_rendering_ctx_arr[i].ctx_name_arr[j] = memory_alloc(&memory, component_rendering_ctx_arr[i].ctx_name_lengths_arr[j] + 1);
        }
    }
    
    for (i = 0; i < component_arr_length; i++) {
        char *component = component_content_arr[i];
        uint32_t component_length = strlen(component);
        
        ark_get_single_template_rendering_ctx(component_rendering_ctx_arr[i].ctx_name_arr, component_rendering_ctx_arr[i].ctx_arr, component_rendering_ctx_arr[i].length, component, component_length);
    }

    char db_relpath[] = "./dev.db";
    char db_path[1024];
    bzero(db_path, 1024);
    full_path(db_path, 1024, db_relpath);

    op_res = sqlite3_open(db_path, &db);
    assert(op_res == SQLITE_OK);
    
    int fd;
    ark_setup_server(&fd, PORT);

    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    const SSL_METHOD *method = TLS_server_method();

    ctx = SSL_CTX_new(method);
    if (!ctx) {
        fprintf(stderr, "Failed to create SSL_CTX\n");
        ERR_print_errors_fp(stderr);
        assert(0);
    }

    char ssl_path[1024];
    
    char ssl_certificate_relpath[] = "../fullchain.pem";
    
    bzero(ssl_path, 1024);
    full_path(ssl_path, 1024, ssl_certificate_relpath);

    op_res = SSL_CTX_use_certificate_file(ctx, ssl_path, SSL_FILETYPE_PEM);
    if (op_res <= 0) {
        fprintf(stderr, "Failed to load certificate\n");
        ERR_print_errors_fp(stderr);
        assert(0);
    }

    char ssl_privatekey_relpath[] = "../privkey.pem";
    
    bzero(ssl_path, 1024);
    full_path(ssl_path, 1024, ssl_privatekey_relpath);

    op_res = SSL_CTX_use_PrivateKey_file(ctx, ssl_path, SSL_FILETYPE_PEM);
    if (op_res <= 0) {
        fprintf(stderr, "Failed to load private key\n");
        ERR_print_errors_fp(stderr);
        assert(0);
    }

    SSL_CTX_set_options(ctx, SSL_OP_IGNORE_UNEXPECTED_EOF);

    ark_task_create(handler, &fd, KB(8)); /** NOTE: Keep an eye that the stack size does not become a problem */
	ark_task_run_scheduler();
    
    /** NOTE: Code bellow will never run */

    sqlite3_close(db);
    
    SSL_CTX_free(ctx);
    EVP_cleanup();

    return 0;
}