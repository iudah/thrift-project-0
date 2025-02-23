#include <Thrift-Project.h>
#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "book.h"
#include "error.h"
#include "json_parser.h"
#define BUFFER_SIZE 4096
#define BACKLOG 8000

typedef enum { JSON, TEXT } response_t;

char *hash_to_string(char *hash, char *buf, int buffer_sz);
bool tx_from_json(tx_t *tx, json_node_t *saved_tx, book_t *book);
bool hash_block(book_t *book, char *string, uint64_t *string_len, char *hash);

int node_sock;
static book_t *node_book;

bool create_socket(int *sock, char *address, int port) {
  *sock = socket(PF_INET, SOCK_STREAM, 0);
  if (*sock < 0) {
    LOG_ERROR("Failed to create socket: %s", strerror(errno));
    return false;
  }

  int yes = 1;
  if (setsockopt(*sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
    LOG_ERROR("Cannot reuse address: %s", strerror(errno));
    return false;
  }

  struct sockaddr_in name = {.sin_family = AF_INET,
                             .sin_port = htons(port),
                             .sin_addr.s_addr = inet_addr(address)};

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

bool http2_response(int fd, response_t type, const char *resp,
                    uint64_t resp_len) {
  if (fd < 0) {
    LOG_ERROR("Invalid file descriptor");
    return false;
  }

  char headers[BUFFER_SIZE];
  const time_t now = time(NULL);
  struct tm *tm = gmtime(&now);
  char date[35];
  strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S %Z", tm);

  const int headers_len =
      snprintf(headers, BUFFER_SIZE,
               "HTTP/2 200 OK\r\n"
               "Server: Thrift/%d.%d.%d\r\n"
               "Date: %s\r\n"
               "Content-Length: %" PRIu64
               "\r\n"
               "Content-Type: %s\r\n"
               "Connection: close\r\n"
               "\r\n",
               THRIFT_PROJECT_VERSION_MAJOR, THRIFT_PROJECT_VERSION_MINOR,
               THRIFT_PROJECT_VERSION_PATCH, date, resp_len,
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
    sent = send(fd, resp, resp_len, MSG_NOSIGNAL);
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
    http2_response(connection_fd, JSON, buffer, message_length);
    return true;
  } else if (c[6] == ' ' && !strncmp(c, "add_tx", 6)) {
    c += 6;

    while (*c) {
      if (c[30] == '\r' && c[31] == '\n' &&
          !strncmp("Content-Type: application/json", c, 30)) {
        c += 31;

        char *payload = strstr(c, "\r\n\r\n");
        if (!payload) {
          http2_response(connection_fd, TEXT, "{\"error\":\"Missing payload\"}",
                         26);
          return false;
        }
        payload += 4;

        json_node_t *json = parse_json_string(payload);
        if (!json || json->value.value_type != OBJECT) {
          const char *err = "{\"error\":\"Invalid transaction format\"}";
          http2_response(connection_fd, JSON, err, strlen(err));
          return true;
        }

        tx_t *tx = calloc(1, sizeof(*tx));
        if (!tx_from_json(tx, json->value.value.node, book)) {
          const char *err = "{\"error\":\"Transaction processing failed\"}";
          http2_response(connection_fd, JSON, err, strlen(err));
          free(tx);
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

        http2_response(connection_fd, JSON, buffer, message_length);
        return true;
      }
      c++;
    }
  }
  const char *err = "{\"error\":\"Invalid endpoint\"}";
  http2_response(connection_fd, JSON, err, strlen(err));
  return false;
}

sighandler_t sys_sig_int = NULL;
bool listening = true;
void stop_listening() {
  listening = false;
  shutdown(node_sock, SHUT_RD);
}

bool run_node(book_t *book, char *address, int port) {
  node_book = book;
  sys_sig_int = signal(SIGINT, (sighandler_t)stop_listening);

  if (!create_socket(&node_sock, address, port)) {
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
        const char *err = "HTTP/2 500 Internal Server Error\r\n\r\n";
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
            http2_response(connection_fd, TEXT, "invalid message", 15);
            const char *err = "{\"error\":\"Invalid request format\"}";
            http2_response(connection_fd, JSON, err, strlen(err));
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
            http2_response(connection_fd, JSON, err, strlen(err));
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
              http2_response(connection_fd, JSON, err, strlen(err));
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
      if (done_processing) break;
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
