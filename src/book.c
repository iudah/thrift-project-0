#include "book.h"
#include "json_parser.h"
#include "sha256.h"
#include <memory.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define HASH_STR_LEN 65
#define HASH_BYTE_LEN 33

#define GENESIS_FILE_PATH "/mnt/sdcard/Jay/thrift-project-0/db/genesis.json"
#define TXS_FILE_PATH "/mnt/sdcard/Jay/thrift-project-0/db/txs.json"
#define TXS_TMP_FILE_PATH "/mnt/sdcard/Jay/thrift-project-0/db/~txs.json"

char *hash_to_string(char *hash, char *buf, int buffer_sz);

bool load_genesis(book_t *book) {
  FILE *genesis_file = fopen(GENESIS_FILE_PATH, "r");

  if (!genesis_file) {
    perror("thrift could not open genesis.json");
    return false;
  }

  json_node_t *json = parse_json(genesis_file);

  fclose(genesis_file);

  // json_node_t *balances =calloc(1,sizeof(*balances));// json_get(json,
  // "balances");
  json_value_t *root_value = json_get_value(json, NULL);
  json_value_t *balance_value = json_get_value(
      root_value->value.node, "balances"); // json_get(json, "balances");
  json_node_t *balances = balance_value->value.node;

  char *key;
  json_value_t *value;
  json_iter_t *balances_iter_state = json_iter(balances);
  while (!json_next(balances_iter_state, &key, &value)) {
    map_pair_t *map = map_get(book->balances, key, (void *)strcmp);
    if (!map)
      map_append_number(book->balances, key, value->value.number);
    else
      map->value.value.number += value->value.number;
  }

  return true;
}

bool effect_tx(map_t *balances, tx_t *tx) {
  if (tx->is_reward) {
    map_get(balances, "originator", (void *)strcmp)->value.value.number +=
        tx->amount;
    return true;
  }

  map_pair_t *sender = map_get(balances, tx->from, (void *)strcmp);
  if (!sender) {
    printf("account (%s) not found", tx->from);
    return false;
  }

  volatile double net = tx->amount + tx->amount * .0000001;
  if (sender->value.value.number < net) {
    printf("%s: insufficient funds: %f < %f", tx->from, net,
           sender->value.value.number);
    return false;
  }

  sender->value.value.number -= net;
  map_get(balances, "originator", (void *)strcmp)->value.value.number +=
      tx->amount * 1e-6;

  map_pair_t *reciever = map_get(balances, tx->to, (void *)strcmp);
  if (!reciever)
    map_append_number(balances, tx->to, tx->amount);
  else
    reciever->value.value.number += tx->amount;

  return true;
}

bool hash_txs(FILE *f, char hash[65]) {
  fseek(f, 0, SEEK_SET);

  struct stat st;
  fstat(fileno(f), &st);

  unsigned char bytes[st.st_size];

  fread(bytes, 1, st.st_size, f);

  SHA256_CTX ctx;

  sha256_init(&ctx);
  char buf[256];

  sha256_update(&ctx, bytes, sizeof(bytes));
  sha256_final(&ctx, (uint8_t *)hash);

  FILE *hash_file = fopen("/mnt/sdcard/Jay/thrift-project-0/db/hash.hash", "w");
  fwrite(hash, 65, 1, hash_file);
  fclose(hash_file);

  return true;
}

void print_hash(char *hash) {
  for (int i = 0; i < 32; i++) {
    printf("%x", hash[i]);
  }
}

bool verify_hash(book_t *book, FILE *tx_file) {
  char hash[65];
  hash_txs(tx_file, hash);
  for (int i = 0; i < 32; i++) {
    if (hash[i] != book->hash[i]) {
      printf("Error: Invalid record. Expected ");
      print_hash(hash);
      printf(" got ");
      print_hash(book->hash);
      puts(".\n");
      return false;
    }
  }
  return true;
}

bool load_txs(book_t *book) {
  FILE *tx_file = fopen(TXS_FILE_PATH, "r");

  if (!tx_file) {
    perror("thrift could not open txs.json");
    return false;
  }
  // json_node_t *txs = parse_json(tx_file);
  json_node_t *root = parse_json(tx_file);
  if (!root) {
    fprintf(stderr, "could not load json.");
    return false;
  }
  array_t *txs;
  txs = root->value.value.array;

  bool is_valid_book = verify_hash(book, tx_file);

  fclose(tx_file);

  if (!is_valid_book)
    return false;

  char *key;
  json_value_t *value;
  json_node_t *tx; //= txs;
  json_iter_t *tx_state;
  for (uint64_t i = 0; i < txs->length; i++) {
    tx = array_get_ptr(txs, i);
    tx_state = json_iter(tx);
    tx_t tx_;
    while (!json_next(tx_state, &key, &value)) {
      if (!strncmp(key, "from", 4))
        tx_.from = value->value.string;
      else if (!strncmp(key, "to", 2))
        tx_.to = value->value.string;
      else if (!strncmp(key, "value", 5))
        tx_.amount = value->value.number;
      else if (!strncmp(key, "reward", 6))
        tx_.is_reward = value->value.boolean;
    }
    if (!effect_tx(book->balances, &tx_)) {
      return false;
    }
    tx = tx->next_pair;
  }

  return true;
}

book_t *load_book() {
  book_t *book = calloc(1, sizeof(*book));
  book->balances = new_map();
  book->txs = new_array();

  if (!load_genesis(book)) {
    return NULL;
  }

  FILE *hash_file = fopen("/mnt/sdcard/Jay/thrift-project-0/db/hash.hash", "r");
  fread(book->hash, sizeof(book->hash), 1, hash_file);
  fclose(hash_file);

  if (!load_txs(book)) {
    return NULL;
  }

  return book;
}

bool show_balances(book_t *book) {
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
  return true;
}

static char *empty_str = "";
static bool add_tx_xxx(book_t *book, char *from, char *to, double amount,
                       char *purpose, void *xxx) {
  tx_t tx_ = {from, to, purpose ? purpose : empty_str, amount,
              xxx == add_tx_xxx};
  tx_t *tx = malloc(sizeof(*tx));
  memcpy(tx, &tx_, sizeof(tx_));

  array_append_ptr(book->txs, tx);
  if (!effect_tx(book->balances, tx)) {
    return false;
  }
  return true;
}

bool add_tx(book_t *book, char *from, char *to, double amount, char *purpose) {
  return add_tx_xxx(book, from, to, amount, purpose, NULL);
}

bool add_reward(book_t *book) {
  return add_tx_xxx(book, "originator", "originator", 70, "reward", add_tx_xxx);
}

bool save_txs(book_t *book) {
  FILE *tx_file = fopen(TXS_FILE_PATH, "r");

  bool found;
  uint64_t last_occurence = 0, k = 0;
  char buffer[4096];
  while ((fgets(buffer, sizeof(buffer), tx_file))) {
    char *c = buffer;
    while (*c) {
      switch (*c) {
      case ']':
        found = true;
        last_occurence = k;
        break;
      case ' ':
      case '\n':
      case '\t':
      case '\r':
        break;
      default:
        if (found)
          found = false;
      }
      c++;
      k++;
      if ((int)*c == EOF) {
        break;
      }
    }
  }
  if (!found) {
    fprintf(stderr, "invalid json.");
    return false;
  }
  printf("%llu", last_occurence);
  fflush(stdout);
  fclose(tx_file);
  rename(TXS_FILE_PATH, TXS_TMP_FILE_PATH);
  FILE *tx_tmp_file = fopen(TXS_TMP_FILE_PATH, "r");
  tx_file = fopen(TXS_FILE_PATH, "w");

  int len;
  while (last_occurence > 0) {
    len = fread(buffer, 1, sizeof(buffer), tx_tmp_file);
    if (last_occurence <= len) {
      buffer[last_occurence] = ',';
      last_occurence = 0;
    } else {
      last_occurence -= len;
    }
    fwrite(buffer, 1, len, tx_file);
  }
  fclose(tx_tmp_file);

  if (!tx_file)
    perror("thrift could not open txs.json");

  uint64_t i = 0;
  char tx_str[1024];
  while (i < book->txs->length) {
    tx_t *tx = book->txs->values[i].value.ptr;

    uint64_t len =
        snprintf(tx_str, sizeof(tx_str),
                 "{\"from\":\"%s\",\"to\":\"%s\",\"value\":%f,\"for\":\"%s\","
                 "\"reward\":%s}]\n",
                 tx->from, tx->to, tx->amount, tx->purpose,
                 tx->is_reward ? "true" : "false");
    fputs(tx_str, tx_file);
    puts(tx_str);

    i++;
  }

  fclose(tx_file);

  tx_file = fopen(TXS_FILE_PATH, "r");
  hash_txs(tx_file, book->hash);
  fclose(tx_file);

  printf("New hash: %s\n",
         hash_to_string(book->hash, (char[HASH_STR_LEN]){0}, HASH_STR_LEN));

  remove(TXS_TMP_FILE_PATH);

  return true;
}

char *hash_to_string(char *hash, char *buf, int buffer_sz) {
  snprintf(buf, buffer_sz,
           "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
           "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
           "%02x%02x%02x%02x%02x%02x",
           hash[0], hash[1], hash[2], hash[3], hash[4], hash[5], hash[6],
           hash[7], hash[8], hash[9], hash[10], hash[11], hash[12], hash[13],
           hash[14], hash[15], hash[16], hash[17], hash[18], hash[19], hash[20],
           hash[21], hash[22], hash[23], hash[24], hash[25], hash[26], hash[27],
           hash[28], hash[29], hash[30], hash[31]);

  return buf;
}