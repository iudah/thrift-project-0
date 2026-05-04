#ifndef ROUTE_REQUESTS_H
#define ROUTE_REQUESTS_H

#include "node_context.h"
#include <stdint.h>

typedef enum { JSON_RESP, TEXT_RESP } response_t;

typedef struct {
  char *method;
  char *path;
  char *content_type;
  char *payload;
} request_metadata_t;

typedef bool (*endpoint_handler_t)(request_metadata_t *meta,
                                   node_context_t *ctx, char *buffer,
                                   int connection_fd);

bool register_route(char *endpoint, endpoint_handler_t handler);

request_metadata_t *parse_request(char *message);

bool http_message(int fd, response_t type, const char *msg, uint64_t msg_len,
                  bool is_server_resp, char *method, char *endpoint);

bool route_request(char *message, char *buffer, node_context_t *ctx,
                   int connection_fd);

#endif
