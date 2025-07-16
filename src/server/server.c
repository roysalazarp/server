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
 * NOTE: The goal is to write code that minimizes failure points. For example, instead
 * of calling malloc multiple times to allocate resources when a new request comes in,
 * a buffer should be allocated with sufficient memory upfront. If there is a special
 * case where the buffer is too small, then address that specific case appropriately.
 */

#ifndef ARK_MAX_HTTP_REQUEST_SIZE
#define ARK_MAX_HTTP_REQUEST_SIZE MB(400)
#endif

#ifndef ARK_MAX_QUEUED_HTTP_REQUESTS
#define ARK_MAX_QUEUED_HTTP_REQUESTS KB(1) /** could also be SOMAXCONN */
#endif

#define MASK_3        0xFFFFFF0000000000
#define MASK_4        0xFFFFFFFF00000000
#define MASK_5        0xFFFFFFFFFF000000
#define MASK_6        0xFFFFFFFFFFFF0000
#define MASK_7        0xFFFFFFFFFFFFFF00

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
} FetchHandle;

#define U32_BITS 32

HTTPRequestContext request_pool[ARK_MAX_QUEUED_HTTP_REQUESTS];
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

struct sockaddr_in client_addr; /** NOTE: Why is this needed ?? */

volatile sig_atomic_t keep_running = true;

void sigint_handler(int signo) {
    if (signo == SIGINT) {
        printf("\nReceived SIGINT, exiting program...\n");
        keep_running = false;
    }
}

/** 
 * LEFT OFF HERE, refactoring and commenting code to make it easier to understand,
 * also remember to get rid of malloc as much as possible!
 */

int ark_server_init(uint16_t port) {
    /* Registers a signal handler to ensure the program exits gracefully */
    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
        printf("Failed to set up signal handler for SIGINT\nError code: %d\n", errno);
        ASSERT(0);
    }

    epoll_fd = epoll_create1(0);
    ASSERT(epoll_fd != -1);

    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT(sock_fd != -1);

    int sock_fd_flags = fcntl(sock_fd, F_GETFL, 0);
    ASSERT(fcntl(sock_fd, F_SETFL, sock_fd_flags | O_NONBLOCK) != -1);

    int sock_fd_optname = 1;
    ASSERT(setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &sock_fd_optname, sizeof(int)) != -1);

    /** Configure server address */
    struct sockaddr_in sock_addr;
    sock_addr.sin_family = AF_INET;             /** IPv4 */
    sock_addr.sin_port = htons(port); /** Convert the port number from host byte order to network byte order (big-endian) */
    sock_addr.sin_addr.s_addr = INADDR_ANY;     /** Listen on all available network interfaces (IPv4 addresses) */

    ASSERT(bind(sock_fd, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) != -1);

    ASSERT(listen(sock_fd, ARK_MAX_QUEUED_HTTP_REQUESTS) != -1);

    server_socket.fd = sock_fd;
    server_socket.type = SERVER_SOCKET;

    event.events = EPOLLIN;
    event.data.ptr = &server_socket;
    ASSERT(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_socket.fd, &event) != -1);

    printf("Server listening on port: %d...\n", (int)port);

    u32 i;
    for (i = 0; i < ARK_MAX_QUEUED_HTTP_REQUESTS; i++) {
        request_pool[i].pool_pos = i;
    }

    return 0;
}

int find_first_available_request_slot() {
    int mask_index, bit_index;

    for (mask_index = 0; mask_index < ARK_MAX_QUEUED_HTTP_REQUESTS / U32_BITS; ++mask_index) {
        u32 mask_entry = request_pool_availability_mask[mask_index];
        
        /** If all bits are set (0xFFFFFFFF), no free slots here; skip */
        if (mask_entry != 0xFFFFFFFFU) {
            for (bit_index = 0; bit_index < U32_BITS; ++bit_index) {
                /** Check if bit is 0 (available) */
                if ((mask_entry & (1U << bit_index)) == 0) {
                    return mask_index * U32_BITS + bit_index;
                }
            }
        }
    }

    return -1;
}

void ark_send_response(int handle, char *response, size_t response_length) {
    printf("\n");

    if (send(request_pool[handle].socket_info.fd, response, response_length, 0) == -1) {}
    close(request_pool[handle].socket_info.fd);

    free(request_pool[handle].client_request.buffer);
    int i = 0;
    for (i = 0; i < request_pool[handle].fetch.number_of_responses_received; i++) {
        free(request_pool[handle].fetch.responses[i].buffer);
    }
    
    /** Clear everything except the pool_pos */
    int pool_pos = request_pool[handle].pool_pos;
    bzero(&request_pool[handle], sizeof(HTTPRequestContext));
    request_pool[handle].pool_pos = pool_pos;
    
    SET_POOL_SLOT_AS_AVAILABLE(handle);
}

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

FetchResponse ark_fetch(int handle, ArkFetchTaskDetails *tasks, int count) {    
    request_pool[handle].fetch.responses = malloc(count * sizeof(RawHTTPMessage));

    int i = 0;
    for (i = 0; i < count; i++) {
        bzero(&request_pool[handle].fetch.responses[i], sizeof(RawHTTPMessage));
        request_pool[handle].fetch.responses[i].buffer_size = MAX_BYTES_TO_READ_FROM_SOCKET_BUFFER_AT_ONCE;
        request_pool[handle].fetch.responses[i].buffer = malloc(MAX_BYTES_TO_READ_FROM_SOCKET_BUFFER_AT_ONCE * sizeof(char));
        bzero(request_pool[handle].fetch.responses[i].buffer, MAX_BYTES_TO_READ_FROM_SOCKET_BUFFER_AT_ONCE);
    }
        
    int st = setjmp(request_pool[handle].fetch.resumable_ctx);
    if (st == 0) {
        int i = 0;
        for (i = 0; i < count; i++) {
            ArkFetchTaskDetails task = tasks[i];

            struct addrinfo hints, *res;

            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_UNSPEC;      /** Use IPv4 or IPv6 */
            hints.ai_socktype = SOCK_STREAM;  /** TCP */

            ASSERT(getaddrinfo(task.domain_name, "http", &hints, &res) == 0);

            int sock_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
            ASSERT(sock_fd != -1);

            struct sockaddr_in sock_addr;
            sock_addr.sin_family = AF_INET;   /** IPv4 */
            sock_addr.sin_port = htons(3000); /** Convert the port number from host byte order to network byte order (big-endian) */

            /** 
             * If I set the socket as non-blocking before calling connect, the socket would block
             * that's why I set it to non-blocking after, but, try to set it up as non-blocking 
             * before calling connect and see how can I manage with setjmp and longjmp
             */
            ASSERT(connect(sock_fd, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) != -1);

            int sock_fd_flags = fcntl(sock_fd, F_GETFL, 0);
            ASSERT(fcntl(sock_fd, F_SETFL, sock_fd_flags | O_NONBLOCK) != -1);
            
            FetchHandle *fetch_handle = malloc(sizeof(FetchHandle));

            fetch_handle->socket_info.fd = sock_fd;
            fetch_handle->socket_info.type = API_SOCKET;
            fetch_handle->pool_pos = handle;
            fetch_handle->id = i;

            event.events = EPOLLIN;
            event.data.ptr = &fetch_handle->socket_info;
            ASSERT(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fetch_handle->socket_info.fd, &event) != -1);

            if (send(sock_fd, task.request, task.request_length, 0) == -1) {}

            request_pool[handle].fetch.number_of_requests_sent++;

            printf("\n");
        }

        longjmp(event_loop, 1); /** return to event loop */
    }

    printf("\n");

    FetchResponse r = {0};
    r.count = request_pool[st].fetch.number_of_responses_received;
    r.responses = request_pool[st].fetch.responses;
    
    return r;
}

ArkRequest ark_retrive_request() {    
    begining:;

    if (setjmp(event_loop) == 0) {
        printf("setjmp(event_loop)\n");
    }
    
    if (!nfds) {
        nfds = epoll_wait(epoll_fd, events, ARK_MAX_QUEUED_HTTP_REQUESTS, -1 /** -1 to put the process to sleep */);
    }

    if (keep_running == false) {
        ASSERT(0);
    }

    ASSERT(nfds != -1);

    while (nfds) {
        nfds--;
        SocketInfo *socket_info = (SocketInfo *)events[nfds].data.ptr;

        switch (socket_info->type) {
            case SERVER_SOCKET: {
                if (events[nfds].events & EPOLLIN) { /** Server received new client request */   
                    socklen_t client_addr_len = sizeof(client_addr);

                    int sock_fd = accept(socket_info->fd, (struct sockaddr *)&client_addr, &client_addr_len);
                    ASSERT(sock_fd != -1);

                    int sock_fd_flags = fcntl(sock_fd, F_GETFL, 0);
                    ASSERT(fcntl(sock_fd, F_SETFL, sock_fd_flags | O_NONBLOCK) != -1);

                    SET_POOL_SLOT_AS_OCCUPIED(0); /** VERY IMPORTANT!!! Do not use pool element 0 because of setjmp/longjmp */
                    
                    int pos = find_first_available_request_slot();
                    SET_POOL_SLOT_AS_OCCUPIED(pos);
                    request_pool[pos].socket_info.type = CLIENT_SOCKET;
                    request_pool[pos].socket_info.fd = sock_fd;

                    request_pool[pos].client_request.buffer_size = MAX_BYTES_TO_READ_FROM_SOCKET_BUFFER_AT_ONCE;
                    request_pool[pos].client_request.buffer = malloc(MAX_BYTES_TO_READ_FROM_SOCKET_BUFFER_AT_ONCE * sizeof(char));
                    bzero(request_pool[pos].client_request.buffer, MAX_BYTES_TO_READ_FROM_SOCKET_BUFFER_AT_ONCE);

                    event.events = EPOLLIN | EPOLLET;
                    event.data.ptr = &request_pool[pos].socket_info;
                    ASSERT(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &event) != -1);

                    break;
                }

                printf("Server socket only should receive EPOLLIN events\n");
                ASSERT(0);

                break;
            }

            case CLIENT_SOCKET: {
                if (events[nfds].events & EPOLLIN) { /** Data available for read in client socket buffer */
                    HTTPRequestContext *request = (HTTPRequestContext *)socket_info;
                    
                    int sock_fd = socket_info->fd;
                    
                    while (true) {
                        /**
                         * It is important to call recv repeatedly until we receive EWOULDBLOCK,
                         * effectively reading all available data in the buffer. This is because we want 
                         * to ensure that we read all data available in the I/O buffer. When using edge-triggered
                         * notifications, we will not receive another notification for data that was already
                         * ready but not read, only for newly arriving data.
                         * 
                         * KEEP IN MIND, a large I/O operation on a single descriptor has the potential to 
                         * starve other descriptors. 
                         */

                        size_t remaining_allowed = ARK_MAX_HTTP_REQUEST_SIZE - request->client_request.received_bytes;
                        
                        size_t max = MIN2(MAX_BYTES_TO_READ_FROM_SOCKET_BUFFER_AT_ONCE, remaining_allowed);
                        ssize_t got = recv(sock_fd, &request->client_request.buffer[request->client_request.received_bytes], max, 0);
                        
                        if (!request->client_request.headers_checked) {
                            boolean has_content_length = false;
                            u64 content_length = 0;
                            size_t cursor = 0;
                            while (*((uint32_t *)(&request->client_request.buffer[cursor])) != 0x0A0D0A0D) { /** Check for "\r\n\r\n" s*/
                                if (memcmp(&request->client_request.buffer[cursor], "Content-Length", strlen("Content-Length")) == 0) { /** Check for Content-Length */
                                    char *p = (char *)&request->client_request.buffer[cursor];
                                    cursor += strlen("Content-Length");

                                    has_content_length = true;

                                    while (request->client_request.buffer[cursor] == ' ' || request->client_request.buffer[cursor] == ':') {
                                        cursor++;
                                    }

                                    printf("\n");

                                    while (!isspace(request->client_request.buffer[cursor])) {
                                        if (!is_digit((unsigned char)request->client_request.buffer[cursor])) {
                                            ASSERT(0);
                                        }

                                        content_length = content_length * 10 + (request->client_request.buffer[cursor] - '0');

                                        /** Prevent overflow — 56-bit max value as a safety ceiling */
                                        if (content_length > 0x000000ffffffffffLL) {
                                            ASSERT(0);
                                        }

                                        cursor++;
                                    }

                                    printf("\n");
                                } else {
                                    cursor++;
                                }
                            }

                            cursor += strlen("\r\n\r\n");

                            request->client_request.headers_checked = true;

                            /** At this point we know the length of the headers and we know the Content-Length if there is one */
                            
                            printf("\n");
                            
                            if (((READ64BE(request->client_request.buffer) & MASK_3) == READ64BE("GET\0\0\0\0")) ||
                                ((READ64BE(request->client_request.buffer) & MASK_4) == READ64LE("HEAD\0\0\0"))  ||
                                ((READ64BE(request->client_request.buffer) & MASK_5) == READ64LE("TRACE\0\0"))   ||
                                ((READ64BE(request->client_request.buffer) & MASK_7) == READ64LE("CONNECT")))
                            {
                                /** Check for "\r\n\r\n", there CAN NOT be Content-Length header */
                                if (has_content_length) {
                                    ASSERT(0);
                                }
                            } else if (((READ64BE(request->client_request.buffer) & MASK_3) ==  READ64BE("PUT\0\0\0\0")) ||
                                       ((READ64BE(request->client_request.buffer) & MASK_4) ==  READ64BE("POST\0\0\0"))  ||
                                       ((READ64BE(request->client_request.buffer) & MASK_5) ==  READ64BE("PATCH\0\0")))
                            {
                                /** There MUST be a Content-Length header */
                                if (!has_content_length) {
                                    ASSERT(0);
                                }
                            } else if (((READ64BE(request->client_request.buffer) & MASK_6) ==  READ64BE("DELETE\0")) ||
                                       ((READ64BE(request->client_request.buffer) & MASK_7) ==  READ64BE("OPTIONS")))
                            {
                                printf("\n");
                                /** There MAY be a Content-Length header */
                            }

                            size_t presice_request_size = cursor + content_length;
                            if (presice_request_size != request->client_request.buffer_size) {
                                request->client_request.buffer_size = presice_request_size;
                                request->client_request.buffer = realloc(request->client_request.buffer, request->client_request.buffer_size);
                                printf("reallocating: %lu\n", request->client_request.buffer_size);
                            }

                            printf("\n");
                        }

                        /** NOTE
                         *  - When the same header is present multiple times in the http request we only consider the last one, the user should only send one header and separate the content with comas.
                         *  - Standard and custom http headers should be register in the "perfect hash table".
                         *  - POST, PUT and PATCH methods must have a Content-Length header, other http methods are not allowed to have a body.
                         *  - Should I accept POST, PUT and PATCH methods without a Content-Length header but a Transfer-Encoding "chunked"?
                         * 
                         *  NOTE, All http request messages (start line + headers) end with "\r\n\r\n" and after that is the start of the body
                         */
                        if (got > 0) {
                            request->client_request.received_bytes += (size_t)got;
                            printf("received_bytes so far: %lu\n", request->client_request.received_bytes);
                        } else {
                            if (!got) {
                                printf("+-+-client disconnected\n");
                            } else if (errno == EAGAIN || errno == EWOULDBLOCK) { 
                                printf("recv error: %s\n", strerror(errno));
                                /**
                                * NOTE
                                * When handling large requests that don't fit entirely in the I/O buffer (e.g., when a user uploads a large file),
                                * `epoll` may notify us multiple times for the same file descriptor as new data arrives.
                                * 
                                * This means that even if we've read all currently available data and encounter `EWOULDBLOCK`,
                                * the full request may not have been received yet. More data may still be incoming as the client
                                * continues writing to the I/O buffer.
                                * 
                                * Therefore, we must not assume that reading the I/O buffer to completion indicates the entire request has been received,
                                * nor should we prematurely process the HTTP request. Instead, we should store the partial data in a separate buffer,
                                * continue accumulating incoming data, and only process the request once it has been fully received.
                                */   
                            }
                            
                            /** Full HTTP headers received */
                            if (request->client_request.received_bytes == request->client_request.buffer_size) {
                                printf("\n");
                                break;
                            }
                        }

                        /** 
                        * HTTP request needs to be parsed at this point.
                        * Memory arena size might need increasing depending on the request size, for example,
                        * when a large file is uploaded we would need to check for the content length header
                        * to see how big is the file and resize the arena accordingly.
                        * 
                        * The framework user might want to say whether they accept file uploads at all, or only
                        * on certain endpoints or a limit size of the file upload. We can for example exit the 
                        * request early if user is not allowed to upload file.
                        */
                    }

                    printf("received %lu\n", request->client_request.received_bytes);
                                                           
                    ArkRequest rout = {0};
                    rout.handle = request->pool_pos;
                    return rout;
                }

                printf("Client socket only should receive EPOLLIN events\n");
                ASSERT(0);

                break;
            }

            case API_SOCKET: {
                /** NOTE, For now lets assume that all http responses have a Content-Length and a body */
                
                FetchHandle *fetch_handle = (FetchHandle *)socket_info;
                int j = fetch_handle->pool_pos;
                HTTPRequestContext *request = &request_pool[j];
                RawHTTPMessage *response = &request->fetch.responses[fetch_handle->id];

                int sock_fd = socket_info->fd;

                /** TODO, read api response with recv */
                while (true) {
                    size_t remaining_allowed = ARK_MAX_HTTP_REQUEST_SIZE - response->received_bytes;
                        
                    size_t max = MIN2(MAX_BYTES_TO_READ_FROM_SOCKET_BUFFER_AT_ONCE, remaining_allowed);
                    ssize_t got = recv(sock_fd, &response->buffer[response->received_bytes], max, 0);
                    
                    if (!response->headers_checked) {
                        boolean has_content_length = false;
                        u64 content_length = 0;
                        size_t cursor = 0;
                        while (*((uint32_t *)(&response->buffer[cursor])) != 0x0A0D0A0D) { /** Check for "\r\n\r\n" s*/
                            if (memcmp(&response->buffer[cursor], "Content-Length", strlen("Content-Length")) == 0) { /** Check for Content-Length */
                                char *p = (char *)&response->buffer[cursor];
                                cursor += strlen("Content-Length");

                                has_content_length = true;

                                while (response->buffer[cursor] == ' ' || response->buffer[cursor] == ':') {
                                    cursor++;
                                }

                                printf("\n");

                                while (!isspace(response->buffer[cursor])) {
                                    if (!is_digit((unsigned char)response->buffer[cursor])) {
                                        ASSERT(0);
                                    }

                                    content_length = content_length * 10 + (response->buffer[cursor] - '0');

                                    /** Prevent overflow — 56-bit max value as a safety ceiling */
                                    if (content_length > 0x000000ffffffffffLL) {
                                        ASSERT(0);
                                    }

                                    cursor++;
                                }

                                printf("\n");
                            } else {
                                cursor++;
                            }
                        }

                        cursor += strlen("\r\n\r\n");

                        response->headers_checked = true;

                        size_t presice_request_size = cursor + content_length;
                        if (presice_request_size != response->buffer_size) {
                            response->buffer_size = presice_request_size;
                            response->buffer = realloc(response->buffer, response->buffer_size);
                            printf("reallocating: %lu\n", response->buffer_size);
                        }

                        printf("\n");
                    }

                    if (got > 0) {
                        response->received_bytes += (size_t)got;
                        printf("received_bytes so far: %lu\n", response->received_bytes);
                    } else {
                        if (!got) {
                            printf("*/*/client disconnected\n");

                            /*
                            break;
                            */
                        } else if (errno == EAGAIN || errno == EWOULDBLOCK) { 
                            printf("recv error: %s\n", strerror(errno));
                        }
                        
                        if (got == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
                            printf("recv error: %s\n", strerror(errno));
                        }
                        
                        if (response->received_bytes == response->buffer_size) {
                            printf("\n");
                            request->fetch.number_of_responses_received++;

                            ASSERT(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sock_fd, NULL) != -1);
                            close(sock_fd);
                            free(fetch_handle);

                            break;
                        }
                    }
                }

                if (request->fetch.number_of_responses_received == request->fetch.number_of_requests_sent) {
                    longjmp(request_pool[j].fetch.resumable_ctx, j);
                }
                
                printf("\n");
                
                break;
            }
            
            default: {
                ASSERT(0);

                break;
            }
        }
    }

    goto begining;
}
