#include <fcntl.h>
#include <netdb.h>
#include <assert.h>

#include "../../include/ark/shared.h"
#include "../../include/ark/server.h"

int ark_client_accept(int *client_fd, int fd) {
    int _; /** Used to check for error */

    int sock_fd = accept(fd, NULL, NULL); /** Ignore the address where the request came from (for now) */
    assert(sock_fd != -1);

    int sock_fd_flags = fcntl(sock_fd, F_GETFL, 0);
    _ = fcntl(sock_fd, F_SETFL, sock_fd_flags | O_NONBLOCK);
    assert(_ != -1);

    *client_fd = sock_fd;

    return 0;
}

int ark_setup_server(int *fd, uint16_t port) {
    int _; /** Used to check for error */

    /* Registers a signal handler to ensure the program exits gracefully */
    
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

    _ = listen(sock_fd, 1000);
    assert(_ != -1);

    *fd = sock_fd;

    printf("Server listening on port: %d...\n", (int)port);

    return 0;
}
