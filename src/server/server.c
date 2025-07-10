#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/epoll.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "../../include/ark/shared.h"
#include "../../include/ark/server.h"
#include "../../include/ark/memory.h"
#include "../memory/memory_internal.h"
#include "../utils.h"

#define REQUEST_BUFFER_SIZE_INCREMENT KB(4)
#define MAX_REQUEST_SIZE MB(300)
#define REQUEST_POOL_SIZE KB(1) /** This could also be SOMAXCONN or determined by the framework user */

#define HTTP_END_SEQUENCE 0x0A0D0A0D  /** "\r\n\r\n" in little-endian */

#define BLOCK_EXECUTION -1 /* In the context of epoll this puts the process to sleep. */
typedef struct {
    enum { SERVER_SOCKET, CLIENT_SOCKET } type;
    int fd;
} Socket;

int epoll_fd;
int nfds;
struct epoll_event events[REQUEST_POOL_SIZE];
struct epoll_event event;

Socket server_socket;

typedef struct {
    Socket socket; /** This should be the first in the struct */
    u32 pool_idx;
    u8 *buffer;
    size_t buffer_size;
    size_t received_bytes;
} RawHTTPRequest;

#define U32_BITS 32

RawHTTPRequest request_pool[REQUEST_POOL_SIZE];
u32 request_pool_availability_mask[REQUEST_POOL_SIZE / U32_BITS];

#define MASK_INDEX(i)       ((i) / U32_BITS)
#define BIT_POSITION(i)     ((i) % U32_BITS)

#define IS_AVAILABLE(i)     ((request_pool_availability_mask[MASK_INDEX(i)] & (1U << BIT_POSITION(i))) == 0)
#define SET_AVAILABLE(i)    (request_pool_availability_mask[MASK_INDEX(i)] &= ~(1U << BIT_POSITION(i)))
#define SET_OCCUPIED(i)     (request_pool_availability_mask[MASK_INDEX(i)] |=  (1U << BIT_POSITION(i)))

#define MAX2(a,b) ((a) > (b) ? (a) : (b))

#define MIN2(a,b) ((a) < (b) ? (a) : (b))
#define MIN3(a,b,c) (MIN2(a, MIN2(b,c)))

struct sockaddr_in client_addr; /** Why is this needed ?? */

volatile sig_atomic_t keep_running = true;

void sigint_handler(int signo) {
    if (signo == SIGINT) {
        printf("\nReceived SIGINT, exiting program...\n");
        keep_running = false;
    }
}

ArkError ark_server(uint16_t port) {
    ArkError error = {0};

    /* Registers a signal handler to ensure the program exits gracefully */
    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
        printf("Failed to set up signal handler for SIGINT\nError code: %d\n", errno);
        ASSERT(0);
    }

    epoll_fd = epoll_create1(0);
    ASSERT(epoll_fd != -1);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT(server_fd != -1);

    int server_fd_flags = fcntl(server_fd, F_GETFL, 0);
    ASSERT(fcntl(server_fd, F_SETFL, server_fd_flags | O_NONBLOCK) != -1);

    int server_fd_optname = 1;
    ASSERT(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &server_fd_optname, sizeof(int)) != -1);

    /** Configure server address */
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;             /** IPv4 */
    server_addr.sin_port = htons(port); /** Convert the port number from host byte order to network byte order (big-endian) */
    server_addr.sin_addr.s_addr = INADDR_ANY;     /** Listen on all available network interfaces (IPv4 addresses) */

    ASSERT(bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != -1);

    ASSERT(listen(server_fd, REQUEST_POOL_SIZE) != -1);

    server_socket.fd = server_fd;
    server_socket.type = SERVER_SOCKET;

    event.events = EPOLLIN;
    event.data.ptr = &server_socket;
    ASSERT(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_socket.fd, &event) != -1);

    printf("Server listening on port: %d...\n", (int)port);

    u32 i;
    for (i = 0; i < REQUEST_POOL_SIZE; i++) {
        request_pool[i].pool_idx = i;
    }

    return error;
}

int find_first_available_request_slot() {
    int mask_index, bit_index;

    for (mask_index = 0; mask_index < REQUEST_POOL_SIZE / U32_BITS; ++mask_index) {
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

ArkRequestResult ark_listen_and_serve(ArkResponse func(const char[])) {
    ArkRequestResult result = {0};

    nfds = epoll_wait(epoll_fd, events, REQUEST_POOL_SIZE, BLOCK_EXECUTION);

    if (keep_running == false) {
        return result;
    }

    ASSERT(nfds != -1);
    
    int i;
    for (i = 0; i < nfds; i++) {
        Socket *socket_info = (Socket *)events[i].data.ptr;

        switch (socket_info->type) {
            case SERVER_SOCKET: {
                if (events[i].events & EPOLLIN) { /** Server received new client request */   
                    socklen_t client_addr_len = sizeof(client_addr);

                    int client_fd = accept(socket_info->fd, (struct sockaddr *)&client_addr, &client_addr_len);
                    ASSERT(client_fd != -1);

                    int client_fd_flags = fcntl(client_fd, F_GETFL, 0);
                    ASSERT(fcntl(client_fd, F_SETFL, client_fd_flags | O_NONBLOCK) != -1);

                    int pos = find_first_available_request_slot();
                    SET_OCCUPIED(pos);
                    request_pool[pos].socket.type = CLIENT_SOCKET;
                    request_pool[pos].socket.fd = client_fd;

                    size_t buffer_size = REQUEST_BUFFER_SIZE_INCREMENT;
                    request_pool[pos].buffer_size = buffer_size;
                    request_pool[pos].buffer = malloc(buffer_size * sizeof(char));

                    event.events = EPOLLIN | EPOLLET;
                    event.data.ptr = &request_pool[pos].socket;
                    ASSERT(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) != -1);

                    break;
                }

                printf("Server socket only should receive EPOLLIN events\n");
                ASSERT(0);

                break;
            }

            case CLIENT_SOCKET: {
                if (events[i].events & EPOLLIN) { /** Data available for read in client socket buffer */
                    RawHTTPRequest *request = (RawHTTPRequest *)socket_info;
                    
                    int client_socket = socket_info->fd;

                    /** 
                     * LEFT OFF HERE
                     * TODO, Use memory arena for request (alloc maybe 1KB) and if request is multipart/form-data we 
                     * are dealing with a file upload in which case we need to increase the size of the arena allocated
                     * to the size of the request message (start line, headers and body).
                     */

                    /**
                     * It is important to call recv repeatedly until we receive EWOULDBLOCK,
                     * effectively reading all available data in the buffer. This is because we want 
                     * to ensure that we read all data available in the I/O buffer. When using edge-triggered
                     * notifications, we will not receive another notification for data that was already
                     * ready but not read, only for newly arriving data.
                     * 
                     * KEEP IN MIND: A large I/O operation on a single descriptor has the potential to 
                     * starve other descriptors. 
                     */
                    
                    /* HttpMessage msg = {0}; */

                    while (1) {
                        /**
                        * IMPORTANT!
                        * It is implied that under no circumstances do I expect to receive requests from
                        * the client that exceed MAX_REQUEST_SIZE. If that were the case, the send
                        * operation should be placed in a while loop, sending until EWOULDBLOCK is returned,
                        * and then resumed later to continue sending.
                        */

                        size_t remaining_allowed = MAX_REQUEST_SIZE - request->received_bytes;
                        size_t max = MIN2(REQUEST_BUFFER_SIZE_INCREMENT, remaining_allowed);
                        ssize_t got = recv(client_socket, &request->buffer[request->received_bytes], max, 0);
                        
                        if (got > 0) {
                            request->received_bytes += (size_t)got;
                            printf("%lu\n", request->received_bytes);
                        }

                        /** Increase request buffer size */
                        /*
                        if (request->received_bytes >= request->buffer_size) {
                        }
                        */
                        request->buffer_size += REQUEST_BUFFER_SIZE_INCREMENT;
                        request->buffer = realloc(request->buffer, request->buffer_size);
     
                        if (got <= 0) {
                            if (!got) {
                                printf("client disconnected\n");
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

                                /** TODO: CHECK IF HTTP MESSAGE HAS BEEN FULLY RECEIVED WITH \r\n\r\n */

                                /** Full HTTP headers received */
                                /*
                                uint32_t *end = (uint32_t *)(&request->buffer[request->received_bytes - 4]);
                                if (*end == HTTP_END_SEQUENCE) {
                                    break;
                                }
                                */
                            }

                            if (request->received_bytes >= (231701558+219)) {
                                break;
                            } else {
                                goto request_cleanup;
                            }
                        }

                        if (!(request->received_bytes <= MAX_REQUEST_SIZE)) {
                            ASSERT(0);
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

                        if (request->received_bytes >= (231701558+219)) {
                            break;
                        }
                    }

                    printf("received %lu\n", request->received_bytes);

                    char r[] =  "HTTP/1.1 200 OK\r\n"
                                "Content-Type: text/html\r\n"
                                "Content-Length: 49\r\n"
                                "Connection: close\r\n"
                                "\r\n"
                                "<html><body><h1>Hello, World!</h1></body></html>\n";

                    ArkResponse response = {0};

                    /* ark_begin_profile(); */
                    /*
                    response = func(raw_request);
                    */
                    /* ark_end_and_print_profile(); */

                    response.content = r;
                    response.length = strlen(r);

                    /**
                     * IMPORTANT!
                     * It is implied that under no circumstances do I want to send an amount of
                     * data that exceeds the write buffer capacity. If that were the case, the send
                     * operation should be placed in a while loop, sending until EWOULDBLOCK is returned,
                     * and then resumed later to continue sending.
                     */
                    if (send(client_socket, response.content, response.length, 0) == -1) {
                        /* TODO */
                    }

                    close(client_socket);
                   
                    request_cleanup:;
                    break;
                }

                printf("Client socket only should receive EPOLLIN events\n");
                ASSERT(0);

                break;
            }

            default: {
                ASSERT(0);

                break;
            }
        }
    }

    return result;
}
