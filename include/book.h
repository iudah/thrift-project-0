#ifndef BOOK_H
#define BOOK_H

#include "array.h"
#include "map.h"

typedef struct book {
  map_t *balances;
  array_t *txs;
  char hash[65];
} book_t;

typedef struct {
  char *from;
  char *to;
  char *purpose;
  double amount;
  char is_reward;
} tx_t;

book_t *load_book();
bool show_balances(book_t *book);
bool add_tx(book_t *book, char *from, char *to, double amount, char *purpose);
bool add_reward(book_t *book);
bool save_txs(book_t *book);

#endif