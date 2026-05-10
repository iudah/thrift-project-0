#ifndef NODE_NET_H
#define NODE_NET_H

#include "book.h"
#include <arpa/inet.h>

typedef struct {
  char *name;
  char *ip;
  int port;
} peer_t;

bool create_socket(int *sock, char *address, int port, bool is_client_socket,
                   struct sockaddr_in *addr, int *addr_len);

bool peer_from_json(peer_t *peer, json_value *saved_peer, array_t *peers);

bool run_node(node_context_t *ctx, char *address, int port);

void stop_listening();

#endif
