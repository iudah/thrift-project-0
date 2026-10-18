#ifndef HTTP_HELPER_H
#define HTTP_HELPER_H

#include <Thrift-Project.h>
#include <arpa/inet.h>
#include <inttypes.h>
#include <linux/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <zot.h>

#include "book.h"
#include "ison.h"

#define ERR_MALFORMED_HEADER "{\"error\":\"Malformed header\"}"
#define ERR_MISSING_PAYLOAD  "{\"error\":\"Missing payload\"}"
#define ERR_INVALID_FORMAT   "{\"error\":\"Invalid message format\"}"
#define JSON_MEME            "application/json"
#define JSON_MEME_LEN        (16)
#define BLOCK_TIME           (10)

static bool send_json_error(int file_desc, const char *msg) {
  return http_message(file_desc, JSON_RESP, msg, strlen(msg), true, nullptr,
                      nullptr);
}

static bool require_json(request_metadata_t *meta, int conn_fd) {
  if (!meta->content_type ||
      strncmp(meta->content_type, JSON_MEME, JSON_MEME_LEN) != 0) {
    send_json_error(conn_fd, ERR_MALFORMED_HEADER);
    return false;
  }
  if (!meta->payload) {
    send_json_error(conn_fd, ERR_MISSING_PAYLOAD);
    return false;
  }
  return true;
}

static json_value *parse_json_body(request_metadata_t *meta, int conn_fd) {
  json_value *json = json_parse_string(meta->payload);
  if (!json || json_value_type(json) != DICT) {
    send_json_error(conn_fd, ERR_INVALID_FORMAT);
    return nullptr;
  }
  return json;
}

#endif
