#include <sqlite3.h>
#include <assert.h>

#include "ark/http.h"
#include "ark/cookies.h"
#include "ark/utils.h"

#include "helpers.h"
#include "../globals.h"

#define HasHeader(H)    (!!msg->headers[H].a)
#define HeaderData(H)   (request + msg->headers[H].a)
#define HeaderLength(H) (msg->headers[H].b - msg->headers[H].a)

int some_other_func(char *err, int n) {
    CHECK_ERR(n == 7, "some errr");
    
    return 0;
}

int is_authenticated(char *err, int *user_id, RequestCtx *rctx) {
    char *request = rctx->raw_request;
    struct HttpMessage *msg = &rctx->msg;
    Memory *memory = rctx->memory;

    int n;
    PROPAGATE_ERR(some_other_func(err, n), "");
    
    char *cookie = NULL;
    if (HasHeader(kHttpCookie)) {
        char *cookies = HeaderData(kHttpCookie);
        uint32_t cookies_length = HeaderLength(kHttpCookie);

        uint32_t count = 0;
        cookie_count(err, &count, cookies, cookies_length);

        Cookies parsed_cookies = {0};
        PROPAGATE_ERR(try_memory_alloc(err, (void **)&parsed_cookies.p, memory, count * sizeof(Cookie)), "Server at capacity");

        parse_cookies(&parsed_cookies, cookies, cookies_length);
        
        char *session_id = NULL;   
        uint32_t session_id_length = 0;     

        uint32_t i;
        for (i = 0; i < parsed_cookies.n; i++) {
            char *cookie_key = cookies + parsed_cookies.p[i].k.a;
            uint32_t cookie_key_length = parsed_cookies.p[i].k.b - parsed_cookies.p[i].k.a;
            
            if (SlicesEqual(cookie_key, cookie_key_length, "session_id", strlen("session_id"))) {
                session_id = cookies + parsed_cookies.p[i].v.a;
                session_id_length = parsed_cookies.p[i].v.b - parsed_cookies.p[i].v.a;
                break;
            }
        }

        if (!session_id_length) {
            return 0;
        }

        const char *sql = "SELECT user_id FROM sessions WHERE id = ? AND datetime('now') < expires_at;";
        sqlite3_stmt *stmt = NULL;

        sqlite3_prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, session_id, session_id_length, SQLITE_STATIC);

        if (sqlite3_step(stmt) != SQLITE_ROW) {
            assert(0);
        }
        
        *user_id = sqlite3_column_int(stmt, 0);

        sqlite3_finalize(stmt);
    }

    return 0;
}