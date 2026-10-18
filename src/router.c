#include <Thrift-Project.h>
#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <zot.h>

#include "ds.h"
#include "ison.h"
#include "node_context.h"
#include "router.h"

#define BUFFER_SIZE 2048

hash_map *endpoints = nullptr;

bool register_route(char *endpoint, endpoint_handler_t handler) {
  if (!endpoints) {
    endpoints = create_hash_map();
  }
  return hash_map_add_ptr(endpoints, endpoint, handler);
}

request_metadata_t *parse_request(char *message) {
  request_metadata_t meta = {nullptr};
  meta.method             = strtok(message, " ");
  meta.path               = strtok(nullptr, " ");
  if (!meta.path) {
    return nullptr;
  }

  message           = meta.path + strlen(meta.path) + 1;
  meta.content_type = strstr(message, "Content-Type: ");
  if (meta.content_type) {
#define CONTENT_TYPE_STRLEN 14
    meta.content_type += CONTENT_TYPE_STRLEN;
    char *end = strstr(meta.content_type, "\r\n");
    if (end) {
      *end = '\0';
    }
    meta.content_type = zstrdup(meta.content_type);
    if (end)
      *end = '\r';
  }

  char *payload_start = strstr(message, "\r\n\r\n");
  if (payload_start)
    meta.payload = payload_start + 4;

  request_metadata_t *meta_ptr = zmalloc(sizeof(meta));
  memcpy(meta_ptr, &meta, sizeof(meta));

  return meta_ptr;
}

bool http_message(int file_descriptor, response_t type, const char *msg,
                  uint64_t msg_len, bool is_server_resp, char *method,
                  char *endpoint) {
  if (file_descriptor < 0) {
    LOG("Invalid file descriptor");
    return false;
  }

  char         headers[BUFFER_SIZE];
  const time_t now       = time(nullptr);
  struct tm   *atom_time = gmtime(&now);
#define DATE_LEN 35
  char date[DATE_LEN];
  strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S %Z", atom_time);

  char *message = zcalloc(msg_len + 1, 1);
  memcpy(message, msg, msg_len);

  const int headers_len = snprintf(
      headers, BUFFER_SIZE,
      "%s%s%s%sHTTP/1.1%s\r\n"
      "%s: Thrift/%d.%d.%d\r\n"
      "Date: %s\r\n"
      "Content-Length: %" PRIu64 "\r\n"
      "Content-Type: %s\r\n"
      "Connection: close\r\n"
      "\r\n%s",
      is_server_resp ? "" : method, is_server_resp ? "" : " ",
      is_server_resp ? "" : endpoint, is_server_resp ? "" : " ",
      is_server_resp ? " 200 OK" : "", is_server_resp ? "Server" : "User-Agent",
      THRIFT_PROJECT_VERSION_MAJOR, THRIFT_PROJECT_VERSION_MINOR,
      THRIFT_PROJECT_VERSION_PATCH, date, msg_len,
      (type == JSON_RESP) ? "application/json" : "text/plain", msg);

  bool success = true;

  ssize_t sent = send(file_descriptor, headers, headers_len, MSG_NOSIGNAL);
  if (sent < 0) {
    if (errno != EPIPE && errno != ECONNRESET) {
      LOG("Response send failed (fd %d): %s", file_descriptor, strerror(errno));
    }
    success = false;
  }

  // if (success) {
  //   sent = send(fd, msg, msg_len, MSG_NOSIGNAL);
  //   if (sent < 0) {
  //     if (errno != EPIPE && errno != ECONNRESET) {
  //       LOG("Body send failed (fd %d): %s", fd, strerror(errno));
  //     }
  //     success = false;
  //   }
  // }

  if (shutdown(file_descriptor, SHUT_WR) < 0 && errno != ENOTCONN) {
    LOG("Shutdown failed (fd %d): %s", file_descriptor, strerror(errno));
  }

  return success;
}

bool http_client_request(int fd, const char *method, const char *path,
                         const char *body, uint64_t body_len, char *response,
                         uint64_t response_cap, uint64_t *response_len) {
  if (fd < 0 || !method || !path || !response || response_cap < 2)
    return false;

  char request[BUFFER_BIG];
  int  request_len;

  if (body && body_len > 0) {
    request_len = snprintf(request, sizeof(request),
                           "%s %s HTTP/1.1\r\n"
                           "Host: thrift-node\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: %" PRIu64 "\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           method, path, body_len, body);
  } else {
    request_len = snprintf(request, sizeof(request),
                           "%s %s HTTP/1.1\r\n"
                           "Host: thrift-node\r\n"
                           "Connection: close\r\n"
                           "\r\n",
                           method, path);
  }

  if (request_len <= 0 || request_len >= (int)sizeof(request))
    return false;

  uint64_t sent_total = 0;
  while (sent_total < (uint64_t)request_len) {
    ssize_t n =
        send(fd, request + sent_total, request_len - sent_total, MSG_NOSIGNAL);
    if (n <= 0)
      return false;
    sent_total += n;
  }

  uint64_t received_total = 0;
  while (received_total < response_cap - 1) {
    ssize_t n =
        recv(fd, response + received_total, response_cap - 1 - received_total, 0);
    if (n < 0)
      return false;
    if (n == 0)
      break;
    received_total += n;
  }
  response[received_total] = '\0';
  if (response_len)
    *response_len = received_total;

  return received_total > 0;
}

char *http_find_body(char *response, uint64_t response_len) {
  if (!response || response_len < 4)
    return nullptr;
  for (uint64_t i = 0; i + 3 < response_len; ++i) {
    if (response[i] == '\r' && response[i + 1] == '\n' &&
        response[i + 2] == '\r' && response[i + 3] == '\n') {
      return response + i + 4;
    }
  }
  return nullptr;
}

bool route_request(char *message, char *buffer, node_context_t *ctx,
                   int connection_fd) {
  request_metadata_t *meta = parse_request(message);
  if (!meta) {
    const char *err = "{\"error\":\"Malformed request\"}";
    http_message(connection_fd, JSON_RESP, err, strlen(err), true, nullptr,
                 nullptr);
    return false;
  }

  map_value *endpt = hash_map_get(endpoints, meta->path);
  if (!endpt) {
    const char *err = "{\"error\":\"Invalid endpoint\"}";
    http_message(connection_fd, JSON_RESP, err, strlen(err), true, nullptr,
                 nullptr);
    return false;
  }

  ((endpoint_handler_t)json_value_data(endpt).ptr)(meta, ctx, buffer,
                                                   connection_fd);

  return true;
}
