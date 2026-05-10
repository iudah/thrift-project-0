#include "book.h"

#include <linux/stat.h>
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

char *bootstrap_path = nullptr;
char *thrift_path = nullptr;

char *unsigned_tx_to_str(tx_t *txn, uint64_t *len);

static bool add_tx_xxx(book_t *book, char *from, char *txn, double amount,
                       char *purpose, void *xxx);

bool effect_tx(hash_map *balances, array_t *users, tx_t *txn) {

  if (!is_valid_tx(txn)) {
    LOG("Transaction is invalid");
    return false;
  }

  double reward = REWARD;

  if (!txn->is_reward || strncmp(txn->from, SYSTEM, strlen(SYSTEM))) {
    map_value *sender = hash_map_get(balances, txn->from);
    if (!sender) {
      LOG_ERROR("Sender account '%s' not found", txn->from);
      return false;
    }
    if (json_value_type(sender) != FLOATS) {
      return false;
    }

    volatile double net = txn->amount;
    double s_balance = json_value_data(sender).number;
    if (s_balance < net) {
      LOG_ERROR("Insufficient funds for %s: %.10f < required %.10f", txn->from,
                s_balance, net);
      return false;
    }

    s_balance -= net;
    hash_map_replace_number(balances, txn->from, s_balance);
  }

  map_value *receiver = hash_map_get(balances, txn->to);
  if (receiver) {
    if (json_value_type(receiver) != FLOATS) {
      return false;
    }
    double r_balance = json_value_data(receiver).number + txn->amount;
    hash_map_replace_number(balances, txn->to, r_balance);
  } else {
    array_append_str(users, txn->to);
    hash_map_add_number(balances, txn->to, txn->amount);
  }

  return true;
}

static char *empty_str = "";
bool add_tx(node_context_t *ctx, char *reciever, double amount, char *purpose,
            bool reward) {

  uint64_t str_len = 0;
  char buffer[BUFFER_BIG] = {0};
  tx_t txn = {.from = get_address_from_keystore(ctx->user_keystore),
              reciever,
              purpose ? purpose : "",
              amount,
              reward};

  if (!sign_buffer(txn.signature, unsigned_tx_to_str(&txn, &str_len), str_len,
                   ctx->user_keystore))
    return false;

  if (!effect_tx(ctx->book.balances, ctx->book.users, &txn)) {
    return false;
  }

  copy_public_key(ctx->user_keystore, &txn);

  tx_t *txn_ptr = zmalloc(sizeof(*txn_ptr));
  memcpy(txn_ptr, &txn, sizeof(txn));

  array_append_ptr(ctx->book.block.txs, txn_ptr);
  return true;
}

bool show_balances(book_t *book) {
  printf("Accounts                   Balances\n");
  printf("-----------------------------------\n");

  uint32_t n_user = array_length(book->users);
  for (uint32_t i = 0; i < n_user; i++) {
    char *account = array_get_ptr(book->users, i);
    map_value *usr_bal_json = hash_map_get(book->balances, account);
    printf("%-18s %18.10f\n", account, json_value_data(usr_bal_json).number);
  }

  printf("Tx Hash: %s.\n", book->block.parent_hash);
  return true;
}

bool add_reward(book_t *book) {
  // return add_tx_xxx(book, "originator", "originator", 70, "reward",
  // add_tx_xxx);
  return true;
}

char *unsigned_tx_to_str(tx_t *txn, uint64_t *len) {
  char tx_json[BUFFER_SMALL];
  *len = snprintf(tx_json, BUFFER_SMALL,
                  ""
                  "{"
                  "\"from\":\"%s\","
                  "\"to\":\"%s\","
                  "\"value\":%.10f,"
                  "\"for\":\"%s\","
                  "\"reward\":%s"
                  "}",
                  txn->from, txn->to, txn->amount, txn->purpose,
                  (int)txn->is_reward ? "true" : "false");
  return zstrdup(tx_json);
}

bool sign_tx(tx_t *txn, keystore *usr_ks) {
  uint64_t len;
  char *tx_str = unsigned_tx_to_str(txn, &len);
  return sign_buffer(txn->signature, tx_str, len, usr_ks);
}

bool is_valid_tx(tx_t *txn) {
  uint64_t len;
  char *tx_str = unsigned_tx_to_str(txn, &len);
  return verify_buffer(txn->signature, tx_str, len, txn->public_key);
}

bool set_block_time(block_t *blk) {
  if (!blk)
    return false;

  time_t now = time(nullptr);
  struct tm *tm_info = gmtime(&now);

  strftime(blk->block_time, sizeof(blk->block_time), "%FT%T", tm_info);

  return true;
}

bool audit_block(block_t *blk) {
  auto ntx = array_length(blk->txs);
  double reward = REWARD;
  for (uint64_t i = 0; i < ntx; ++i) {
    tx_t *txn = array_get_ptr(blk->txs, i);
    if (txn->is_reward) {
      if (strncmp(txn->from, SYSTEM, strlen(SYSTEM)))
        continue;
      if (txn->amount > reward && blk->block_idx > 0) {
        LOG_ERROR("Excessive reward; likely invalid tx.");
        return false;
      }
    }
  }
  return true;
}

bool effect_block(block_t *blk, book_t *book) {
  auto ntx = array_length(blk->txs);
  double reward = REWARD;
  for (uint64_t i = 0; i < ntx; ++i) {
    tx_t *txn = array_get_ptr(blk->txs, i);
    if (!effect_tx(book->balances, book->users, txn))
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

bool load_book(book_t *book) {
  auto home = get_thrift_home();

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

  return true;
}
