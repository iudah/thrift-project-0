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

#define BUFFER_SIZE 4096
#define BACKLOG 8000

extern int errno;
int node_sock;
volatile sig_atomic_t listening = 0;
sighandler_t sys_sig_int = nullptr;
pthread_t thread;
pthread_mutex_t listen_lock = PTHREAD_MUTEX_INITIALIZER;

void *add_peers_and_blocks(node_context_t *ctx);

bool create_socket(int *sock, char *address, int port, bool is_client_socket,
                   struct sockaddr_in *addr, int *addr_len) {

  struct sockaddr_in name = {.sin_family = AF_INET, .sin_port = htons(port)};
  int pton_status = inet_pton(AF_INET, address, &name.sin_addr.s_addr);
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

bool peer_from_json(peer_t *peer, json_value *saved_peer, array_t *peers) {

  json_value *query;

  query = json_query(saved_peer, "ip");
  if (json_value_type(query) != TEXT) {
    return false;
  }
  peer->ip = json_value_data(query).string;

  query = json_query(saved_peer, "port");
  if (json_value_type(query) != FLOATS) {
    return false;
  }
  peer->port = (int)json_value_data(query).number;

  query = json_query(saved_peer, "name");
  if (query) {
    if (json_value_type(query) != TEXT) {
      return false;
    }
    peer->name = json_value_data(query).string;
  }

  array_append_ptr(peers, peer);
  return true;
}

struct poll_array {
  struct pollfd *fds;
  uint64_t count;
  uint64_t cap;
};

bool init_poll_array(struct poll_array *fds) {
  fds->cap = BUFFER_SMALL;
  fds->fds = zmalloc(fds->cap * sizeof(*fds->fds));
  return fds->fds != nullptr;
}

bool add_pollfd(struct poll_array *fds, int file_desc) {
  if (fds->count >= fds->cap) {
    auto new_cap = fds->cap * 2;
    auto tmp = zrealloc(fds->fds, new_cap * sizeof(*fds->fds));

    if (!tmp)
      return false;

    fds->cap = new_cap;
    fds->fds = tmp;
  }

  fds->fds[fds->count++].fd = file_desc;

  return true;
}

bool del_pollfd(struct poll_array *fds, uint64_t index, int file_desc) {
  if (index >= fds->count || fds->fds[index].fd != file_desc)
    return false;

  fds->fds[index] = fds->fds[--fds->count];

  return true;
}

struct worker_arg {
  struct poll_array fds;
  struct {
    int32_t readfd;
    int32_t writefd;
  } pipe;
  pthread_t thread;
};

void *web_worker(void *arg) {
  struct worker_arg *worker = arg;
  while (true) {
    auto ret = poll(worker->fds.fds, worker->fds.count, -1);
    if (ret < 0)
      break;

    for (uint64_t i = 0; i < worker->fds.count; ++i) {

      if (worker->fds.fds[i].fd == worker->pipe.readfd &&
          (worker->fds.fds[i].revents & POLLIN)) {
        bool listening;
        read(worker->pipe.readfd, &listening, 1);

        if (!listening)
          break;

        continue;
      }

      if (worker->fds.fds[i].revents & POLLIN) {
        auto client_fd = worker->fds.fds[i].fd;

        char *buffer = zmalloc(BUFFER_BIG + 1);
        // handle reqs
        auto read_size = read(client_fd, buffer, BUFFER_BIG);

        if (read_size < 0) {
          auto errmsg = strerror(errno);
          write(client_fd, errmsg, strlen(errmsg));
        } else {
        }

        shutdown(client_fd, SHUT_RDWR);
        close(client_fd);
        del_pollfd(&worker->fds, i, client_fd);
      }
    }
  }
  return nullptr;
}

bool run_node(node_context_t *ctx, char *address, int port) {

  ctx->peers = create_array();
  set_handlers_peers(ctx->peers);
  reg_endpoints();

  sys_sig_int = signal(SIGINT, (sighandler_t)stop_listening);

  if (!create_socket(&node_sock, address, port, false, nullptr, nullptr)) {
    LOG_ERROR("Failed to initialize node socket");
    return false;
  }
  LOG("Node is running at %s:%d", address, port);
  fflush(stdout);

  zthread_create(&thread, nullptr, (void *)add_peers_and_blocks, ctx);

  struct sockaddr_in client;
  int sizeof_client = sizeof(client);
  int connection_fd;

#define N_THREADS (4)
  struct worker_arg workers[N_THREADS] = {nullptr};
  for (uint64_t i = 0; i < N_THREADS; ++i) {
    init_poll_array(&workers[i].fds);
    zthread_create(&workers[i].thread, nullptr, (void *)web_worker,
                   &workers[i]);

    if (pipe((int *)&workers[i].pipe) == -1) {
      LOG_ERROR("%s.", strerror(errno));
      return false;
    }

    add_pollfd(&workers[i].fds, workers[i].pipe.readfd);
  }

  struct pollfd fds = {node_sock};

#define TIMEOUT (250)

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
      if (!worker || worker->fds.count > workers[i].fds.count) {
        worker = &workers[i];
      }
      if (worker->fds.count == 1) {
        break;
      }
    }

    add_pollfd(&worker->fds, connection_fd);
    write(worker->pipe.writefd, (bool[1]){true}, 1);
  }

  shutdown(node_sock, SHUT_RDWR);
  close(node_sock);

  pthread_join(thread, nullptr);

  for (uint64_t i = 0; i < N_THREADS; ++i) {
    write(workers[i].pipe.writefd, (bool[1]){false}, 1);
    pthread_join(workers[i].thread, nullptr);
  }

  return true;
}

void stop_listening() {
  listening = 0;
  shutdown(node_sock, SHUT_RD);
}

void *add_peers_and_blocks(node_context_t *ctx) {
  book_t *book = &ctx->book;

  while (1) {
    pthread_mutex_lock(&listen_lock);
    bool adding_peers_and_block = listening == 1;
    pthread_mutex_unlock(&listen_lock);

    if (adding_peers_and_block) {
      // Search and add peers
      // Search and add blocks
    }
  }

  return NULL;
}
