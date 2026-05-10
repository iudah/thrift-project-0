#include "book.h"
#include "ds.h"
#include "ison.h"
#include "zot.h"
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

uint64_t tx_to_str(char *buffer, uint64_t len, tx_t *txn) {
  if (!buffer)
    return 0;

  uint64_t string_len =
      snprintf(buffer, len,
               "    {"
               "      \"from\": \"%s\","
               "      \"to\": \"%s\","
               "      \"value\": %.10f,"
               "      \"for\": \"%s\","
               "      \"reward\": %s,"
               "      \"public_key\": \"%s\","
               "      \"signature\": \"%s\""
               "    }",
               txn->from, txn->to, txn->amount, txn->purpose,
               (int)txn->is_reward ? "true" : "false",
               hash_bin_to_hex(txn->public_key, SHA256_BIN_LEN,
                               (char[SHA256_HEX_BUF_LEN]){0}),
               hash_bin_to_hex(txn->signature, ED25519_BIN_LEN,
                               (char[ED25519_HEX_BUF_LEN]){0}));

  return string_len;
}

uint64_t block_to_str(char *buffer, uint64_t len, block_t *blk) {

  uint32_t n_tx = array_length(blk->txs);

  uint64_t buf_space = BUFFER_BIG;
  char buf[BUFFER_BIG] = {0};
  uint64_t tx_ln = 0;
  char tmp[BUFFER_SMALL] = {0};

  for (uint64_t i = 0; i < n_tx; i++) {
    tx_t *txn = array_get_ptr(blk->txs, i);
    if (i) {
      strncat(buf, ",", 1);
    }
    tx_ln = tx_to_str(tmp, sizeof(tmp), txn);
    strncat(buf, tmp, buf_space);
    buf_space -= tx_ln;
  }

  len = snprintf(buffer, len,
                 "{\"parent_hash\": \"%s\","
                 "\"block_time\": \"%s\","
                 "\"block_index\": %i,"
                 "\"txs\": [%s]}",
                 blk->parent_hash, blk->block_time, blk->block_idx, buf);

  return len;
}

bool tx_from_json(tx_t *txn, json_value *saved_tx) {

  if (json_value_type(saved_tx) != DICT) {
    return false;
  }

  json_value *query;

  query = json_query(saved_tx, "value");
  txn->amount = json_value_data(query).number;

  query = json_query(saved_tx, "from");
  txn->from = json_value_data(query).string;

  query = json_query(saved_tx, "to");
  txn->to = json_value_data(query).string;

  query = json_query(saved_tx, "reward");
  txn->is_reward = false;

  if (query != nullptr)
    txn->is_reward = json_value_data(query).boolean;

  query = json_query(saved_tx, "for");
  txn->purpose = query ? json_value_data(query).string : nullptr;
  if (!txn->purpose) {
    txn->purpose = zstrdup("");
  }

  query = json_query(saved_tx, "signature");
  if (query) {
    memcpy(txn->signature,
           hash_hex_to_bin(json_value_data(query).string, ED25519_HEX_LEN,
                           (uint8_t[ED25519_BIN_LEN + 1]){0}),
           ED25519_BIN_LEN + 1);
  } else {
    LOG("Signature not found");
    return false;
  }

  query = json_query(saved_tx, "public_key");
  if (query) {
    memcpy(txn->public_key,
           hash_hex_to_bin(json_value_data(query).string, SHA256_HEX_LEN,
                           (uint8_t[SHA256_BIN_LEN + 1]){0}),
           SHA256_BIN_LEN + 1);

  } else {
    LOG("Public key not found");
    return false;
  }

  return true;
}

bool block_from_json(block_t *blk, json_value *saved_blk) {

  if (json_value_type(saved_blk) != DICT) {
    return false;
  }

  json_value *query;

  query = json_query(saved_blk, "parent_hash");
  auto tmp = query ? json_value_data(query).string : nullptr;
  if (tmp) {
    memcpy(blk->parent_hash, tmp, sizeof(blk->parent_hash));
  } else {
    blk->parent_hash[0] = 0;
  }

  query = json_query(saved_blk, "block_time");
  tmp = query ? json_value_data(query).string : nullptr;
  if (tmp) {
    memcpy(blk->block_time, tmp, sizeof(blk->block_time));
  } else {
    blk->block_time[0] = 0;
  }

  query = json_query(saved_blk, "block_index");
  blk->block_idx = query ? json_value_data(query).integer : 0;

  query = json_query(saved_blk, "txs");
  if (!query || json_value_type(query) != LIST) {
    return false;
  }

  blk->txs = create_array();
  auto list = json_value_data(query).ptr;
  auto len = array_length(list);
  for (uint32_t i = 0; i < len; ++i) {
    tx_t txn;
    if (tx_from_json(&txn, array_get(list, i))) {
      auto txn_ptr = zmalloc(sizeof(txn));
      memcpy(txn_ptr, &txn, sizeof(txn));
      array_append_ptr(blk->txs, txn_ptr);
    }
  }

  return true;
}
