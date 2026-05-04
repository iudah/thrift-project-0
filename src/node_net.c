#include <Thrift-Project.h>
#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <zot.h>

#include "book.h"
#include "ds.h"
#include "ison.h"
#include "node_context.h"
#include "node_net.h"
#include "router.h"

#define BUFFER_SIZE 4096
#define BACKLOG 8000

extern int errno;
int node_sock;
volatile sig_atomic_t listening = 0;
sighandler_t sys_sig_int = NULL;
pthread_t thread;
pthread_mutex_t listen_lock = PTHREAD_MUTEX_INITIALIZER;

char PEERS_FILE_PATH[512];

void set_handlers_peers(array_t *peer_list);
void reg_endpoints();
void *add_peers_and_blocks(node_context_t *ctx);

bool create_socket(int *sock, char *address, int port, bool is_client_socket,
                   struct sockaddr_in *addr, int *addr_len) {

  struct sockaddr_in name = {.sin_family = AF_INET, .sin_port = htons(port)};
  int pton_status = inet_pton(AF_INET, address, &name.sin_addr.s_addr);
  if (pton_status == 0) {
    LOG("Invalid address format.");
    return false;
  } else if (pton_status < 0) {
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

  json_value *q;

  q = json_query(saved_peer, "ip");
  if (json_value_type(q) != TEXT) {
    return false;
  }
  peer->ip = json_value_data(q).string;

  q = json_query(saved_peer, "port");
  if (json_value_type(q) != FLOATS) {
    return false;
  }
  peer->port = (int)json_value_data(q).number;

  q = json_query(saved_peer, "name");
  if (q) {
    if (json_value_type(q) != TEXT) {
      return false;
    }
    peer->name = json_value_data(q).string;
  }

  array_append_ptr(peers, peer);
  return true;
}

bool run_node(node_context_t *ctx, book_t *book, char *address, int port) {

  ctx->peers = create_array();
  set_handlers_peers(ctx->peers);
  reg_endpoints();

  if (bootstrap_path == NULL) {
    char path[512];
    memcpy(path, thrift_path, strlen(thrift_path) + 1);
    strncat(path, "/.thrift/db/bootstrap.json", 28);
    bootstrap_path = zstrdup(path);
  }

  ctx->book = book;
  sys_sig_int = signal(SIGINT, (sighandler_t)stop_listening);

  // check list of peers
  snprintf(PEERS_FILE_PATH, sizeof(PEERS_FILE_PATH), "%s/.thrift/db/peers.json",
           thrift_path);
  FILE *peers_file = fopen(PEERS_FILE_PATH, "r");
  if (!peers_file) {

  } else {
    // check if peers are available
  }

  if (!peers_file /*or no peers available*/) {
    FILE *bootstrap_file = fopen(bootstrap_path, "r");
    if (!bootstrap_file) {
      LOG("No bootstraps found, assuming we are the only running node.");
    } else {
      auto json = json_parse_file(bootstrap_file);
      if (!json || json_value_type(json) != LIST) {
        LOG("bootstrap.json appears to be malformed.");
      }
      array_t *boots = json_value_data(json).ptr;
      uint32_t boot_strap_len = array_length(boots);
      for (int i = 0; i < boot_strap_len; i++) {
        json_value *node = array_get(boots, i);
        char *bstrap_name = "";
        char *bstrap_ip;
        int bstrap_port;

        json_value *q;

        q = json_query(node, "ip");
        if (json_value_type(q) != TEXT) {
          return false;
        }
        bstrap_ip = json_value_data(q).string;

        q = json_query(node, "port");
        if (json_value_type(q) != FLOATS) {
          return false;
        }
        bstrap_port = (int)json_value_data(q).number;

        q = json_query(node, "name");
        if (q != NULL && json_value_type(q) != TEXT) {
          return false;
        }
        bstrap_name = q ? json_value_data(q).string : zstrdup("");
        // LOG("bootstrap.json appears to be malformed. Expected \"ip\" or "
        //     "\"port\".");

        int sock;
        struct sockaddr_in addr;
        int addr_len;
        // for each ip:port request to be addded
        if (!create_socket(&sock, bstrap_ip, bstrap_port, true, &addr,
                           &addr_len)) {
          LOG("Failed to create socket.");
        } else {
          int status = connect(sock, (const struct sockaddr *)&addr, addr_len);
          if (status < 0) {
            LOG("Connection to %s:%i failed.", bstrap_ip, bstrap_port);
          } else {
            char buffer[BUFFER_SIZE];
            snprintf(buffer, BUFFER_SIZE, "{\"ip\":\"%s\",\"port\":%i}",
                     address, port);
            http_message(sock, JSON_RESP, buffer, strlen(buffer), false, "POST",
                         "/add_node");
            // add to list of peers if alive
            read(sock, buffer, sizeof(buffer));
            puts(buffer);
            // add peer
            peer_t *peer = zmalloc(sizeof(*peer));
            peer->ip = bstrap_ip;
            peer->port = bstrap_port;
            peer->name = bstrap_name;

            array_append_ptr(ctx->peers, peer);
          }
          close(sock);
        }
      }
    }
  }

  if (!create_socket(&node_sock, address, port, false, NULL, NULL)) {
    LOG_ERROR("Failed to initialize node socket");
    return false;
  }
  LOG("Node is running at %s:%d", address, port);
  fflush(stdout);

  zthread_create(&thread, NULL, (void *)add_peers_and_blocks, ctx);

  struct sockaddr_in client;
  int sizeof_client = sizeof(client);
  int connection_fd;
  // return true;
  while (listening) {
    if ((connection_fd = accept(node_sock, (struct sockaddr *)&client,
                                &sizeof_client)) < 0) {
      if (listening) {
        LOG("Accept failed: %s", strerror(errno));
        const char *err = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
        write(connection_fd, err, strlen(err));
        close(connection_fd);
        continue;
      } else {
        break;
      }
    }
    // process connection
    // TODO: use dynamic allocation for resp
    char resp[BUFFER_SIZE];
    char buffer[BUFFER_SIZE];
    int message_length = read(connection_fd, buffer, BUFFER_SIZE - 1);
    buffer[message_length] = 0;
    char *c = buffer;
    bool got_link = false, getting_link = false, done_processing = false;

    char *link = NULL;
    if (!route_request(c, resp, ctx, connection_fd)) {
      const char *err = "{\"error\":\"Invalid request format\"}";
      http_message(connection_fd, JSON_RESP, err, strlen(err), true, NULL,
                   NULL);
      close(connection_fd);
    }

    shutdown(connection_fd, SHUT_RDWR);
    close(connection_fd);
  }

  shutdown(node_sock, SHUT_RDWR);
  close(node_sock);
  if (array_length(book->block.txs) > 0) {
    LOG("Saving pending block.");
    save_block(ctx->book);
  }

  pthread_join(thread, NULL);

  return true;
}

void stop_listening() {
  pthread_mutex_lock(&listen_lock);
  listening = false;
  pthread_mutex_unlock(&listen_lock);
  shutdown(node_sock, SHUT_RD);
}

void *add_peers_and_blocks(node_context_t *ctx) {
  book_t *book = ctx->book;

  while (1) {
    pthread_mutex_lock(&listen_lock);
    bool adding_peers_and_block = listening;
    pthread_mutex_unlock(&listen_lock);

    if (adding_peers_and_block) {
      sleep(25);
      // Search and add peers
      // Search and add blocks
    }
  }

  return NULL;
}
