#include <Thrift-Project.h>
#include <arpa/inet.h>
#include <asm-generic/poll.h>
#include <bits/pthread_types.h>
#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <zot.h>

#include "book.h"
#include "ds.h"
#include "ison.h"
#include "node_net.h"
#include "router.h"
#include "src/http_helper.h"

#define BUFFER_SIZE 4096
#define BACKLOG     8000

extern int           errno;
int                  node_sock;
volatile atomic_bool listening;
sighandler_t         sys_sig_int = nullptr;
pthread_t            thread;

void *add_peers_and_blocks(node_context_t *ctx);

bool create_socket(int *sock, char *address, int port, bool is_client_socket,
                   struct sockaddr_in *addr, int *addr_len) {

  struct sockaddr_in name = {.sin_family = AF_INET, .sin_port = htons(port)};
  int pton_status         = inet_pton(AF_INET, address, &name.sin_addr.s_addr);
  if (pton_status == 0) {
    LOG("Invalid address format.");
    return false;
  }
  if (pton_status < 0) {
    LOG("An error occored:%s", strerror(errno));
    return false;
  }

  *sock = socket(PF_INET, SOCK_STREAM, 0);
  if (*sock < 0) {
    LOG("Failed to create socket: %s", strerror(errno));
    return false;
  }

  if (is_client_socket) {
    *addr_len = sizeof(struct sockaddr_in);
    memcpy(addr, &name, *addr_len);
    // TODO: Use non-blocking connect
    struct timeval time_value = {.tv_sec = 1, .tv_usec = 0};
    setsockopt(*sock, SOL_SOCKET, SO_RCVTIMEO, &time_value, sizeof(time_value));
    setsockopt(*sock, SOL_SOCKET, SO_SNDTIMEO, &time_value, sizeof(time_value));
    if (connect(*sock, (struct sockaddr *)&name, *addr_len) < 0) {
      close(*sock);
      return false;
    }
    return true;
  }

  int yes = 1;
  if (setsockopt(*sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
    LOG("Cannot reuse address: %s", strerror(errno));
    return false;
  }

  if (bind(*sock, (struct sockaddr *)&name, sizeof(name))) {
    LOG("Binding failed: %s", strerror(errno));
    return false;
  }
  if (listen(*sock, BACKLOG)) {
    LOG("Listen failed: %s", strerror(errno));
    return false;
  }
  return true;
}

bool peer_from_json(peer_t *peer, json_value *saved_peer) {
  if (!peer || !saved_peer)
    return false;

  json_value *query;

  query = json_query(saved_peer, "ip");
  if (!query || json_value_type(query) != TEXT)
    return false;
  peer->ip = json_value_data(query).string;

  query = json_query(saved_peer, "port");
  if (!query || json_value_type(query) != INTEGERS)
    return false;
  peer->port = (int)json_value_data(query).integer;

  query = json_query(saved_peer, "name");
  if (query) {
    if (json_value_type(query) != TEXT)
      return false;
    peer->name = json_value_data(query).string;
  }

  return true;
}

bool requested_block_hex(json_value *json, char *block_hash_hex) {
  if (!json || !block_hash_hex)
    return false;

  json_value *query = json_query(json, "block");
  if (!query || json_value_type(query) != TEXT)
    return false;

  const char *src = json_value_data(query).string;
  if (!src)
    return false;

  size_t len = strlen(src);
  if (len > SHA256_HEX_LEN)
    len = SHA256_HEX_LEN;
  memcpy(block_hash_hex, src, len);
  block_hash_hex[len] = '\0';

  return true;
}

struct poll_array {
  struct pollfd *poll_fds;
  uint64_t       len;
  uint64_t       cap;
};

bool init_poll_array(struct poll_array *fds) {
  fds->cap      = BUFFER_SMALL;
  fds->poll_fds = zmalloc(fds->cap * sizeof(*fds->poll_fds));
  return fds->poll_fds != nullptr;
}

bool add_pollfd(struct poll_array *fds, int file_desc) {
  if (fds->len >= fds->cap) {
    auto new_cap = fds->cap * 2;
    auto tmp     = zrealloc(fds->poll_fds, new_cap * sizeof(*fds->poll_fds));

    if (!tmp)
      return false;

    fds->cap      = new_cap;
    fds->poll_fds = tmp;
  }

  fds->poll_fds[fds->len].fd     = file_desc;
  fds->poll_fds[fds->len].events = POLLIN;
  ++fds->len;

  return true;
}

bool del_pollfd(struct poll_array *fds, uint64_t index, int file_desc) {
  if (index >= fds->len || fds->poll_fds[index].fd != file_desc)
    return false;

  fds->poll_fds[index] = fds->poll_fds[--fds->len];

  return true;
}

struct worker_arg {
  node_context_t   *ctx;
  struct poll_array watched;
  struct {
    int32_t readfd;
    int32_t writefd;
  } pipe;
  pthread_t thread;
};

void *web_worker(void *arg) {
  struct worker_arg *worker  = arg;
  bool               running = true;

  while (running) {
    errno   = 0;
    int ret = poll(worker->watched.poll_fds, worker->watched.len, -1);
    if (ret < 0) {
      if (errno == EINTR)
        continue;
      break;
    }

    for (uint64_t i = 0; i < worker->watched.len;) {
      int   worker_pipe = worker->watched.poll_fds[i].fd;
      short revents     = worker->watched.poll_fds[i].revents;

      if (worker_pipe == worker->pipe.readfd) {
        if (revents & POLLIN) {
          uint8_t cmd     = 0;
          ssize_t cmd_len = read(worker_pipe, &cmd, 1);
          if (cmd_len <= 0 || cmd == 0) {
            running = false;
            break;
          }
        }
        ++i;
        continue;
      }

      int client_socket = worker->watched.poll_fds[i].fd;
      if (revents & (POLLHUP | POLLERR | POLLNVAL)) {
        shutdown(client_socket, SHUT_RDWR);
        close(client_socket);
        del_pollfd(&worker->watched, i, client_socket);
        continue;
      }

      if (revents & POLLIN) {
        char    message[BUFFER_BIG + 1];
        ssize_t msg_len = read(client_socket, message, BUFFER_BIG);
        if (msg_len > 0) {
          message[msg_len] = '\0';
          char resp[BUFFER_SMALL];
          route_request(message, resp, worker->ctx, client_socket);
        }
        shutdown(client_socket, SHUT_RDWR);
        close(client_socket);
        del_pollfd(&worker->watched, i, client_socket);
        continue;
      }

      ++i;
    }
  }

  return nullptr;
}

int  search_pipe_fds[2]; // R,W
bool run_node(node_context_t *ctx, char *address, int port) {

  ctx->peers = create_array();
  load_peers(ctx->peers, ctx);
  reg_endpoints();

  if (!create_socket(&node_sock, address, port, false, nullptr, nullptr)) {
    LOG_ERROR("Failed to initialize node socket");
    return false;
  }
  LOG("Node is running at %s:%d", address, port);
  fflush(stdout);

  if (pipe(search_pipe_fds) < 0) {
    LOG_ERROR("pipe failed: %s", strerror(errno));
    return false;
  }

  sys_sig_int = signal(SIGINT, (sighandler_t)stop_listening);
  zthread_create(&thread, nullptr, (void *)add_peers_and_blocks, ctx);

  struct sockaddr_in client;
  int                sizeof_client = sizeof(client);
  int                connection_fd;

#define N_THREADS (4)
  struct worker_arg workers[N_THREADS] = {{nullptr}};
  for (uint64_t i = 0; i < N_THREADS; ++i) {
    init_poll_array(&workers[i].watched);
    if (pipe((int *)&workers[i].pipe) == -1) {
      LOG_ERROR("%s.", strerror(errno));
      return false;
    }

    add_pollfd(&workers[i].watched, workers[i].pipe.readfd);
    workers[i].ctx = ctx;

    zthread_create(&workers[i].thread, nullptr, (void *)web_worker,
                   &workers[i]);
  }

  struct pollfd fds = {.fd = node_sock, .events = POLLIN};

#define TIMEOUT (250)

  atomic_store(&listening, true);
  while (listening) {

    auto n_open_fds = poll(&fds, 1, TIMEOUT);

    if (n_open_fds <= 0)
      continue;
    if (!(fds.revents & POLLIN))
      continue;

    if ((connection_fd = accept(node_sock, (struct sockaddr *)&client,
                                &sizeof_client)) < 0) {
      LOG_ERROR("Accept failed: %s.", strerror(errno));
      continue;
    }

    struct worker_arg *worker = nullptr;
    for (uint64_t i = 0; i < N_THREADS; ++i) {
      if (!worker || worker->watched.len > workers[i].watched.len) {
        worker = &workers[i];
      }
      if (worker->watched.len == 1) {
        break;
      }
    }

    add_pollfd(&worker->watched, connection_fd);
    write(worker->pipe.writefd, (uint8_t[1]){true}, 1);
  }

  shutdown(node_sock, SHUT_RDWR);
  close(node_sock);

  for (uint64_t i = 0; i < N_THREADS; ++i) {
    write(workers[i].pipe.writefd, (bool[1]){false}, 1);
    pthread_join(workers[i].thread, nullptr);
  }
  pthread_join(thread, nullptr);

  close(search_pipe_fds[0]);
  close(search_pipe_fds[1]);

  if (array_length(ctx->book.block.txs) > 0) {
    LOG("Saving pending block.");
    save_block(&ctx->book, ctx->book.block.miner, ctx);
  }

  return true;
}

void stop_listening() {
  if (!listening)
    return;
  atomic_store(&listening, false);
  write(search_pipe_fds[1], " ", 1);
  shutdown(node_sock, SHUT_RD);
}

static bool get_peer_with_most_block(array_t *peers, peer_t *peer,
                                     const uint32_t n_blocks) {
  uint32_t n_peers         = array_length(peers);
  peer_t   chosen_peer     = {nullptr, nullptr, 0};
  uint32_t max_peer_blocks = n_blocks;

  for (uint32_t i = 0; i < n_peers; ++i) {
    peer_t *candidate = array_get_ptr(peers, i);
    if (!candidate || !candidate->ip)
      continue;

    int                server_fd = -1;
    struct sockaddr_in addr;
    int                addr_len = 0;
    if (!create_socket(&server_fd, candidate->ip, candidate->port, true, &addr,
                       &addr_len))
      continue;

    char     response[BUFFER_BIG];
    uint64_t response_len = 0;
    bool     request_ok =
        http_client_request(server_fd, "GET", "/status", nullptr, 0, response,
                            sizeof(response), &response_len);
    shutdown(server_fd, SHUT_RDWR);
    close(server_fd);
    if (!request_ok)
      continue;

    char *body = http_find_body(response, response_len);
    if (!body)
      continue;

    json_value *json = json_parse_string(body);
    if (!json || json_value_type(json) != DICT)
      continue;

    json_value *count_q = json_query(json, "block_count");
    if (!count_q)
      continue;

    uint32_t count = 0;
    if (json_value_type(count_q) == INTEGERS)
      count = (uint32_t)json_value_data(count_q).integer;
    else
      continue;

    if (count > max_peer_blocks) {
      max_peer_blocks = count;
      chosen_peer     = *candidate;
    }
  }

  *peer = chosen_peer;
  return chosen_peer.ip != nullptr;
}

static bool apply_synced_blocks(book_t *book, json_value *json,
                                node_context_t *ctx) {
  json_value *blocks_q = json_query(json, "blocks");
  if (!blocks_q || json_value_type(blocks_q) != LIST)
    return false;

  array_t *blocks   = json_value_data(blocks_q).ptr;
  uint32_t n_blocks = array_length(blocks);

  for (uint32_t i = 0; i < n_blocks; ++i) {
    json_value *blk_json = array_get_ptr(blocks, i);
    if (!blk_json || json_value_type(blk_json) != DICT)
      continue;

    block_t blk;
    if (!block_from_json(&blk, blk_json))
      continue;

    memcpy(book->block.parent_hash, blk.parent_hash, SHA256_HEX_BUF_LEN);
    memcpy(book->block.block_time, blk.block_time, STRFTIME_BUF_LEN);
    memcpy(book->block.miner, blk.miner, ADDRESS_STR_MAX);
    book->block.block_idx = blk.block_idx;
    book->block.txs       = blk.txs;

    if (!effect_block(&book->block, book))
      continue;

    save_block(book, book->block.miner, ctx);
  }

  return true;
}

static void sync_with_peer(node_context_t *ctx) {
  peer_t peer = {nullptr, nullptr, 0};

  if (!get_peer_with_most_block(ctx->peers, &peer, ctx->book.block.block_idx))
    return;
  if (!peer.ip)
    return;

  int                server_fd = -1;
  struct sockaddr_in addr;
  int                addr_len = 0;
  if (!create_socket(&server_fd, peer.ip, peer.port, true, &addr, &addr_len))
    return;

  char body[BUFFER_SMALL];
  int  body_len = snprintf(body, sizeof(body), "{\"block\":\"%s\"}",
                           ctx->book.block.parent_hash);
  if (body_len <= 0 || body_len >= (int)sizeof(body)) {
    shutdown(server_fd, SHUT_RDWR);
    close(server_fd);
    return;
  }

  char     response[BUFFER_BIG * 2];
  uint64_t response_len = 0;
  bool     request_ok =
      http_client_request(server_fd, "POST", "/sync", body, body_len, response,
                          sizeof(response), &response_len);
  shutdown(server_fd, SHUT_RDWR);
  close(server_fd);
  if (!request_ok)
    return;

  char *resp_body = http_find_body(response, response_len);
  if (!resp_body)
    return;

  json_value *json = json_parse_string(resp_body);
  if (!json || json_value_type(json) != DICT)
    return;

  apply_synced_blocks(&ctx->book, json, ctx);
}

void *add_peers_and_blocks(node_context_t *ctx) {

  struct pollfd fds = {.fd = search_pipe_fds[0], .events = POLLIN};

  while (listening) {
    errno           = 0;
    auto n_open_fds = poll(&fds, 1, TIMEOUT);

    if (n_open_fds < 0) {
      if (errno == EINTR)
        continue;
      break;
    }

    if (n_open_fds == 0 && listening) {
      sync_with_peer(ctx);
    }
  }

  save_peers(ctx->peers, ctx);

  return nullptr;
}
// https://www.youtube.com/watch?v=3mBglPMQGVk
