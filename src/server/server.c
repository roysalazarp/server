#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/epoll.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "../../include/ark/shared.h"
#include "../../include/ark/server.h"
#include "../utils.h"

/**
 * ERROR HANDLING
 *
 * Errors can be categorized into two types: Technical errors and Actionable errors.
 *
 * Technical errors: Errors that should NOT occur in practice and that the library user 
 * could report as a bug, for example, an error when calling the epoll_create1 function. 
 * These are handled with asserts.
 *
 * Actionable errors: Errors that the library user can take action to fix, for example, 
 * when making an API call to a third-party service and the service is down. These errors
 * should be handled in whatever way makes the most sense to inform the library user and
 * gracefully handle the situation.
 * 
 * When functions return an int:
 * -1 for Technical error
 *  0 success
 * >0 error code
 */

#ifndef ARK_MAX_HTTP_REQUEST_SIZE
#define ARK_MAX_HTTP_REQUEST_SIZE MB(400)
#endif

#ifndef ARK_MAX_QUEUED_HTTP_REQUESTS
#define ARK_MAX_QUEUED_HTTP_REQUESTS KB(1) /** could also be SOMAXCONN */
#endif

#define MASK_4        0xFFFFFFFF00000000
#define MASK_5        0xFFFFFFFFFF000000
#define MASK_6        0xFFFFFFFFFFFF0000
#define MASK_7        0xFFFFFFFFFFFFFF00
#define MASK_8        0xFFFFFFFFFFFFFFFF

#define GET           0x4745542000000000
#define HEAD          0x4845414420000000
#define TRACE         0x5452414345200000
#define CONNECT       0x434F4E4E45435420
#define PUT           0x5055542000000000
#define POST          0x504F535420000000
#define PATCH         0x5041544348200000
#define DELETE        0x44454C4554452000
#define OPTIONS       0x4F5054494F4E5320

#define READ64LE(P)                    \
  (__extension__({                     \
    uint64_t __x;                      \
    memcpy(&__x, P, 64 / 8);           \
    __builtin_bswap32(__x);            \
  }))

#define READ64BE(P)                    \
  (__extension__({                     \
    uint64_t __x;                      \
    memcpy(&__x, P, 64 / 8);           \
    __builtin_bswap64(__x);            \
  }))

#define END_OF_HTTP_MESSAGE_HEADERS 0x0A0D0A0D /** "\n\r\n\r" in ASCII */

#define MAX_BYTES_TO_READ_FROM_SOCKET_BUFFER_AT_ONCE KB(1)

#define MIN2(a,b) ((a) < (b) ? (a) : (b))

typedef struct {
    enum { SERVER_SOCKET, CLIENT_SOCKET, API_SOCKET } type;
    int fd; /** File descriptor */
} SocketInfo;

typedef struct {
    SocketInfo socket_info; /** IMPORTANT: SocketInfo should be the first member in the struct */
    RawHTTPMessage client_request;
    struct {
        jmp_buf resumable_ctx; /** 
                                * Stores the program state at the point where a call to an external 
                                * server is made. Used to resume execution from this point once the 
                                * server response is received. 
                                */
        int number_of_requests_sent;
        int number_of_responses_received;
        RawHTTPMessage *responses;
    } fetch;
    u32 pool_pos;
} HTTPRequestContext;

typedef struct {
    SocketInfo socket_info; /** IMPORTANT: SocketInfo should be the first member in the struct */
    int pool_pos;
    int id; /** TODO: Explain why this is necessary */
} FetchContext;

#define U32_BITS 32

HTTPRequestContext *request_pool_ptrs[ARK_MAX_QUEUED_HTTP_REQUESTS];
u32 request_pool_availability_mask[ARK_MAX_QUEUED_HTTP_REQUESTS / U32_BITS];

#define MASK_INDEX(i)       ((i) / U32_BITS)
#define BIT_POSITION(i)     ((i) % U32_BITS)

#define SET_POOL_SLOT_AS_AVAILABLE(i)    (request_pool_availability_mask[MASK_INDEX(i)] &= ~(1U << BIT_POSITION(i)))
#define SET_POOL_SLOT_AS_OCCUPIED(i)     (request_pool_availability_mask[MASK_INDEX(i)] |=  (1U << BIT_POSITION(i)))

/** Epoll stuff */
int epoll_fd;
int nfds;
struct epoll_event events[ARK_MAX_QUEUED_HTTP_REQUESTS];
struct epoll_event event;

SocketInfo server_socket;

jmp_buf event_loop; /** Used to jump the program execution back to the
                        epoll event loop after making a fetch request */

volatile sig_atomic_t keep_running = true;

void sigint_handler(int signo) {
    if (signo == SIGINT) {
        printf("\nReceived SIGINT, exiting program...\n");
        keep_running = false;
    }
}

int ark_server_init(uint16_t port) {
    /** TODO: Validate passed arguments */

    int _; /** Used to check for error */

    /* Registers a signal handler to ensure the program exits gracefully */
    sig_t sig = signal(SIGINT, sigint_handler);
    assert(sig != SIG_ERR);

    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(sock_fd != -1);

    int sock_fd_flags = fcntl(sock_fd, F_GETFL, 0);
    assert(sock_fd_flags != -1);

    _ = fcntl(sock_fd, F_SETFL, sock_fd_flags | O_NONBLOCK);
    assert(_ != -1);

    int sock_fd_optname = 1;
    _ = setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &sock_fd_optname, sizeof(int));
    assert(_ != -1);

    /** Configure server address */
    struct sockaddr_in sock_addr;
    sock_addr.sin_family = AF_INET;             /** IPv4 */
    sock_addr.sin_port = htons(port);           /** Convert the port number from host byte order to network byte order (big-endian) */
    sock_addr.sin_addr.s_addr = INADDR_ANY;     /** Listen on all available network interfaces (IPv4 addresses) */

    _ = bind(sock_fd, (struct sockaddr *)&sock_addr, sizeof(sock_addr));
    assert(_ != -1);

    _ = listen(sock_fd, ARK_MAX_QUEUED_HTTP_REQUESTS);
    assert(_ != -1);

    server_socket.fd = sock_fd;
    server_socket.type = SERVER_SOCKET;

    epoll_fd = epoll_create1(0);
    assert(epoll_fd != -1);

    event.events = EPOLLIN;
    event.data.ptr = &server_socket;
    _ = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_socket.fd, &event);
    assert(_ != -1);

    printf("Server listening on port: %d...\n", (int)port);

    return 0;
}

int ark_send_response(int handle, char *response, size_t response_length) {
    /** TODO: Validate passed arguments */

    if (send(request_pool_ptrs[handle]->socket_info.fd, response, response_length, 0) == -1) {}
    close(request_pool_ptrs[handle]->socket_info.fd);

    free(request_pool_ptrs[handle]->client_request.buffer);
    int i = 0;
    for (i = 0; i < request_pool_ptrs[handle]->fetch.number_of_responses_received; i++) {
        free(request_pool_ptrs[handle]->fetch.responses[i].buffer);
    }
    
    free(request_pool_ptrs[handle]);
    request_pool_ptrs[handle] = NULL;
    
    SET_POOL_SLOT_AS_AVAILABLE(handle);

    return 0;
}

int ark_fetch(ArkFetchResponse *response, int handle, ArkFetchTaskDetails *tasks, int tasks_count) {    
    /** TODO: Validate passed arguments */

    int _;
    request_pool_ptrs[handle]->fetch.responses = malloc(tasks_count * sizeof(RawHTTPMessage));
    assert(request_pool_ptrs[handle]->fetch.responses != NULL);

    int i = 0;
    for (i = 0; i < tasks_count; i++) {
        bzero(&request_pool_ptrs[handle]->fetch.responses[i], sizeof(RawHTTPMessage));
        request_pool_ptrs[handle]->fetch.responses[i].buffer_size = MAX_BYTES_TO_READ_FROM_SOCKET_BUFFER_AT_ONCE;
        request_pool_ptrs[handle]->fetch.responses[i].buffer = malloc(MAX_BYTES_TO_READ_FROM_SOCKET_BUFFER_AT_ONCE * sizeof(char));
        assert(request_pool_ptrs[handle]->fetch.responses[i].buffer != NULL);
        bzero(request_pool_ptrs[handle]->fetch.responses[i].buffer, MAX_BYTES_TO_READ_FROM_SOCKET_BUFFER_AT_ONCE);
    }

    int st = setjmp(request_pool_ptrs[handle]->fetch.resumable_ctx);
    if (st == 0) {
        int i = 0;
        for (i = 0; i < tasks_count; i++) {
            ArkFetchTaskDetails task = tasks[i];

            struct addrinfo hints, *res;

            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_UNSPEC;      /** Use IPv4 or IPv6 */
            hints.ai_socktype = SOCK_STREAM;  /** TCP */

            _ = getaddrinfo(task.domain_name, "http", &hints, &res);
            assert(_ != -1);

            int sock_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
            assert(sock_fd != -1);

            struct sockaddr_in sock_addr;
            sock_addr.sin_family = AF_INET;         /** IPv4 */
            sock_addr.sin_port = htons(task.port);  /** Convert the port number from host byte order to network byte order (big-endian) */

            /** non-blocking (using fcntl) should be set after connect so it doesn't block */
            _ = connect(sock_fd, (struct sockaddr *)&sock_addr, sizeof(sock_addr));
            assert(_ != -1);
                
            int sock_fd_flags = fcntl(sock_fd, F_GETFL, 0);
            assert(fcntl(sock_fd, F_SETFL, sock_fd_flags | O_NONBLOCK) != -1);
            
            FetchContext *fetch_ctx = malloc(sizeof(FetchContext));
            assert(fetch_ctx != NULL);

            fetch_ctx->socket_info.fd = sock_fd;
            fetch_ctx->socket_info.type = API_SOCKET;
            fetch_ctx->pool_pos = handle;
            fetch_ctx->id = i;

            event.events = EPOLLIN;
            event.data.ptr = &fetch_ctx->socket_info;
            assert(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fetch_ctx->socket_info.fd, &event) != -1);

            if (send(sock_fd, task.request, task.request_length, 0) == -1) {}

            request_pool_ptrs[handle]->fetch.number_of_requests_sent++;
        }

        longjmp(event_loop, 1); /** return to event loop */
    }

    response->count = request_pool_ptrs[st]->fetch.number_of_responses_received;
    response->responses = request_pool_ptrs[st]->fetch.responses;
    
    return 0;
}

int ark_retrive_request(ArkRequest *ark_request) {    
    int _;
    begining:;

    setjmp(event_loop); /** Point of return */
    
    if (nfds == 0) { /** We've handled all events received in the last epoll_wait call */
        /** Check if more notifications available */
        nfds = epoll_wait(epoll_fd, events, ARK_MAX_QUEUED_HTTP_REQUESTS, -1 /** -1 to put the process to sleep */);
    }

    if (keep_running == false) {
        return ARK_SHUT_SERVER_SIGNAL;
    }

    assert(nfds != -1);

    while (nfds) {
        nfds--;
        SocketInfo *socket_info = (SocketInfo *)events[nfds].data.ptr;

        switch (socket_info->type) {
            case SERVER_SOCKET: {
                if (events[nfds].events & EPOLLIN) { /** Server received new client request */   
                    int _; /** Used to check for error */

                    int sock_fd = accept(socket_info->fd, NULL, NULL); /** Ignore the address where the request came from (for now) */
                    assert(sock_fd != -1);

                    int sock_fd_flags = fcntl(sock_fd, F_GETFL, 0);
                    _ = fcntl(sock_fd, F_SETFL, sock_fd_flags | O_NONBLOCK);
                    assert(_ != -1);

                    SET_POOL_SLOT_AS_OCCUPIED(0); /** IMPORTANT! Do not use pool slot 0 because of setjmp/longjmp */
                    
                    int pos = -1;
                    int mask_index, bit_index;
                    for (mask_index = 0; mask_index < ARK_MAX_QUEUED_HTTP_REQUESTS / U32_BITS; ++mask_index) {
                        u32 mask_entry = request_pool_availability_mask[mask_index];
                        
                        /** If all bits are set (0xFFFFFFFF), no free slots here; skip */
                        if (mask_entry != 0xFFFFFFFFU) {
                            for (bit_index = 0; bit_index < U32_BITS; ++bit_index) {
                                /** Check if bit is 0 (available) */
                                if ((mask_entry & (1U << bit_index)) == 0) {
                                    pos = mask_index * U32_BITS + bit_index;
                                    goto found;
                                }
                            }
                        }
                    }

                    assert(pos != -1);

                found:;
                    SET_POOL_SLOT_AS_OCCUPIED(pos);

                    request_pool_ptrs[pos] = malloc(sizeof(HTTPRequestContext));
                    assert(request_pool_ptrs[pos] != NULL);

                    bzero(request_pool_ptrs[pos], sizeof(HTTPRequestContext));

                    request_pool_ptrs[pos]->socket_info.type = CLIENT_SOCKET;
                    request_pool_ptrs[pos]->socket_info.fd = sock_fd;
                    
                    request_pool_ptrs[pos]->pool_pos = pos;

                    request_pool_ptrs[pos]->client_request.buffer_size = MAX_BYTES_TO_READ_FROM_SOCKET_BUFFER_AT_ONCE;
                    request_pool_ptrs[pos]->client_request.buffer = malloc(MAX_BYTES_TO_READ_FROM_SOCKET_BUFFER_AT_ONCE * sizeof(char));
                    assert(request_pool_ptrs[pos]->client_request.buffer != NULL);
                    bzero(request_pool_ptrs[pos]->client_request.buffer, MAX_BYTES_TO_READ_FROM_SOCKET_BUFFER_AT_ONCE);

                    event.events = EPOLLIN | EPOLLET;
                    event.data.ptr = &request_pool_ptrs[pos]->socket_info;
                    _ = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &event);
                    assert(_ != -1);

                    break;
                }

                printf("%s\n", "Server socket only should receive EPOLLIN events\n");
                assert(0);

                break;
            }

            case CLIENT_SOCKET: {
                if (events[nfds].events & EPOLLIN) { /** Data available for read in client socket buffer */
                    HTTPRequestContext *ctx = (HTTPRequestContext *)socket_info;
                    
                    while (true) {
                        size_t remaining_allowed = ARK_MAX_HTTP_REQUEST_SIZE - ctx->client_request.received_bytes;
                        size_t max_read = MIN2(MAX_BYTES_TO_READ_FROM_SOCKET_BUFFER_AT_ONCE, remaining_allowed);

                        ssize_t got = recv(socket_info->fd, &ctx->client_request.buffer[ctx->client_request.received_bytes], max_read, 0);
                        
                        if (!ctx->client_request.headers_checked) { /** Checking the HTTP request headers helps us determine how many bytes to expect in the request */
                            boolean has_content_length = false;
                            u64 content_length = 0;
                            size_t cursor = 0;
                            while (*((uint32_t *)(&ctx->client_request.buffer[cursor])) != END_OF_HTTP_MESSAGE_HEADERS) {
                                if (memcmp(&ctx->client_request.buffer[cursor], "Content-Length", strlen("Content-Length")) == 0) {
                                    cursor += strlen("Content-Length");

                                    has_content_length = true;

                                    while (ctx->client_request.buffer[cursor] == ' ' || ctx->client_request.buffer[cursor] == ':') {
                                        cursor++;
                                    }

                                    while (!isspace(ctx->client_request.buffer[cursor])) {
                                        if (!is_digit((unsigned char)ctx->client_request.buffer[cursor])) {
                                            assert(0);
                                        }

                                        content_length = content_length * 10 + (ctx->client_request.buffer[cursor] - '0');

                                        cursor++;
                                    }
                                } else {
                                    cursor++;
                                }
                            }

                            cursor += 4; /* skip END_OF_HTTP_MESSAGE_HEADERS "\n\r\n\r" */
                            ctx->client_request.headers_checked = true;

                            /** At this point we know the length of the headers and we know the Content-Length if there is one */

                            u64 http_method = *(u64 *)ctx->client_request.buffer;

                            if (((__builtin_bswap64(http_method) & MASK_4) == GET)   ||
                                ((__builtin_bswap64(http_method) & MASK_5) == HEAD)  ||
                                ((__builtin_bswap64(http_method) & MASK_6) == TRACE) ||
                                ((__builtin_bswap64(http_method) & MASK_8) == CONNECT)) 
                            {
                                /** HTTP requests with these methods can NOT contain a Content-Length header */
                                assert(!has_content_length);
                            } else if (((__builtin_bswap64(http_method) & MASK_4) == PUT)  ||
                                       ((__builtin_bswap64(http_method) & MASK_5) == POST) ||
                                       ((__builtin_bswap64(http_method) & MASK_6) == PATCH)) 
                            {
                                /** HTTP requests with these methods MUST contain a Content-Length header */
                                assert(has_content_length);
                            } else if (((__builtin_bswap64(http_method) & MASK_7) == DELETE) ||
                                       ((__builtin_bswap64(http_method) & MASK_8) == OPTIONS)) 
                            {
                                /** There MAY be a Content-Length header */
                            }
                            
                            size_t presice_request_size = cursor + content_length;
                            assert(presice_request_size <= ARK_MAX_HTTP_REQUEST_SIZE);

                            if (presice_request_size != ctx->client_request.buffer_size) {
                                ctx->client_request.buffer_size = presice_request_size;
                                ctx->client_request.buffer = realloc(ctx->client_request.buffer, ctx->client_request.buffer_size);
                                printf("reallocating: %lu\n", ctx->client_request.buffer_size);
                            }
                        }

                        if (got > 0) {
                            ctx->client_request.received_bytes += (size_t)got;
                            printf("received_bytes so far: %lu\n", ctx->client_request.received_bytes);
                        } else {
                            if (!got) {
                                printf("Client disconnected\n");
                            } else if (errno == EAGAIN || errno == EWOULDBLOCK) { 
                                perror(NULL);
                            }
                            
                            if (ctx->client_request.received_bytes == ctx->client_request.buffer_size) {
                                /** Full HTTP message received */
                                break;
                            } else {
                                goto begining;
                            }
                        }

                        if (ctx->client_request.received_bytes == ctx->client_request.buffer_size) {
                            break;
                        }
                    }

                    printf("received %lu\n", ctx->client_request.received_bytes);
                                          
                    ark_request->raw = NULL;
                    ark_request->length = 0;
                    ark_request->handle = ctx->pool_pos;
                    
                    return 0;
                }

                printf("%s\n", "Client socket only should receive EPOLLIN events\n");
                assert(0);

                break;
            }

            case API_SOCKET: {
                /** NOTE, For now lets assume that all http responses have a Content-Length and a body */
                
                FetchContext *fetch_ctx = (FetchContext *)socket_info;
                int pos = fetch_ctx->pool_pos;
                HTTPRequestContext *request = request_pool_ptrs[pos];
                RawHTTPMessage *response = &request->fetch.responses[fetch_ctx->id];

                int sock_fd = socket_info->fd;

                while (true) {
                    size_t remaining_allowed = ARK_MAX_HTTP_REQUEST_SIZE - response->received_bytes;
                    size_t max_read = MIN2(MAX_BYTES_TO_READ_FROM_SOCKET_BUFFER_AT_ONCE, remaining_allowed);
                        
                    ssize_t got = recv(sock_fd, &response->buffer[response->received_bytes], max_read, 0);
                    
                    if (!response->headers_checked) {
                        boolean has_content_length = false;
                        u64 content_length = 0;
                        size_t cursor = 0;
                        while (*((uint32_t *)(&response->buffer[cursor])) != END_OF_HTTP_MESSAGE_HEADERS) {
                            if (memcmp(&response->buffer[cursor], "Content-Length", strlen("Content-Length")) == 0) {
                                cursor += strlen("Content-Length");

                                has_content_length = true;

                                while (response->buffer[cursor] == ' ' || response->buffer[cursor] == ':') {
                                    cursor++;
                                }

                                while (!isspace(response->buffer[cursor])) {
                                    if (!is_digit((unsigned char)response->buffer[cursor])) {
                                        assert(0);
                                    }

                                    content_length = content_length * 10 + (response->buffer[cursor] - '0');

                                    cursor++;
                                }
                            } else {
                                cursor++;
                            }
                        }

                        cursor += 4; /* skip END_OF_HTTP_MESSAGE_HEADERS "\n\r\n\r" */
                        response->headers_checked = true;

                        size_t presice_request_size = cursor + content_length;
                        assert(presice_request_size <= ARK_MAX_HTTP_REQUEST_SIZE);

                        if (presice_request_size != response->buffer_size) {
                            response->buffer_size = presice_request_size;
                            response->buffer = realloc(response->buffer, response->buffer_size);
                            printf("reallocating: %lu\n", response->buffer_size);
                        }
                    }

                    if (got > 0) {
                        response->received_bytes += (size_t)got;
                        printf("received_bytes so far: %lu\n", response->received_bytes);
                    } else {
                        if (!got) {
                            printf("client disconnected\n");
                        } else if (errno == EAGAIN || errno == EWOULDBLOCK) { 
                            perror(NULL);
                        }
                        
                        if (response->received_bytes == response->buffer_size) {
                            /** Full HTTP message received */
                            request->fetch.number_of_responses_received++;

                            _ = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sock_fd, NULL);
                            assert(_ != -1);

                            close(sock_fd);
                            free(fetch_ctx);

                            break;
                        } else {
                            goto begining;
                        }
                    }

                    if (response->received_bytes == response->buffer_size) {
                        break;
                    }
                }

                if (request->fetch.number_of_responses_received == request->fetch.number_of_requests_sent) {
                    longjmp(request_pool_ptrs[pos]->fetch.resumable_ctx, pos);
                }
                
                break;
            }
            
            default: {
                assert(0);

                break;
            }
        }
    }

    goto begining;
}
