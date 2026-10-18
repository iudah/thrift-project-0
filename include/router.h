#ifndef ROUTE_REQUESTS_H
#define ROUTE_REQUESTS_H

#include "book.h"
#include <stdint.h>

request_metadata_t *parse_request(char *message);

bool http_client_request(int fd, const char *method, const char *path,
                         const char *body, uint64_t body_len, char *response,
                         uint64_t response_cap, uint64_t *response_len);

char *http_find_body(char *response, uint64_t response_len);

bool route_request(char *message, char *buffer, node_context_t *ctx,
                   int connection_fd);

#endif
