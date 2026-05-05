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
               "{\"from\":\"%s\",\"to\":\"%s\",\"value\":%f,\"for\":\"%s\","
               "\"reward\":%s}",
               tx->from, tx->to, tx->amount, tx->purpose,
               tx->is_reward ? "true" : "false");

  return string_len;
}

uint64_t block_to_str(char *buffer, uint64_t len, block_t *blk) {

  time_t now = time(NULL);
  struct tm *tm_info = gmtime(&now);

  strftime(blk->block_time, sizeof(blk->block_time), "%FT%T", tm_info);

  uint32_t n_tx = array_length(blk->txs);

  char buf[2048];
  uint64_t tx_ln = 0;
  for (uint64_t i = 0; i < n_tx; i++) {
    tx_t *tx = array_get_ptr(blk->txs, i);
    if (i) {
      buf[tx_ln++] = ',';
      buf[tx_ln] = 0;
    }
    tx_ln += tx_to_str(buf + tx_ln, sizeof(buf) - tx_ln, tx);
  }

  len = snprintf(buffer, len,
                 "{\"parent_hash\": \"%s\","
                 "\"block_time\": \"%sZ\","
                 "\"block_index\": %i,"
                 "\"txs\": [%s]}",
                 blk->parent_hash, blk->block_time, blk->block_idx, buf);

  return len;
}

bool tx_from_json(tx_t *tx, json_value *saved_tx, book_t *book) {

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
    // Why is this here?
    // LOG("Signature not found");
    memcpy(tx->signature,
           hex_decode(json_value_data(q).string,
                      strlen(json_value_data(q).string)),
           sizeof(tx->signature));
  } else {
    LOG("Signature not found");
  }

  q = json_query(saved_tx, "public_key");
  if (q) {
    LOG("Public key not found");
    tx->public_key = hex_decode(json_value_data(q).string,
                                strlen(json_value_data(q).string));
  }

  return effect_tx(book->balances, book->users, tx);
}

bool block_from_json(block_t *blk, json_value *saved_blk, book_t *book) {

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
    if (tx_from_json(&tx, array_get_ptr(list, i), book)) {
      auto tx_ = zmalloc(sizeof(tx));
      memcpy(tx_, &tx, sizeof(tx));
      array_append_ptr(blk->txs, tx_);
    }
  }

  return true;
}
