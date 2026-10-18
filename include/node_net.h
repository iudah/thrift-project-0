#ifndef NODE_NET_H
#define NODE_NET_H

#include <arpa/inet.h>

bool create_socket(int *sock, char *address, int port, bool is_client_socket,
                   struct sockaddr_in *addr, int *addr_len);

#endif
