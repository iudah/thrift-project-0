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

hash_map *endpoints = NULL;

bool register_route(char *endpoint, endpoint_handler_t handler) {
  if (!endpoints) {
    endpoints = create_hash_map();
  }
  return hash_map_add_ptr(endpoints, endpoint, handler);
}

request_metadata_t *parse_request(char *message) {
  request_metadata_t meta = {0};
  meta.method = strtok(message, " ");
  meta.path = strtok(NULL, " ");

  message = meta.path + strlen(meta.path) + 1;
  meta.content_type = strstr(message, "Content-Type: ");
  if (meta.content_type) {
#define CONTENT_TYPE_STRLEN 14
    meta.content_type += CONTENT_TYPE_STRLEN;
    char *end = strstr(meta.content_type, "\r\n");
    if (end) {
      *end = '\0';
    }
    meta.content_type = zstrdup(meta.content_type);
    *end = '\r';
  }

  char *payload_start = strstr(message, "\r\n\r\n");
  if (payload_start)
    meta.payload = payload_start + 4;

  request_metadata_t *meta_ptr = zmalloc(sizeof(meta));
  memcpy(meta_ptr, &meta, sizeof(meta));

  return meta_ptr;
}

bool http_message(int fd, response_t type, const char *msg, uint64_t msg_len,
                  bool is_server_resp, char *method, char *endpoint) {
  if (fd < 0) {
    LOG("Invalid file descriptor");
    return false;
  }

  char headers[BUFFER_SIZE];
  const time_t now = time(NULL);
  struct tm *tm = gmtime(&now);
  char date[35];
  strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S %Z", tm);

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

  ssize_t sent = send(fd, headers, headers_len, MSG_NOSIGNAL);
  if (sent < 0) {
    if (errno != EPIPE && errno != ECONNRESET) {
      LOG("Response send failed (fd %d): %s", fd, strerror(errno));
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

  if (shutdown(fd, SHUT_WR) < 0 && errno != ENOTCONN) {
    LOG("Shutdown failed (fd %d): %s", fd, strerror(errno));
  }

  return success;
}

bool route_request(char *message, char *buffer, node_context_t *ctx,
                   int connection_fd) {
  request_metadata_t *meta = parse_request(message);

  map_value *q = hash_map_get(endpoints, meta->path);
  if (!q) {
    const char *err = "{\"error\":\"Invalid endpoint\"}";
    http_message(connection_fd, JSON_RESP, err, strlen(err), true, NULL, NULL);
    return false;
  }

  ((endpoint_handler_t)json_value_data(q).ptr)(meta, ctx, buffer,
                                               connection_fd);

  return true;
}
