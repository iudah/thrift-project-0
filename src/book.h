#ifndef BOOK_H
#define BOOK_H

#include "array.h"
#include "map.h"

typedef struct book {
  map_t *balances;
  array_t *txs;
} book_t;

typedef struct {
  char *from;
  char *to;
  char *purpose;
  double amount;
  char is_reward;
} tx_t;

book_t *load_book();
void show_balances(book_t *book);
int add_tx(book_t *book, char *from, char *to, double amount, char *purpose);
int add_reward(book_t *book);
int save_txs(book_t *book);

#endif