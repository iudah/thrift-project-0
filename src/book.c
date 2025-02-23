#include "book.h"

#include <errno.h>
#include <memory.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "error.h"
#include "json_parser.h"
#include "sha256.h"

#define HASH_STR_LEN 65
#define HASH_BYTE_LEN 33
#define BUFFER_LENGTH 4096

#define GENESIS_FILE_PATH "/mnt/sdcard/Jay/thrift-project-0/db/genesis.json"
#define BLOCKS_FILE_PATH "/mnt/sdcard/Jay/thrift-project-0/db/blocks.json"
#define BLOCKS_TMP_FILE_PATH "/mnt/sdcard/Jay/thrift-project-0/db/~blocks.json"
#define HASH_FILE_PATH "/mnt/sdcard/Jay/thrift-project-0/db/hash.hash"
#define INCOMPLETE_BLOCK_FILE_PATH \
  "/mnt/sdcard/Jay/thrift-project-0/db/incomplete_block.json"

char *hash_to_string(char *hash, char *buf, int buffer_sz);

bool load_genesis(book_t *book) {
  FILE *genesis_file = fopen(GENESIS_FILE_PATH, "r");

  if (!genesis_file) {
    LOG_ERROR("Could not open genesis.json: %s", strerror(errno));
    return false;
  }

  json_node_t *json = parse_json(genesis_file);

  fclose(genesis_file);

  json_value_t *root_value = json_get_value(json, NULL);
  json_value_t *balance_value =
      json_get_value(root_value->value.node, "balances");
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
    map_pair_t *originator_pair =
        map_get(balances, "originator", (void *)strcmp);
    if (!originator_pair) {
      LOG_ERROR("Originator account missing in reward transaction");
      return false;
    }
    originator_pair->value.value.number += tx->amount;
    return true;
  }

  map_pair_t *sender = map_get(balances, tx->from, (void *)strcmp);
  if (!sender) {
    LOG_ERROR("Sender account '%s' not found", tx->from);
    return false;
  }

  volatile double net = tx->amount + tx->amount * 1e-6;
  if (sender->value.value.number < net) {
    LOG_ERROR("Insufficient funds for %s: %.2f < required %.2f", tx->from,
              sender->value.value.number, net);
    return false;
  }

  sender->value.value.number -= net;
  map_get(balances, "originator", (void *)strcmp)->value.value.number +=
      tx->amount * 1e-6;

  map_pair_t *receiver = map_get(balances, tx->to, (void *)strcmp);
  if (!receiver)
    map_append_number(balances, tx->to, tx->amount);
  else
    receiver->value.value.number += tx->amount;

  return true;
}

bool hash_txs(FILE *f, char *hash) {
  fseek(f, 0, SEEK_SET);

  struct stat st;
  fstat(fileno(f), &st);

  unsigned char *bytes = calloc(1, st.st_size);

  fread(bytes, 1, st.st_size, f);

  SHA256_CTX ctx;

  sha256_init(&ctx);
  char buf[256];

  sha256_update(&ctx, bytes, st.st_size);

  sha256_final(&ctx, (uint8_t *)hash);

  FILE *hash_file = fopen(HASH_FILE_PATH, "w");
  fwrite(hash, HASH_STR_LEN, 1, hash_file);
  fclose(hash_file);

  free(bytes);

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

bool verify_hash(book_t *book, FILE *tx_file) {
  char hash[HASH_BYTE_LEN];
  hash_txs(tx_file, hash);

  if (!strncmp(hash, book->block.parent_hash, 32)) {
    return true;
  }

  char expected_hash[HASH_STR_LEN], actual_hash[HASH_STR_LEN];
  LOG_ERROR("Hash verification failed\nExpected: %s\nActual:   %s",
            hash_to_string(hash, expected_hash, HASH_STR_LEN),
            hash_to_string(book->block.parent_hash, actual_hash, HASH_STR_LEN));
  return false;
}

bool tx_from_json(tx_t *tx, json_node_t *saved_tx, book_t *book) {
  json_iter_t *tx_iter = json_iter(saved_tx);

  char *key;
  json_value_t *value;
  tx->purpose = strdup("");
  while (!json_next(tx_iter, &key, &value)) {
    if (!strncmp(key, "from", 4)) {
      tx->from = value->value.string;
    } else if (!strncmp(key, "to", 2)) {
      tx->to = value->value.string;
    } else if (!strncmp(key, "value", 5)) {
      tx->amount = value->value.number;
    } else if (!strncmp(key, "reward", 6))
      tx->is_reward = value->value.boolean;
    else if (!strncmp(key, "for", 3))
      tx->purpose = value->value.string;
    else if (!strncmp(key, "reward", 6))
      tx->is_reward = value->value.boolean;
  }
  return effect_tx(book->balances, tx);
}

bool parse_block(book_t *book, FILE *blocks_file) {
  json_node_t *root = parse_json(blocks_file);
  if (!root) {
    LOG_ERROR("Failed to parse blocks.json");
    return false;
  }
  void *blocks;
  uint64_t no_of_blocks = 1;
  bool value_is_array = root->value.value_type == ARRAY;
  if (value_is_array) {
    blocks = root->value.value.array;
    no_of_blocks = ((array_t *)blocks)->length;
  } else
    blocks = root->value.value.node;

  for (uint64_t i = 0; i < no_of_blocks; i++) {
    json_node_t *saved_block =
        value_is_array ? array_get_ptr(blocks, i) : blocks;
    json_iter_t *block_iter = json_iter(saved_block);

    char buf[4096];
    uint64_t len = 0;

    char *key;
    json_value_t *value;
    block_t block;

    SHA256_CTX ctx;
    sha256_init(&ctx);

    while (!json_next(block_iter, &key, &value)) {
      if (!strncmp(key, "parent_hash", 11)) {
        memcpy(block.parent_hash, value->value.string, 64);
        block.parent_hash[64] = 0;
        len += snprintf(buf + len, BUFFER_LENGTH - len,
                        "{\"parent_hash\": \"%s\",", block.parent_hash);
      } else if (!strncmp(key, "block_time", 10)) {
        memcpy(block.block_time, value->value.string, 20);
        block.block_time[20] = 0;
        len += snprintf(buf + len, BUFFER_LENGTH - len,
                        "\"block_time\": \"%s\",", block.block_time);
        if (value_is_array && i == (no_of_blocks - 1)) {
          struct tm tm;
          strptime(block.block_time, "%Y-%m-%dT%H:%M:%SZ", &tm);
          book->last_block_time = timegm(&tm);
        }
      } else if (!strncmp(key, "txs", 3)) {
        array_t *txs = value->value.array;
        len += snprintf(buf + len, BUFFER_LENGTH - len, "\"txs\": [");

        for (uint64_t j = 0; j < txs->length; j++) {
          json_node_t *saved_tx = array_get_ptr(txs, j);

          tx_t tx;

          if (!tx_from_json(&tx, saved_tx, book)) {
            LOG_ERROR("Failed to process transaction from JSON");
            return false;
          }

          len += snprintf(
              buf + len, BUFFER_LENGTH - len,
              "{\"from\":\"%s\",\"to\":\"%s\",\"value\":%f,\"for\":\"%s\","
              "\"reward\":%s},",
              tx.from, tx.to, tx.amount, tx.purpose,
              tx.is_reward ? "true" : "false");

          if (!value_is_array) {
            tx_t *i_tx = malloc(sizeof(*i_tx));
            memcpy(i_tx, &tx, sizeof(*i_tx));
            array_append_ptr(book->block.txs, i_tx);
          }

          saved_tx = saved_tx->next_pair;
        }
      } else if (!strncmp(key, "hash", 4)) {
        len--;
        len += snprintf(buf + len, BUFFER_LENGTH - len, "]}");

        char hash[32];
        sha256_update(&ctx, (BYTE *)buf, len);
        sha256_final(&ctx, (uint8_t *)hash);

        snprintf(buf, sizeof(buf), "%s",
                 hash_to_string(hash, (char[HASH_STR_LEN]){}, HASH_STR_LEN));

        if (strncmp(buf, value->value.string, HASH_BYTE_LEN)) {
          printf("Error: Invalid block. Expected %s got %s\n", buf,
                 value->value.string);
          return false;
        }
        if (value_is_array) {
          memcpy(book->block.parent_hash, value->value.string, 64);
          book->block.parent_hash[64] = 0;
        }
      }
    }
  }
  return true;
}

bool load_blocks(book_t *book) {
  FILE *blocks_file = fopen(BLOCKS_FILE_PATH, "r");

  if (!blocks_file) {
    LOG_ERROR("Could not open blocks.json: %s", strerror(errno));
    return false;
  }

  if (!parse_block(book, blocks_file)) {
    fclose(blocks_file);
    return false;
  }
  fclose(blocks_file);

  struct stat st;
  FILE *incomplete_block_file = fopen(INCOMPLETE_BLOCK_FILE_PATH, "r+");

  if (errno && errno != ENOENT) {
    LOG_ERROR("Could not open incomplete_block.json: %s", strerror(errno));
    return false;
  }

  if (incomplete_block_file) {
    fstat(fileno(incomplete_block_file), &st);

    if (st.st_size > 1) {
      if (!parse_block(book, incomplete_block_file)) return false;
      fclose(incomplete_block_file);
    }
    fclose(incomplete_block_file);
  }
  return true;
}

book_t *load_book() {
  book_t *book = calloc(1, sizeof(*book));
  book->balances = new_map();
  book->block.txs = new_array();

  if (!load_genesis(book)) {
    return NULL;
  }

  if (!load_blocks(book)) {
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

  printf("Tx Hash: %s.\n", book->block.parent_hash);
  return true;
}

static char *empty_str = "";
static bool add_tx_xxx(book_t *book, char *from, char *to, double amount,
                       char *purpose, void *xxx) {
  tx_t tx_ = {from, to, purpose ? purpose : empty_str, amount,
              xxx == add_tx_xxx};
  tx_t *tx = malloc(sizeof(*tx));
  memcpy(tx, &tx_, sizeof(tx_));

  array_append_ptr(book->block.txs, tx);
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

bool hash_block(book_t *book, char *string, uint64_t *string_len, char *hash) {
  time_t now = time(NULL);
  struct tm *tm_info = gmtime(&now);

  strftime(book->block.block_time, sizeof(book->block.block_time), "%FT%T",
           tm_info);
  char buf[BUFFER_LENGTH];
  char *buffer = string ? string : buf;
  uint64_t len = 0;

  len += snprintf(buffer + len, BUFFER_LENGTH - len,
                  "{\"parent_hash\": \"%s\","
                  "\"block_time\": \"%sZ\","
                  "\"txs\": [",
                  book->block.parent_hash, book->block.block_time);
  for (uint64_t i = 0; i < book->block.txs->length; i++) {
    tx_t *tx = array_get_ptr(book->block.txs, i);

    len +=
        snprintf(buffer + len, BUFFER_LENGTH - len,
                 "{\"from\":\"%s\",\"to\":\"%s\",\"value\":%f,\"for\":\"%s\","
                 "\"reward\":%s},",
                 tx->from, tx->to, tx->amount, tx->purpose,
                 tx->is_reward ? "true" : "false");
  }

  len--;
  len += snprintf(buffer + len, BUFFER_LENGTH - len, "]}");

  SHA256_CTX ctx;
  sha256_init(&ctx);
  sha256_update(&ctx, (BYTE *)buffer, len);
  sha256_final(&ctx, (uint8_t *)hash);

  if (string_len) *string_len = len;

  return true;
}
bool save_block(book_t *book) {
  bool complete = book->block.txs->length > 8;

  FILE *blocks_file;

  char buffer[BUFFER_LENGTH];
  if (complete) {
    remove(INCOMPLETE_BLOCK_FILE_PATH);
    book->last_block_time = time(NULL);

    blocks_file = fopen(BLOCKS_FILE_PATH, "r");

    bool found;
    uint64_t last_occurence = 0, k = 0;
    while ((fgets(buffer, sizeof(buffer), blocks_file))) {
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
            if (found) found = false;
        }
        c++;
        k++;
        if ((int)*c == EOF) {
          break;
        }
      }
    }

    if (!found) {
      LOG_ERROR("Invalid JSON structure in blocks file");
      return false;
    }
    printf("%llu", last_occurence);
    fclose(blocks_file);

    rename(BLOCKS_FILE_PATH, BLOCKS_TMP_FILE_PATH);
    FILE *tx_tmp_file = fopen(BLOCKS_TMP_FILE_PATH, "r");
    blocks_file = fopen(BLOCKS_FILE_PATH, "w");
    if (!blocks_file) {
      LOG_ERROR("Could not open blocks.json for writing: %s", strerror(errno));
      return false;
    }

    int len;
    while (last_occurence > 0) {
      len = fread(buffer, 1, sizeof(buffer), tx_tmp_file);
      if (last_occurence <= len) {
        buffer[last_occurence] = ',';
        last_occurence = 0;
      } else {
        last_occurence -= len;
      }

      fwrite(buffer, 1, len, blocks_file);
    }
    fclose(tx_tmp_file);

  } else {
    blocks_file = fopen(INCOMPLETE_BLOCK_FILE_PATH, "w");
    if (!blocks_file) {
      perror("thrift could not open " INCOMPLETE_BLOCK_FILE_PATH);
      return false;
    }
  }
  char hash[32];
  uint64_t len = 0;
  if (!hash_block(book, buffer, &len, hash)) {
    return false;
  }
  char block_hash[65];
  hash_to_string((char *)hash, block_hash, HASH_STR_LEN);

  len--;
  len += snprintf(buffer + len, BUFFER_LENGTH - len, ",\"hash\":\"%s\"}%c\n",
                  block_hash, complete ? ']' : '\n');

  fwrite(buffer, len, 1, blocks_file);
  fflush(blocks_file);

  fclose(blocks_file);

  if (complete) {
    tx_t *tx;
    while (book->block.txs->length) {
      tx = array_remove_last_ptr(book->block.txs);
      memcpy(book->block.parent_hash, block_hash, HASH_STR_LEN);
      free(tx);
    }
    printf("New block hash: %s.\n", block_hash);
    fflush(stdout);
    remove(BLOCKS_TMP_FILE_PATH);
  }
  return true;
}
