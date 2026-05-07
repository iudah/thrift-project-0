#include "book.h"

#include <memory.h>
#include <sodium.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zot.h>

#include "ds.h"
#include "genesis_header.h"
#include "ison.h"

#define HASH_STR_LEN 65
#define HASH_BYTE_LEN 33
#define BUFFER_LENGTH 4096

char *GENESIS_FILE_PATH = 0;
char *BLOCKS_FILE_PATH = 0;
char *BLOCKS_TMP_FILE_PATH = 0;
char *HASH_FILE_PATH = 0;
char *INCOMPLETE_BLOCK_FILE_PATH = 0;
char *KEYSTORE_FILE_PATH = 0;

char *bootstrap_path = NULL;
char *thrift_path = NULL;

char *unsigned_tx_to_str(tx_t *tx, uint64_t *len);

static bool add_tx_xxx(book_t *book, char *from, char *to, double amount,
                       char *purpose, void *xxx);

void make_thrift_dirs(char *path) {
  char *slash = path;
  while ((slash = strstr(slash, "/"))) {
    *slash = 0;
    if (path[0] != 0 && access(path, F_OK) != 0) {
      mkdir(path, 0777);
      LOG("mkdir(%s)", path);
    }
    *slash = '/';
    ++slash;
  }
  LOG("mkdir(%s)", path);
}

bool effect_tx(hash_map *balances, array_t *users, tx_t *tx) {

  if (!is_valid_tx(tx)) {
    LOG("Transaction is invalid");
    return false;
  }

  double REWARD = 64;

  if (!tx->is_reward || strncmp(tx->from, "SYSTEM", 6)) {
    map_value *sender = hash_map_get(balances, tx->from);
    if (!sender) {
      LOG_ERROR("Sender account '%s' not found", tx->from);
      return false;
    }
    if (json_value_type(sender) != FLOATS) {
      return false;
    }

    volatile double net = tx->amount;
    double s_balance = json_value_data(sender).number;
    if (s_balance < net) {
      LOG_ERROR("Insufficient funds for %s: %.10f < required %.10f", tx->from,
                s_balance, net);
      return false;
    }

    s_balance -= net;
    hash_map_replace_number(balances, tx->from, s_balance);
  }

  map_value *receiver = hash_map_get(balances, tx->to);
  if (receiver) {
    if (json_value_type(receiver) != FLOATS) {
      return false;
    }
    double r_balance = json_value_data(receiver).number + tx->amount;
    hash_map_replace_number(balances, tx->to, r_balance);
  } else {
    array_append_str(users, tx->to);
    hash_map_add_number(balances, tx->to, tx->amount);
  }

  return true;
}

bool add_tx(book_t *book, char *from, char *to, double amount, char *purpose) {
  return add_tx_xxx(book, from, to, amount, purpose, NULL);
}

static char *empty_str = "";
static bool add_tx_xxx(book_t *book, char *from, char *to, double amount,
                       char *purpose, void *xxx) {
  tx_t tx_ = {.from = from,
              to,
              purpose ? purpose : empty_str,
              amount,
              xxx == add_tx_xxx};
  tx_t *tx = zmalloc(sizeof(*tx));
  memcpy(tx, &tx_, sizeof(tx_));

  if (!effect_tx(book->balances, book->users, tx)) {
    return false;
  }
  array_append_ptr(book->block.txs, tx);
  return true;
}

bool show_balances(book_t *book) {
  printf("Accounts                   Balances\n");
  printf("-----------------------------------\n");

  uint32_t n_user = array_length(book->users);
  for (uint32_t i = 0; i < n_user; i++) {
    char *account = array_get_ptr(book->users, i);
    map_value *q = hash_map_get(book->balances, account);
    printf("%-18s %18f\n", account, json_value_data(q).number);
  }

  printf("Tx Hash: %s.\n", book->block.parent_hash);
  return true;
}

bool add_reward(book_t *book) {
  // return add_tx_xxx(book, "originator", "originator", 70, "reward",
  // add_tx_xxx);
  return true;
}

char *unsigned_tx_to_str(tx_t *tx, uint64_t *len) {
  char tx_json[1024];
  *len = snprintf(tx_json, 1024,
                  ""
                  "{"
                  "\"from\":\"%s\","
                  "\"to\":\"%s\","
                  "\"value\":%.10f,"
                  "\"for\":\"%s\","
                  "\"reward\":%s"
                  "}",
                  tx->from, tx->to, tx->amount, tx->purpose,
                  tx->is_reward ? "true" : "false");
  return zstrdup(tx_json);
}

bool sign_tx(tx_t *tx, keystore *ks) {
  uint64_t len;
  char *tx_str = unsigned_tx_to_str(tx, &len);
  if (!sign_buffer(tx->signature, tx_str, len, ks)) {
    return false;
  }
  return true;
}

bool is_valid_tx(tx_t *tx) {
  uint64_t len;
  char *tx_str = unsigned_tx_to_str(tx, &len);
  return verify_buffer(tx->signature, tx_str, len, tx->public_key);
}

bool set_block_time(block_t *blk) {
  if (!blk)
    return false;

  time_t now = time(NULL);
  struct tm *tm_info = gmtime(&now);

  strftime(blk->block_time, sizeof(blk->block_time), "%FT%T", tm_info);

  return true;
}

bool audit_block(block_t *blk) {
  auto ntx = array_length(blk->txs);
  double REWARD = 64;
  for (uint64_t i = 0; i < ntx; ++i) {
    tx_t *tx = array_get_ptr(blk->txs, i);
    if (tx->is_reward) {
      if (strncmp(tx->from, "SYSTEM", 6))
        continue;
      if (tx->amount > REWARD && blk->block_idx > 0) {
        LOG_ERROR("Excessive reward; likely invalid tx.");
        return false;
      }
    }
  }
  return true;
}

bool effect_block(block_t *blk, book_t *book) {
  auto ntx = array_length(blk->txs);
  double REWARD = 64;
  for (uint64_t i = 0; i < ntx; ++i) {
    tx_t *tx = array_get_ptr(blk->txs, i);
    if (!effect_tx(book->balances, book->users, tx))
      return false;
  }
  return true;
}

bool mine_genesis_block(book_t *book) {

  block_t blk;
  auto block_json = json_parse_string((char *)genesis_json);
  if (!block_from_json(&blk, block_json))
    return false;

  if (!audit_block(&blk))
    return false;

  if (!effect_block(&blk, book))
    return false;

  if (!save_block(&blk))
    return false;

  hash_block(&blk, book->block.parent_hash);

  return true;
}

book_t *load_book() {
  auto home = get_thrift_home();

  book_t *book = zmalloc(sizeof(*book));
  book->users = create_array();
  book->balances = create_hash_map();

  auto head_txt = join_path(home, "HEAD.txt");
  if (file_exists(head_txt)) {
    if (!replay_txs(book, home))
      return NULL;
  } else {
    if (!setup_file_structure(home) || !mine_genesis_block(book)) {
      return NULL;
    }
  }

  book->block.txs = create_array();

  return book;
}
