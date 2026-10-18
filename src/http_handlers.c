#include <Thrift-Project.h>
#include <arpa/inet.h>
#include <inttypes.h>
#include <linux/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <zot.h>

#include "book.h"
#include "ds.h"
#include "genesis_header.h"
#include "http_helper.h"
#include "ison.h"
#include "node_net.h"

void set_handlers_peers(array_t *peer_list) {
  // peers = peer_list;
}

static void broadcast_peer(peer_t *peer, char *payload) {
  int server_fd;
  struct sockaddr_in addr;
  int addr_len;
  if (create_socket(&server_fd, peer->ip, peer->port, true, &addr, &addr_len))
    return;
  http_message(server_fd, JSON_RESP, payload, strlen(payload), false, nullptr,
               nullptr);
  shutdown(server_fd, SHUT_RDWR);
  close(server_fd);
}

void *bc_peer_async(void *arg) {
  struct {
    array_t *peers;
    void *payload;
  } *args = arg;
  array_t *peers = args->peers;
  void *payload = args->payload;
  uint32_t n_peers = array_length(peers);
  for (uint32_t i = 0; i < n_peers; ++i) {
    broadcast_peer(array_get_ptr(peers, i), payload);
  }
  return nullptr;
}

bool handle_add_node(request_metadata_t *meta, node_context_t *ctx,
                     char *__attribute__((unused)) buffer, int connection_fd) {
  (void)ctx;

  if (!require_json(meta, connection_fd))
    return false;
  json_value *json = parse_json_body(meta, connection_fd);
  if (!json)
    return true;

  peer_t *peer = zcalloc(1, sizeof(*peer));
  if (!peer_from_json(peer, json)) {
    send_json_error(connection_fd, "{\"error\":\"Peer registration failed\"}");
    zfree(peer);
    return true;
  }

  bool duplicate_node = false;
  uint32_t n_peers = array_length(ctx->peers);
  uint32_t peer_ip_length = strlen(peer->ip);
  for (uint32_t i = 0; i < n_peers; ++i) {
    peer_t *existing = array_get_ptr(ctx->peers, i);
    if (strncmp(existing->ip, peer->ip, peer_ip_length) == 0 &&
        existing->port == peer->port) {
      duplicate_node = true;
    }
  }

  if (!duplicate_node) {
    array_append_ptr(ctx->peers, peer);

    char peer_buffer[BUFFER_SMALL + 1] = {0};
    auto payload_len = snprintf(peer_buffer, BUFFER_SMALL,
                                "{\n"
                                "  \"ip\":\"%s\",\n"
                                "  \"port\":%i,\n"
                                "  \"name\":\"%s\"\n"
                                "}",
                                peer->ip, peer->port, peer->name);

    // Broadcast the new node in new thread
    pthread_t node_bc_thread;
    struct arg {
      array_t *peers;
      void *payload;
    } arg = {ctx->peers, zcalloc(payload_len + 1, 1)};

    memcpy(arg.payload, peer_buffer, payload_len);

    struct arg *args = zcalloc(1, sizeof(struct arg));
    *args = arg;

    zthread_create(&node_bc_thread, nullptr, bc_peer_async, args);
  }

  char resp[BUFFER_SMALL] = {0};
  snprintf(resp, BUFFER_SMALL, "{\"name\":\"%s\",\"status\":\"success\"}",
           peer->name);
  http_message(connection_fd, JSON_RESP, resp, strlen(resp), true, nullptr,
               nullptr);
  return true;
}

bool handle_add_tx(request_metadata_t *meta, node_context_t *ctx, char *buffer,
                   int connection_fd) {
  if (!require_json(meta, connection_fd))
    return false;
  json_value *json = parse_json_body(meta, connection_fd);
  if (!json)
    return true;

  book_t *book = &ctx->book;
  tx_t *txn = zcalloc(1, sizeof(*txn));

  if (!tx_from_json(txn, json) || !is_valid_tx(txn) ||
      !effect_tx(book, book->users, txn)) {
    send_json_error(connection_fd,
                    "{\"error\":\"Transaction processing failed. "
                    "Verify signature and public key\"}");
    zfree(txn);
    return true;
  }

  array_append_ptr(book->block.txs, txn);

  char hash_hex[SHA256_HEX_LEN + 1];
  hash_block(&book->block, hash_hex);

  int len = snprintf(buffer, BUFFER_SMALL,
                     "{\"hash\":\"%s\",\"status\":\"pending\"}", hash_hex);

  if (difftime(time(nullptr), book->last_block_time) > BLOCK_TIME)
    save_block(book, book->block.miner);

  http_message(connection_fd, JSON_RESP, buffer, len, true, nullptr, nullptr);
  return true;
}

static uint32_t append_str(char *buffer, uint32_t content_len, uint32_t limit,
                           char *new_str, ...) {
  if (content_len > limit)
    return limit;
  va_list args;
  va_start(args, new_str);
  auto new_str_len =
      vsnprintf(buffer + content_len, limit - content_len, new_str, args);
  va_end(args);
  return content_len + new_str_len;
}

bool handle_sync(request_metadata_t *meta, node_context_t *ctx, char *buffer,
                 int connection_fd) {
  if (!require_json(meta, connection_fd))
    return false;
  json_value *json = parse_json_body(meta, connection_fd);
  if (!json)
    return true;

  char block_hash_hex[SHA256_HEX_BUF_LEN + 1] = {0};
  if (!requested_block_hex(json, block_hash_hex)) {
    send_json_error(connection_fd, "{\"error\":\"Missing block hash\"}");
    return true;
  }

  char genesis_hash[SHA256_HEX_BUF_LEN];
  hash_buffer((char *)genesis_json, strlen(genesis_json), genesis_hash);
  bool requested_is_genesis =
      strncmp(block_hash_hex, genesis_hash, SHA256_HEX_LEN) == 0;

#define INIT_MSG_LEN 11
  char message[BUFFER_BIG * 2] = "{\"blocks\":[";
  auto message_len = INIT_MSG_LEN;
  char head_block[SHA256_HEX_BUF_LEN + 1] = {0};
  char hash_out[SHA256_HEX_BUF_LEN];
  strncpy(head_block, ctx->book.block.parent_hash, SHA256_HEX_BUF_LEN);
  auto home_path = get_thrift_home();
  auto blocks_path = join_path(home_path, "blocks");
  array_t *hashpaths = list_blocks(blocks_path, head_block);
  if (!hashpaths) {
    send_json_error(connection_fd, "{\"error\":\"Could not read blocks\"}");
    return true;
  }
  auto len = array_length(hashpaths);

  bool succeed = true;
  bool first_block_found = requested_is_genesis;

  for (uint32_t i = len; i-- > 0;) {
    char *hashjson = array_get_ptr(hashpaths, i);

    if (!first_block_found) {
      if (strncmp(hashjson, block_hash_hex, SHA256_HEX_LEN) != 0)
        continue;
      first_block_found = true;
      continue;
    }

    auto block_path = join_path(blocks_path, hashjson);
    auto blockjson = fopen(block_path, "rb");
    if (!blockjson) {
      succeed = false;
      break;
    }

    char *blocks_buffer = zcalloc(2 * BUFFER_BIG, 1);
    auto read_size = fread(blocks_buffer, 1, 2 * BUFFER_BIG, blockjson);
    blocks_buffer[read_size] = 0;

    fclose(blockjson);

    if (read_size >= ((2 * BUFFER_BIG) - message_len - 3)) {
      succeed = true;
      break;
    }

    hash_buffer(blocks_buffer, read_size, hash_out);

    if (strncmp(hash_out, hashjson, SHA256_HEX_LEN)) {
      succeed = false;
      break;
    }

    if (message_len > INIT_MSG_LEN) {
      message_len = append_str(message, message_len, 2 * BUFFER_BIG, ",");
    }
    message_len =
        append_str(message, message_len, 2 * BUFFER_BIG, "%s", blocks_buffer);
  }

  if (!succeed) {
    send_json_error(connection_fd, "{\"error\":\"Sync failed\"}");
    return true;
  }
  message_len = append_str(message, message_len, 2 * BUFFER_BIG, "]}");

  http_message(connection_fd, JSON_RESP, message, message_len, true, nullptr,
               nullptr);
  return true;
}

bool handle_balances(request_metadata_t *meta, node_context_t *ctx,
                     char *buffer, int connection_fd) {
  (void)meta;
  uint64_t len = append_str(buffer, 0, BUFFER_SMALL,
                            "{\n"
                            "  \"hash\": \"%s\",\n"
                            "  \"balances\": {\n",
                            ctx->book.block.parent_hash);
  uint32_t n_user = array_length(ctx->book.users);
  for (uint32_t i = 0; i < n_user; ++i) {
    char *account = array_get_ptr(ctx->book.users, i);
    map_value *balance = hash_map_get(ctx->book.balances, account);

    int64_t bal = balance ? json_value_data(balance).integer : 0;
    len = append_str(buffer, len, BUFFER_SMALL, "    \"%s\": %" PRId64 "%s\n",
                     account, bal, i < n_user - 1 ? "," : "");
  }
  len = append_str(buffer, len, BUFFER_SMALL, "  }\n}\n");
  http_message(connection_fd, JSON_RESP, buffer, len, true, nullptr, nullptr);
  return true;
}

bool handle_status(request_metadata_t *meta, node_context_t *ctx, char *buffer,
                   int connection_fd) {
  (void)meta;
  book_t *book = &ctx->book;
  int len = snprintf(buffer, BUFFER_SMALL,
                     "{\n"
                     "  \"hash\": \"%s\",\n"
                     "  \"block_count\": %i\n"
                     "}\n",
                     book->block.parent_hash, book->block.block_idx);
  http_message(connection_fd, JSON_RESP, buffer, len, true, nullptr, nullptr);
  return true;
}

bool handle_peers(request_metadata_t *meta, node_context_t *ctx, char *buffer,
                  int connection_fd) {
  (void)meta;
  (void)ctx;

  uint32_t len = snprintf(buffer, BUFFER_SMALL, "{\"peers\":[");
  uint32_t n_peers = array_length(ctx->peers);
  for (uint32_t i = 0; i < n_peers; ++i) {
    peer_t *peer = array_get_ptr(ctx->peers, i);
    len +=
        snprintf(buffer + len, BUFFER_SMALL - len,
                 "{\n"
                 "  \"ip\":\"%s\",\n"
                 "  \"port\":%i,\n"
                 "  \"name\":\"%s\"\n"
                 "}%s",
                 peer->ip, peer->port, peer->name, i < n_peers - 1 ? "," : "");
  }
  len += snprintf(buffer + len, BUFFER_SMALL - len, "]}\n");
  http_message(connection_fd, JSON_RESP, buffer, len, true, nullptr, nullptr);
  return true;
}

bool handle_exit(request_metadata_t *meta, node_context_t *ctx, char *buffer,
                 int connection_fd) {
  (void)meta;
  (void)ctx;
  int len = snprintf(buffer, BUFFER_SMALL, "{\"status\": \"ended\"}\n");
  stop_listening();
  http_message(connection_fd, JSON_RESP, buffer, len, true, nullptr, nullptr);
  return true;
}

bool handle_create_wallet(request_metadata_t *meta, node_context_t *ctx,
                          char *buffer, int connection_fd) {
  // NOTE: There doesn't ssem to be an incentive to do this yer. I think it is
  // pointless for now.
  // I want to do it though, so that I can setup the wallet on a cloud
  // computer and allow a mobile device user to setup a mining node on the
  // cloud computer.
  // TODO: Implement the above.

  const char *err = "{\"error\":\"Wallet creation cannot be performed via "
                    "API safely yet\"}";
  http_message(connection_fd, JSON_RESP, err, strlen(err), true, nullptr,
               nullptr);
  return true;
}

void reg_endpoints() {
  register_route("/balances", handle_balances);
  register_route("/status", handle_status);
  register_route("/sync", handle_sync);
  register_route("/add_tx", handle_add_tx);
  register_route("/add_node", handle_add_node);
  register_route("/peers", handle_peers);
  register_route("/create_wallet", handle_create_wallet);
  register_route("/exit", handle_exit);
}
