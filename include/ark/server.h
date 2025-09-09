#ifndef ARK_SERVER_H
#define ARK_SERVER_H

#include <stdio.h>
#include <stdint.h>

int ark_setup_server(int *fd, uint16_t port);
int ark_client_accept(int *client_fd, int fd);

#endif /* ARK_SERVER_H */