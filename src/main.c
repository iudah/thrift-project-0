#include <Thrift-Project.h>
#include <bits/getopt.h>
#include <getopt.h>
#include <stdio.h>
#include <wallet.h>
#include <zot.h>
#define _XOPEN_SOURCE 500
#include <stdlib.h>
#include <string.h>

int getsubopt(char **optionp, char *const *keylistp, char **valuep);

#include "book.h"
#include "node_net.h"

enum { ADD_TX, SHOW_BALS, START_NODE } action;
enum { FROM = 0, TO, AMOUNT, NOTE, ADDRESS, PORT, THRIFT };
char *const tokens[] = {
    [FROM] = "from",         [TO] = "to",           [AMOUNT] = "amount",
    [NOTE] = "note",         [ADDRESS] = "address", [PORT] = "port",
    [THRIFT] = "thrift_path"};

bool path_set_only = false;

void print_help() {
  printf("Usage:\n"
         "thrift [--thrift \"path/to/.thrift\"] --add-tx "
         "from=<sender>,to=<reciever>,amount=<amount>[,note=<note>]\n"
         "thrift [--thrift \"path/to/.thrift\"] --show-balances\n"
         "thrift [--bootstrap \"path/to/bootstrap\" --thrift "
         "\"path/to/.thrift\"] --start-node "
         "address=<address>[,port=<port>]\n");
}

bool set_path(char *pathname, char **path, char *usr_path) {
  if (*path) {
    LOG("%s already set to %s. Ignoring new path \"%s\"", pathname, *path,
        usr_path);
    return false;
  }
  *path = zstrdup(usr_path);

  set_paths();

  return path_set_only = true;
}

int main(int argc, char *argv[]) {
  printf("Thrift Project CLI\n"
         "Version %d.%d.%d\n",
         THRIFT_PROJECT_VERSION_MAJOR, THRIFT_PROJECT_VERSION_MINOR,
         THRIFT_PROJECT_VERSION_PATCH);
  fflush(stdout);

  if (argc < 2) {
    print_help();
  }

  book_t *book = NULL;

  static struct option long_options[] = {
      {"add-tx", required_argument, 0, 'a'},
      {"add-reward", no_argument, 0, 'r'},
      {"show-balances", no_argument, 0, 'b'},
      {"create-wallet", no_argument, 0, 'c'},
      {"start-node", required_argument, 0, 's'},
      {"thrift", required_argument, 0, 'T'},
      {"bootstrap", required_argument, 0, 'B'},
      {NULL, 0, NULL, 0}};

  int opt_indx = 0;
  int c = 0;

  while ((c = getopt_long(argc, argv, "a:rbcs:T:B:", long_options,
                          &opt_indx)) != -1) {
    switch (c) {
    case 'a': {
      tx_t tx = {.purpose = NULL};
      char *subopt = optarg;
      char *value;
      while (*subopt != '\0') {
        switch (getsubopt(&subopt, tokens, &value)) {
        case FROM:
          tx.from = value;
          break;
        case TO:
          tx.to = value;
          break;
        case AMOUNT:
          tx.amount = strtod(value, NULL);
          break;
        case NOTE:
          tx.purpose = value;
          break;
        }
      }

      book = load_book();
      if (!book) {
        LOG_ERROR("Failed to load book");
        return -1;
      }

      user_keystore = load_keystore(NULL);
      if (!tx.from) {
        tx.from = get_address_from_keystore();
      } else if (strcmp(tx.from, get_address_from_keystore())) {
        LOG("Incorrect sender address. Expected `%s` got `%s`.",
            get_address_from_keystore(), tx.from);
        return -1;
      }

      sign_tx(&tx, user_keystore);

      add_tx(book, tx.from, tx.to, tx.amount, tx.purpose);
      save_block(book);
      printf("Txs updated successfully.\n");
    } break;
    case 'r': {

      book = load_book();
      if (!book) {
        LOG_ERROR("Failed to load book");
        return -1;
      }

      add_reward(book);
      save_block(book);
      printf("Reward updated successfully.\n");
    } break;
    case 'b': {
      book = load_book();
      if (!book) {
        LOG_ERROR("Failed to load book");
        return -1;
      }

      show_balances(book);
    } break;
    case 'c': {
      path_set_only = false;

      if (keystore_exists()) {
        LOG("Wallet already exists.");
        // user_keystore = load_keystore(NULL);
      } else {
        user_keystore = create_wallet(NULL);
      }
      printf("Your address is: %s\n", get_address_from_keystore());
    } break;
    case 's': {
      int port = 80;
      char *address = NULL;
      char *subopt = optarg;
      char *value;
      while (*subopt != '\0') {
        switch (getsubopt(&subopt, tokens, &value)) {
        case ADDRESS:
          address = value;
          break;
        case PORT:
          port = atoi(value);
          break;
        }
      }

      book = load_book();
      if (!book) {
        LOG_ERROR("Failed to load book");
        return -1;
      }

      run_node(book, address, port);
      printf("Node stopped.\n");
    } break;
    case 'T': {
      if (!set_path("Thrift path", &thrift_path, optarg))
        return -1;
    } break;
    case 'B': {
      if (!set_path("Bootstrap path", &bootstrap_path, optarg))
        return -1;
    } break;
    default:
      print_help();
    }
  }

  if (path_set_only && !book)
    print_help();

  return 0;
}
