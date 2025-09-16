#define _XOPEN_SOURCE 700  // Required for strptime on Linux

#include <stdlib.h>
#include <argon2.h>
#include <uuid/uuid.h>
#include <time.h>
#include <assert.h>

#include "ark/url.h"
#include "ark/http.h"
#include "ark/utils.h"

#include "routes.h"
#include "../globals.h"

void login(void *v) {
    RequestCtx *rctx = (RequestCtx *)v;

    uint32_t index = 0;
    find_element_index(&index, component_name_arr, "login", component_arr_length);
    char *component = component_content_arr[index];
    uint32_t component_length = strlen(component);
    assert(component_length);

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

void login_create_session(void *v) {
    RequestCtx *rctx = (RequestCtx *)v;
    char *request = rctx->raw_request;
    struct HttpMessage *msg = rctx->msg;

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

    const char *sql = "SELECT id, password FROM users WHERE email = ?;";
    sqlite3_stmt *stmt = NULL;

    sqlite3_prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, email, email_length, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        assert(0);
    }

    int user_id = sqlite3_column_int(stmt, 0);
    const char *hashed_password = (const char *)sqlite3_column_text(stmt, 1);

    if (argon2i_verify(hashed_password, password, password_length) != ARGON2_OK) {
        printf("Failed to verify password\n");
        assert(0);
    }

    sqlite3_finalize(stmt);

    uuid_t uuid;
    char session_id[37];
    uuid_generate(uuid);
    uuid_unparse_lower(uuid, session_id);

    printf("Generated UUID: %s\n", session_id);

    sql = "INSERT OR REPLACE INTO sessions (id, user_id, expires_at, updated_at) "
          "VALUES (?, ?, datetime('now', '+1 month'), datetime('now')) "
          "RETURNING expires_at;";
    
    sqlite3_prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, session_id, strlen(session_id), SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, user_id);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        assert(0);
    }

    const unsigned char *expires_at = sqlite3_column_text(stmt, 0);
    
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

    char response[KB(8)];
    bzero(response, KB(8));
    sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Set-Cookie: session_id=%s; Path=/; HttpOnly; Secure; SameSite=Strict; Expires=%s\r\n"
            "Content-Length: 0\r\n"
            "HX-Redirect: /\r\n\r\n",
            session_id, http_date);
    
    int response_length = strlen(response);
    ark_fd_write(rctx->ssl, rctx->fd, response, response_length);
    
    SSL_shutdown(rctx->ssl);
    SSL_free(rctx->ssl);
    close(rctx->fd);
    memory_free(rctx->memory);
}