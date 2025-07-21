#include <Thrift-Project.h>
#include <arpa/inet.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <zot.h>

#include "book.h"
#include "ds.h"
#include "ison.h"
#include "node_net.h"
#include "router.h"

#define BUFFER_SIZE 4096
static array_t *peers;

void set_handlers_peers(array_t *peer_list) { peers = peer_list; }

bool handle_balances(request_metadata_t *meta, char *buffer, book_t *book,
                     int connection_fd) {
  uint64_t len = snprintf(buffer, BUFFER_SIZE,
                          "{\n"
                          "  \"hash\": \"%s\",\n"
                          "  \"balances\": {\n",
                          book->block.parent_hash);
  uint32_t n_user = array_length(book->users);
  for (uint32_t i = 0; i < n_user; ++i) {
    char *account = array_get_ptr(book->users, i);
    map_value *q = hash_map_get(book->balances, account);
    len +=
        snprintf(buffer + len, BUFFER_SIZE - len, "    \"%s\": %f%s\n", account,
                 json_value_data(q).number, i < n_user - 1 ? "," : "");
  }
  len += snprintf(buffer + len, BUFFER_SIZE - len, "  }\n}\n");
  http_message(connection_fd, JSON_RESP, buffer, len, true, NULL, NULL);
  return true;
}

bool handle_status(request_metadata_t *meta, char *buffer, book_t *book,
                   int connection_fd) {
  int len = snprintf(buffer, BUFFER_SIZE,
                     "{\n"
                     "  \"hash\": \"%s\",\n"
                     "  \"block_count\": %i\n"
                     "}\n",
                     book->block.parent_hash, book->block.block_idx);
  http_message(connection_fd, JSON_RESP, buffer, len, true, NULL, NULL);
  return true;
}

bool handle_add_tx(request_metadata_t *meta, char *buffer, book_t *book,
                   int connection_fd) {
  if (!meta->content_type ||
      strncmp(meta->content_type, "application/json", 16)) {
    const char *msg = "{\"error\":\"Malformed header\"}";
    http_message(connection_fd, TEXT_RESP, msg, strlen(msg), true, NULL, NULL);
    return false;
  }
  if (!meta->payload) {
    const char *msg = "{\"error\":\"Missing payload\"}";
    http_message(connection_fd, TEXT_RESP, msg, strlen(msg), true, NULL, NULL);
    return false;
  }
  json_value *json = json_parse_string(meta->payload);
  if (!json || json_value_type(json) != DICT) {
    const char *err = "{\"error\":\"Invalid transaction format\"}";
    http_message(connection_fd, JSON_RESP, err, strlen(err), true, NULL, NULL);
    return true;
  }
  tx_t *tx = zcalloc(1, sizeof(*tx));
  if (!tx_from_json(tx, json, book)) {
    const char *err = "{\"error\":\"Transaction processing failed. "
                      "Verify signature and public key\"}";
    http_message(connection_fd, JSON_RESP, err, strlen(err), true, NULL, NULL);
    zfree(tx);
    return true;
  }
  array_append_ptr(book->block.txs, tx);
  char hash_bin[32];
  hash_block(book, NULL, NULL, hash_bin);
  int len =
      snprintf(buffer, BUFFER_SIZE, "{\"hash\":\"%s\",\"status\":\"pending\"}",
               hash_to_string((char *)hash_bin, buffer + 11, 65));
  if (difftime(time(NULL), book->last_block_time) > 10)
    save_block(book);
  http_message(connection_fd, JSON_RESP, buffer, len, true, NULL, NULL);
  return true;
}

bool handle_add_node(request_metadata_t *meta, char *buffer, book_t *book,
                     int connection_fd) {
  if (!meta->content_type ||
      strncmp(meta->content_type, "application/json", 16)) {
    const char *msg = "{\"error\":\"Malformed header\"}";
    http_message(connection_fd, JSON_RESP, msg, strlen(msg), true, NULL, NULL);
    return false;
  }
  if (!meta->payload) {
    const char *msg = "{\"error\":\"Missing payload\"}";
    http_message(connection_fd, JSON_RESP, msg, strlen(msg), true, NULL, NULL);
    return false;
  }
  json_value *json = json_parse_string(meta->payload);
  if (!json || json_value_type(json) != DICT) {
    const char *msg = "{\"error\":\"Invalid message format\"}";
    http_message(connection_fd, JSON_RESP, msg, strlen(msg), true, NULL, NULL);
    return true;
  }
  peer_t *peer = zcalloc(1, sizeof(*peer));
  if (!peer_from_json(peer, json, peers)) {
    const char *err = "{\"error\":\"Peer registration failed\"}";
    http_message(connection_fd, JSON_RESP, err, strlen(err), true, NULL, NULL);
    zfree(peer);
    return true;
  }
  array_append_ptr(peers, peer);
  int len = snprintf(buffer, BUFFER_SIZE,
                     "{\"name\":\"%s\",\"status\":\"success\"}", "lorem");
  http_message(connection_fd, JSON_RESP, buffer, len, true, NULL, NULL);
  return true;
}

bool handle_peers(request_metadata_t *meta, char *buffer, book_t *book,
                  int connection_fd) {
  if (!meta->payload) {
    const char *msg = "{\"error\":\"Missing payload\"}";
    http_message(connection_fd, TEXT_RESP, msg, strlen(msg), true, NULL, NULL);
    return false;
  }
  uint32_t len = snprintf(buffer, BUFFER_SIZE, "{\"peers\":[");
  uint32_t n_peers = array_length(peers);
  for (uint32_t i = 0; i < n_peers; ++i) {
    peer_t *peer = array_get_ptr(peers, i);
    len +=
        snprintf(buffer + len, BUFFER_SIZE - len,
                 "{\n"
                 "  \"ip\":\"%s\",\n"
                 "  \"port\":%i,\n"
                 "  \"name\":\"%s\"\n"
                 "}%s",
                 peer->ip, peer->port, peer->name, i < n_peers - 1 ? "," : "");
  }
  len += snprintf(buffer + len, BUFFER_SIZE - len, "]}\n");
  http_message(connection_fd, JSON_RESP, buffer, len, true, NULL, NULL);
  return true;
}

bool handle_exit(request_metadata_t *meta, char *buffer, book_t *book,
                 int connection_fd) {
  int len = snprintf(buffer, BUFFER_SIZE, "{\"status\": \"ended\"}\n");
  stop_listening();
  http_message(connection_fd, JSON_RESP, buffer, len, true, NULL, NULL);
  return true;
}

void reg_endpoints() {
  register_route("/balances", handle_balances);
  register_route("/status", handle_status);
  register_route("/add_tx", handle_add_tx);
  register_route("/add_node", handle_add_node);
  register_route("/peers", handle_peers);
  register_route("/exit", handle_exit);
}
