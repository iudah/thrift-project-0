#ifndef ROUTE_REQUESTS_H
#define ROUTE_REQUESTS_H

#include "book.h"
#include <stdint.h>

request_metadata_t *parse_request(char *message);

bool route_request(char *message, char *buffer, node_context_t *ctx,
                   int connection_fd);

#endif
