#include <Thrift-Project.h>
#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <zot.h>

#include "book.h"
#include "json_parser.h"
#define BUFFER_SIZE 4096
#define BACKLOG 8000

typedef enum { JSON, TEXT } response_t;
typedef struct {
  char *name;
  char *ip;
  int port;
} peer_t;

char *hash_to_string(char *hash, char *buf, int buffer_sz);
bool tx_from_json(tx_t *tx, json_node_t *saved_tx, book_t *book);
bool hash_block(book_t *book, char *string, uint64_t *string_len, char *hash);

int node_sock;
static book_t *node_book;
static array_t *peers;

char PEERS_FILE_PATH[512];

bool create_socket(int *sock, char *address, int port, bool is_client_socket,
                   struct sockaddr_in *addr, int *addr_len) {

  struct sockaddr_in name = {.sin_family = AF_INET, .sin_port = htons(port)};
  int pton_status = inet_pton(AF_INET, address, &name.sin_addr.s_addr);
  if (pton_status == 0) {
    LOG_ERROR("Invalid address format.");
    return false;
  } else if (pton_status < 0) {
    LOG_ERROR("An error occored:%s", strerror(errno));
    return false;
  }

  *sock = socket(PF_INET, SOCK_STREAM, 0);
  if (*sock < 0) {
    LOG_ERROR("Failed to create socket: %s", strerror(errno));
    return false;
  }

  if (is_client_socket) {
    *addr_len = sizeof(struct sockaddr_in);
    memcpy(addr, &name, *addr_len);
    return true;
  }

  int yes = 1;
  if (setsockopt(*sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
    LOG_ERROR("Cannot reuse address: %s", strerror(errno));
    return false;
  }

  if (bind(*sock, (struct sockaddr *)&name, sizeof(name))) {
    LOG_ERROR("Binding failed: %s", strerror(errno));
    return false;
  }
  if (listen(*sock, BACKLOG)) {
    LOG_ERROR("Listen failed: %s", strerror(errno));
    return false;
  }
  return true;
}

bool http_message(int fd, response_t type, const char *msg, uint64_t msg_len,
                  bool is_server_resp, char *method, char *endpoint) {
  if (fd < 0) {
    LOG_ERROR("Invalid file descriptor");
    return false;
  }

  char headers[BUFFER_SIZE];
  const time_t now = time(NULL);
  struct tm *tm = gmtime(&now);
  char date[35];
  strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S %Z", tm);

  const int headers_len = snprintf(
      headers, BUFFER_SIZE,
      "%s%s%s%sHTTP/1.1%s\r\n"
      "%s: Thrift/%d.%d.%d\r\n"
      "Date: %s\r\n"
      "Content-Length: %" PRIu64 "\r\n"
      "Content-Type: %s\r\n"
      "Connection: close\r\n"
      "\r\n",
      is_server_resp ? "" : method, is_server_resp ? "" : " ",
      is_server_resp ? "" : endpoint, is_server_resp ? "" : " ",
      is_server_resp ? " 200 OK" : "", is_server_resp ? "Server" : "User-Agent",
      THRIFT_PROJECT_VERSION_MAJOR, THRIFT_PROJECT_VERSION_MINOR,
      THRIFT_PROJECT_VERSION_PATCH, date, msg_len,
      type == JSON ? "application/json" : "text/plain");

  bool success = true;

  ssize_t sent = send(fd, headers, headers_len, MSG_NOSIGNAL);
  if (sent < 0) {
    if (errno != EPIPE && errno != ECONNRESET) {
      LOG_ERROR("Header send failed (fd %d): %s", fd, strerror(errno));
    }
    success = false;
  }

  if (success) {
    sent = send(fd, msg, msg_len, MSG_NOSIGNAL);
    if (sent < 0) {
      if (errno != EPIPE && errno != ECONNRESET) {
        LOG_ERROR("Body send failed (fd %d): %s", fd, strerror(errno));
      }
      success = false;
    }
  }

  if (shutdown(fd, SHUT_WR) < 0 && errno != ENOTCONN) {
    LOG_ERROR("Shutdown failed (fd %d): %s", fd, strerror(errno));
  }

  return success;
}

bool peer_from_json(peer_t *peer, json_node_t *saved_peer, array_t *peers) {
  json_iter_t *peer_iter = json_iter(saved_peer);

  char *key;
  json_value_t *value;
  peer->name = strdup("");
  while (!json_next(peer_iter, &key, &value)) {
    if (!strncmp(key, "ip", 2)) {
      peer->ip = value->value.string;
    } else if (!strncmp(key, "port", 4)) {
      peer->port = (int)value->value.number;
    } else if (!strncmp(key, "name", 4)) {
      peer->name = value->value.string;
    }
  }
  array_append_ptr(peers, peer);
  return true;
}

bool handle_link(char *message, char *buffer, book_t *book, int connection_fd) {
  char *c = message;

  if (c[8] == ' ' && !strncmp(c, "balances", 8)) {
    uint64_t message_length;
    message_length = snprintf(buffer, BUFFER_SIZE,
                              ""
                              "{\n"
                              "  \"hash\": \"%s\",\n"
                              "  \"balances\": {\n",
                              book->block.parent_hash);

    char *key;
    ds_value_t *value;
    map_iter(book->balances);
    while (!map_next((void **)&key, &value)) {
      message_length +=
          snprintf(buffer + message_length, BUFFER_SIZE - message_length,
                   "    \"%s\": %f,\n", key, value->value.number);
    }

    buffer[message_length - 2] = ' ';

    message_length +=
        snprintf(buffer + message_length, BUFFER_SIZE - message_length,
                 "  }\n"
                 "}\n");
    http_message(connection_fd, JSON, buffer, message_length, true, NULL, NULL);
    return true;
  } else if (c[6] == ' ' && !strncmp(c, "add_tx", 6)) {
    c += 6;
    char *content_info = strstr(c, "Content-Type: ");
    content_info += 14;
    if (strncmp(content_info, "application/json", 16)) {
      http_message(connection_fd, TEXT, "{\"error\":\"Malformed header\"}", 28,
                   true, NULL, NULL);
      return false;
    }

    char *payload = strstr(c, "\r\n\r\n");
    if (!payload) {
      http_message(connection_fd, TEXT, "{\"error\":\"Missing payload\"}", 26,
                   true, NULL, NULL);
      return false;
    }

    payload += 4;

    json_node_t *json = parse_json_string(payload);
    if (!json || json->value.value_type != OBJECT) {
      const char *err = "{\"error\":\"Invalid transaction format\"}";
      http_message(connection_fd, JSON, err, strlen(err), true, NULL, NULL);
      return true;
    }

    tx_t *tx = zcalloc(1, sizeof(*tx));
    if (!tx_from_json(tx, json->value.value.node, book)) {
      const char *err = "{\"error\":\"Transaction processing failed\"}";
      http_message(connection_fd, JSON, err, strlen(err), true, NULL, NULL);
      zfree(tx);
      return true;
    }

    array_append_ptr(book->block.txs, tx);
    char hash_bin[32];
    hash_block(book, NULL, NULL, hash_bin);

    const int message_length = snprintf(
        buffer, BUFFER_SIZE, "{\"hash\":\"%s\",\"status\":\"pending\"}",
        hash_to_string((char *)hash_bin, buffer + 11, 65));

    if (difftime(time(NULL), book->last_block_time) > 10) {
      save_block(book);
    }

    http_message(connection_fd, JSON, buffer, message_length, true, NULL, NULL);
    return true;

  } else if (c[8] == ' ' && !strncmp(c, "add_node", 8)) {
    c += 8;

    char *content_info = strstr(c, "Content-Type: ");
    content_info += 14;
    if (strncmp(content_info, "application/json", 16)) {
      auto msg = "{\"error\":\"Malformed header\"}";
      http_message(connection_fd, TEXT, msg, strlen(msg), true, NULL, NULL);
      return false;
    }

    char *payload = strstr(c, "\r\n\r\n");
    if (!payload) {
      auto msg = "{\"error\":\"Missing payload\"}";
      http_message(connection_fd, TEXT, msg, strlen(msg), true, NULL, NULL);
      return false;
    }

    payload += 4;

    json_node_t *json = parse_json_string(payload);
    if (!json || json->value.value_type != OBJECT) {
      const char *msg = "{\"error\":\"Invalid message format\"}";
      http_message(connection_fd, JSON, msg, strlen(msg), true, NULL, NULL);
      return true;
    }

    peer_t *peer = zcalloc(1, sizeof(*peer));
    if (!peer_from_json(peer, json->value.value.node, peers)) {
      const char *err = "{\"error\":\"Peer registration failed\"}";
      http_message(connection_fd, JSON, err, strlen(err), true, NULL, NULL);
      zfree(peer);
      return true;
    }

    array_append_ptr(peers, peer);

    const int message_length =
        snprintf(buffer, BUFFER_SIZE,
                 "{\"name\":\"%s\",\"status\":\"success\"}", "lorem");

    http_message(connection_fd, JSON, buffer, message_length, true, NULL, NULL);
    return true;
  }
  const char *err = "{\"error\":\"Invalid endpoint\"}";
  http_message(connection_fd, JSON, err, strlen(err), true, NULL, NULL);
  return false;
}

sighandler_t sys_sig_int = NULL;
bool listening = true;
void stop_listening() {
  listening = false;
  shutdown(node_sock, SHUT_RD);
}

bool run_node(book_t *book, char *address, int port) {
  peers = new_array();

  if (bootstrap_path == NULL) {
    char path[512];
    memcpy(path, thrift_path, strlen(thrift_path) + 1);
    strncat(path, "/.thrift/db/bootstrap.json", 28);
    bootstrap_path = strdup(path);
  }

  node_book = book;
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
      auto json = parse_json(bootstrap_file);
      if (json->value.value_type != ARRAY) {
        LOG("bootstrap.json appears to be malformed.");
      }
      array_t *boots = json->value.value.array;
      for (int i = 0; i < boots->length; i++) {
        json_node_t *node = array_get_ptr(boots, i);
        char *bstrap_name = "";
        char *bstrap_ip;
        int bstrap_port;
        while (node && node->key) {
          if (!strncmp(node->key, "name", 4)) {
            bstrap_name = node->value.value.string;
          } else if (!strncmp(node->key, "ip", 2)) {
            bstrap_ip = node->value.value.string;
          } else if (!strncmp(node->key, "port", 4)) {
            bstrap_port = (int)node->value.value.number;
          } else {
            LOG("bootstrap.json appears to be malformed. Expected \"ip\" or "
                "\"port\" got %s",
                node->key);
          }
          node = node->next_pair;
        }

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
            LOG_ERROR("Connection to %s:%i failed.", bstrap_ip, bstrap_port);
          } else {
            char buffer[BUFFER_SIZE];
            snprintf(buffer, BUFFER_SIZE, "{\"ip\":\"%s\",\"port\":%i}",
                     address, port);
            http_message(sock, JSON, buffer, strlen(buffer), false, "POST",
                         "/add_node");
            // add to list of peers if alive
            read(sock, buffer, sizeof(buffer));
            puts(buffer);
            // add peer
            peer_t *peer = zmalloc(sizeof(*peer));
            peer->ip = bstrap_ip;
            peer->port = bstrap_port;
            peer->name = bstrap_name;
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
  struct sockaddr_in client;
  int sizeof_client = sizeof(client);
  int connection_fd;
  // return true;
  while (listening) {
    if ((connection_fd = accept(node_sock, (struct sockaddr *)&client,
                                &sizeof_client)) < 0) {
      if (listening) {
        LOG_ERROR("Accept failed: %s", strerror(errno));
        const char *err = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
        write(connection_fd, err, strlen(err));
        close(connection_fd);
        continue;
      } else {
        break;
      }
    }
    // process connection
    char buffer[BUFFER_SIZE];
    int message_length = read(connection_fd, buffer, BUFFER_SIZE - 1);
    buffer[message_length] = 0;
    char *c = buffer;
    bool got_link = false, getting_link = false, done_processing = false;

    char *link = NULL;
    while (*c) {
      switch (*c) {
      case 'g':
      case 'G':
        if (!(c == buffer && (c[1] == 'e' || c[1] == 'E') &&
              (c[2] == 't' || c[2] == 'T'))) {
          http_message(connection_fd, TEXT, "invalid message", 15, true, NULL,
                       NULL);
          const char *err = "{\"error\":\"Invalid request format\"}";
          http_message(connection_fd, JSON, err, strlen(err), true, NULL, NULL);
          close(connection_fd);
          c[3] = 0;
        }
        c += 2;
        break;
      case 'p':
      case 'P':
        if (!(c == buffer && (c[1] == 'o' || c[1] == 'O') &&
              (c[2] == 's' || c[2] == 'S') && (c[3] == 't' || c[3] == 'T'))) {
          const char *err = "{\"error\":\"Invalid request format\"}";
          http_message(connection_fd, JSON, err, strlen(err), true, NULL, NULL);
          close(connection_fd);
          c[4] = 0;
        }
        c += 3;
        break;
      case '/':
        if (!got_link && !getting_link) {
          getting_link = true;
          link = c;
          c++;
          if (!handle_link(c, buffer, book, connection_fd)) {
            const char *err = "{\"error\":\"Invalid request format\"}";
            http_message(connection_fd, JSON, err, strlen(err), true, NULL,
                         NULL);
            close(connection_fd);
          }
        }
        break;
      case ' ':
        if (getting_link) {
          got_link = true;
          getting_link = false;
          *c = 0;
        }
      }
      c++;
      if (done_processing)
        break;
    }

    shutdown(connection_fd, SHUT_RDWR);
    close(connection_fd);
  }

  shutdown(node_sock, SHUT_RDWR);
  close(node_sock);
  if (book->block.txs->length > 0) {
    LOG("Saving pending block.");
    save_block(node_book);
  }
  return true;
}
