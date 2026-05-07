#include "book.h"
#include "ds.h"
#include "ison.h"
#include "zot.h"
#include <stdint.h>
#include <string.h>

uint64_t tx_to_str(char *buffer, uint64_t len, tx_t *tx) {
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
               tx->from, tx->to, tx->amount, tx->purpose,
               tx->is_reward ? "true" : "false",
               hash_to_string((char *)tx->public_key, 32, (char[65]){0}),
               hash_to_string((char *)tx->signature, 64, (char[129]){0}));

  return string_len;
}

uint64_t block_to_str(char *buffer, uint64_t len, block_t *blk) {

  uint32_t n_tx = array_length(blk->txs);

#define BUF_SIZE (4096)
  uint64_t buf_space = BUF_SIZE;
  char buf[BUF_SIZE] = {0};
  uint64_t tx_ln = 0;
  char tmp[1024] = {0};

  for (uint64_t i = 0; i < n_tx; i++) {
    tx_t *tx = array_get_ptr(blk->txs, i);
    if (i) {
      strncat(buf, ",", 1);
    }
    tx_ln = tx_to_str(tmp, sizeof(tmp), tx);
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

bool tx_from_json(tx_t *tx, json_value *saved_tx) {

  if (json_value_type(saved_tx) != DICT) {
    return false;
  }

  json_value *q;

  q = json_query(saved_tx, "value");
  tx->amount = json_value_data(q).number;

  q = json_query(saved_tx, "from");
  tx->from = json_value_data(q).string;

  q = json_query(saved_tx, "to");
  tx->to = json_value_data(q).string;

  q = json_query(saved_tx, "reward");
  tx->is_reward = q != NULL && json_value_data(q).boolean;

  q = json_query(saved_tx, "for");
  tx->purpose = q ? json_value_data(q).string : NULL;
  if (!tx->purpose) {
    tx->purpose = zstrdup("");
  }

  q = json_query(saved_tx, "signature");
  if (q) {
    memcpy(tx->signature, hex_decode(json_value_data(q).string, 128), 64);
    tx->signature[64] = 0;
  } else {
    LOG("Signature not found");
    return false;
  }

  q = json_query(saved_tx, "public_key");
  if (q) {
    memcpy(tx->public_key, hex_decode(json_value_data(q).string, 64), 32);
    tx->public_key[32] = 0;
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

  json_value *q;

  q = json_query(saved_blk, "parent_hash");
  auto tmp = q ? json_value_data(q).string : NULL;
  if (tmp) {
    memcpy(blk->parent_hash, tmp, sizeof(blk->parent_hash));
  } else {
    blk->parent_hash[0] = 0;
  }

  q = json_query(saved_blk, "block_time");
  tmp = q ? json_value_data(q).string : NULL;
  if (tmp) {
    memcpy(blk->block_time, tmp, sizeof(blk->block_time));
  } else {
    blk->block_time[0] = 0;
  }

  q = json_query(saved_blk, "block_index");
  blk->block_idx = q ? json_value_data(q).integer : 0;

  q = json_query(saved_blk, "txs");
  if (!q || json_value_type(q) != LIST) {
    return false;
  }

  blk->txs = create_array();
  auto list = json_value_data(q).ptr;
  auto len = array_length(list);
  for (uint32_t i = 0; i < len; ++i) {
    tx_t tx;
    if (tx_from_json(&tx, array_get(list, i))) {
      auto tx_ = zmalloc(sizeof(tx));
      memcpy(tx_, &tx, sizeof(tx));
      array_append_ptr(blk->txs, tx_);
    }
  }

  return true;
}
