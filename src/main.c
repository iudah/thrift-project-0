#include <Thrift-Project.h>
#include <bits/getopt.h>
#include <getopt.h>
#include <stdio.h>
#define _XOPEN_SOURCE 500
#include <stdlib.h>
#include <string.h>

int getsubopt(char **optionp, char *const *keylistp, char **valuep);

#include "book.h"

enum { ADD_TX, SHOW_BALS } action;
enum { FROM = 0, TO, AMOUNT, NOTE };
char *const tokens[] = {
    [FROM] = "from", [TO] = "to", [AMOUNT] = "amount", [NOTE] = "note"};

void print_help() {
  printf("Usage:\n"
         "thrift --add-tx "
         "from=<sender>,to=<reciever>,amount=<amount>[,note=<note>]\n"
         "thrift --show-balances\n");
}

int main(int argc, char *argv[]) {
  printf("Thrift Project CLI\n\
Version %d.%d.%d\n",
         THRIFT_PROJECT_VERSION_MAJOR, THRIFT_PROJECT_VERSION_MINOR,
         THRIFT_PROJECT_VERSION_PATCH);

  if (argc < 2) {
    print_help();
  }

  book_t *book = load_book();

  static struct option long_options[] = {{"add-tx", required_argument, 0, 'a'},
                                         {"add-reward", no_argument, 0, 'r'},
                                         {"show-balances", no_argument, 0, 'b'},
                                         {NULL, 0, NULL, 0}};

  int opt_indx = 0;
  int c = 0;

  while ((c = getopt_long(argc, argv, "a:rb", long_options, &opt_indx)) != -1) {
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
      add_tx(book, tx.from, tx.to, tx.amount, tx.purpose);
      save_txs(book);
      printf("Txs updated successfully.\n");
    } break;
    case 'r':
      add_reward(book);
      save_txs(book);
      printf("Reward updated successfully.\n");
      break;
    case 'b':
      show_balances(book);
      break;
    default:
      print_help();
    }
  }

  return 0;
}
