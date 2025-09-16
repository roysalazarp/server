#include <string.h>

#include "ark/utils.h"

#include "routes.h"
#include "../globals.h"

void logout(void *v)  {
    RequestCtx *rctx = (RequestCtx *)v;

    int user_id = 0;
    is_authenticated(&user_id, rctx);

    if (user_id) {
        const char *sql = "DELETE FROM sessions WHERE user_id = ?;";
        sqlite3_stmt *stmt = NULL;

        sqlite3_prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL);
        sqlite3_bind_int(stmt, 1, user_id);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            printf("did't delete record\n");
        }

        sqlite3_finalize(stmt);
    }

    char response[KB(8)];
    bzero(response, KB(8));
    sprintf(response,
           "HTTP/1.1 200 OK\r\n"
           "Set-Cookie: session_id=deleted; Path=/; HttpOnly; Secure; SameSite=Strict; Expires=Thu, 01 Jan 1970 00:00:00 GMT\r\n"
           "Content-Length: 0\r\n"
           "HX-Refresh: true\r\n\r\n");
    
    int response_length = strlen(response);
    ark_fd_write(rctx->ssl, rctx->fd, response, response_length);
    
    SSL_shutdown(rctx->ssl);
    SSL_free(rctx->ssl);
    close(rctx->fd);
    memory_free(rctx->memory);
}
