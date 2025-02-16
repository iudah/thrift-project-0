#include "book.h"
#include "json_parser.h"
#include "sha256.h"
#include <memory.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// static /*_Thread_local*/ map_pair_t *originator;

void load_genesis(book_t *book) {
  FILE *genesis_file =
      fopen("/mnt/sdcard/Jay/thrift-project-0/db/genesis.json", "r");

  if (!genesis_file)
    perror("thrift could not open genesis.json");

  json_node_t *json = parse_json(genesis_file);

  fclose(genesis_file);

  json_node_t *balances = json_get(json, "balances");

  char *key;
  json_value_t *value;
  json_iter(balances);
  while (!json_next(&key, &value)) {
    map_pair_t *map = map_get(book->balances, key, (void *)strcmp);
    if (!map)
      map_append_number(book->balances, key, value->value.number);
    else
      map->value.value.number += value->value.number;
  }

  // originator = map_get(book->balances, "originator", (void *)strcmp);

  return;
}

void effect_tx(map_t *balances, tx_t *tx) {
  if (tx->is_reward) {
    map_get(balances, "originator", (void *)strcmp)->value.value.number +=
        tx->amount;
    return;
  }

  map_pair_t *sender = map_get(balances, tx->from, (void *)strcmp);
  if (!sender) {
    printf("account (%s) not found", tx->from);
    return;
  }

  volatile double net = tx->amount + tx->amount * .0000001;
  if (sender->value.value.number < net) {
    printf("%s: insufficient funds: %f < %f", tx->from, net,
           sender->value.value.number);
    return;
  }

  sender->value.value.number -= net;
  map_get(balances, "originator", (void *)strcmp)->value.value.number +=
      tx->amount * .0000001;

  map_pair_t *reciever = map_get(balances, tx->to, (void *)strcmp);
  if (!reciever)
    map_append_number(balances, tx->to, tx->amount);
  else
    reciever->value.value.number += tx->amount;
}

void hash_txs(FILE *f, char hash[65]) {
  fseek(f, 0, SEEK_SET);

  struct stat st;
  fstat(fileno(f), &st);

  unsigned char bytes[st.st_size];
  
  fread(bytes, 1, st.st_size, f);

// char * b=(char*)bytes;
//   while((fgets(b,256,  f))){
//     b+=strlen(b);
//   }

  SHA256_CTX ctx;

  sha256_init(&ctx);
  char buf[256];

  // while((fgets(buf,sizeof(buf),  f))){
sha256_update(&ctx, bytes, sizeof(bytes));
// }
  sha256_final(&ctx, (uint8_t *)hash);

  FILE *hash_file = fopen("/mnt/sdcard/Jay/thrift-project-0/db/hash.hash", "w");
  fwrite(hash, 65, 1, hash_file);
  fclose(hash_file);
}

void print_hash(char *hash) {
  for (int i = 0; i < 32; i++) {
    printf("%x", hash[i]);
  }
}

bool verify_hash(book_t *book,FILE *tx_file){char hash[65];
  hash_txs(tx_file, hash);
  for (int i = 0; i < 32; i++) {
    if (hash[i] != book->hash[i]) {
      printf("Error: Invalid record. Expected ");print_hash(hash);
      printf(" got ");print_hash(book->hash);
      puts(".\n");
      return false;
    }
  }return true;}

void load_txs(book_t *book) {
  FILE *tx_file = fopen("/mnt/sdcard/Jay/thrift-project-0/db/txs.json", "r");

  if (!tx_file)
    perror("thrift could not open genesis.json");

  json_node_t *txs = parse_json(tx_file);

bool is_valid_book=verify_hash(book, tx_file);

  fclose(tx_file);

// if(!is_valid_book)return ;

  // json_node_t *txs = json_get(json, "balances");

  char *key;
  json_value_t *value;
  json_node_t *tx = txs;
  while (tx) {
    json_iter(tx);
    tx_t tx_;
    while (!json_next(&key, &value)) {
      if (!strncmp(key, "from", 4))
        tx_.from = value->value.string;
      else if (!strncmp(key, "to", 2))
        tx_.to = value->value.string;
      else if (!strncmp(key, "value", 5))
        tx_.amount = value->value.number;
      else if (!strncmp(key, "from", 4))
        tx_.is_reward = value->value.bool_;
    }
    effect_tx(book->balances, &tx_);
    tx = tx->next_pair;
  }

  

  return;
}

book_t *load_book() {
  book_t *book = calloc(1, sizeof(*book));
  book->balances = new_map();
  book->txs = new_array();

  load_genesis(book);

  FILE *hash_file = fopen("/mnt/sdcard/Jay/thrift-project-0/db/hash.hash", "r");
  fread(book->hash, sizeof(book->hash), 1, hash_file);
  fclose(hash_file);

  load_txs(book);

  return book;
}


void show_balances(book_t *book) {
  printf("Accounts                   Balances\n");
  printf("-----------------------------------\n");
  map_iter(book->balances);
  char *account;
  ds_value_t *bal;
  while (!map_next((void **)&account, &bal)) {
    printf("%-18s %18f\n", account, bal->value.number);
  }

  printf("Tx Hash: ");
  print_hash(book->hash);
  putchar('\n');
}

static char *empty_str = "";
static int add_tx_xxx(book_t *book, char *from, char *to, double amount,
                      char *purpose, void *xxx) {
  tx_t tx_ = {from, to, purpose ? purpose : empty_str, amount,
              xxx == add_tx_xxx};
  tx_t *tx = malloc(sizeof(*tx));
  memcpy(tx, &tx_, sizeof(tx_));

  array_append_ptr(book->txs, tx);
  effect_tx(book->balances, tx);
  return 0;
}

int add_tx(book_t *book, char *from, char *to, double amount, char *purpose) {
  return add_tx_xxx(book, from, to, amount, purpose, NULL);
}

int add_reward(book_t *book) {
  return add_tx_xxx(book, "originator", "originator", 70, "reward", add_tx_xxx);
}

int save_txs(book_t *book) {
  FILE *tx_file = fopen("/mnt/sdcard/Jay/thrift-project-0/db/txs.json", "a");

  if (!tx_file)
    perror("thrift could not open genesis.json");

  uint64_t i = 0;
  while (i < book->txs->length) {
    tx_t *tx = book->txs->values[i].value.ptr;

    char tx_str[1024];

    uint64_t len =
        snprintf(tx_str, sizeof(tx_str),
                 "{\"from\":\"%s\",\"to\":\"%s\",\"value\":%f,\"for\":\"%s\","
                 "\"reward\":%s}\n",
                 tx->from, tx->to, tx->amount, tx->purpose,
                 tx->is_reward ? "true" : "false");
fputs(tx_str,tx_file);
    puts(tx_str);

    i++;
  }


  fclose(tx_file);

tx_file = fopen("/mnt/sdcard/Jay/thrift-project-0/db/txs.json", "r");
  hash_txs(tx_file, book->hash);
  fclose(tx_file);

  printf("New hash: ");
  print_hash(book->hash);
  putchar('\n');

  return 0;
}
