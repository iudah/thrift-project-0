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

char *hash_to_string(char *hash, char *buf, int buffer_sz);
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
    LOG("Signature not found");
    memcpy(tx->signature,
           hex_decode(json_value_data(q).string,
                      strlen(json_value_data(q).string)),
           sizeof(tx->signature));
  }

  q = json_query(saved_tx, "public_key");
  if (q) {
    LOG("Public key not found");
    tx->public_key = hex_decode(json_value_data(q).string,
                                strlen(json_value_data(q).string));
  }

  return effect_tx(book->balances, book->users, tx);
}

bool effect_tx(hash_map *balances, array_t *users, tx_t *tx) {

  if (!is_valid_tx(tx)) {
    LOG("Transaction is invalid");
    return false;
  }

  if (tx->is_reward) {
    map_value *originator_pair = hash_map_get(balances, "originator");
    if (!originator_pair) {
      LOG_ERROR("Originator account missing in reward transaction");
      return false;
    }
    if (json_value_type(originator_pair) != FLOATS) {
      return false;
    }
    double credit = json_value_data(originator_pair).number;
    credit += tx->amount;
    hash_map_replace_number(balances, "originator", credit);
    return true;
  }

  map_value *sender = hash_map_get(balances, tx->from);
  if (!sender) {
    LOG_ERROR("Sender account '%s' not found", tx->from);
    return false;
  }
  if (json_value_type(sender) != FLOATS) {
    return false;
  }

  volatile double net = tx->amount + tx->amount * 1e-6;
  double s_balance = json_value_data(sender).number;
  if (s_balance < net) {
    LOG_ERROR("Insufficient funds for %s: %.2f < required %.2f", tx->from,
              s_balance, net);
    return false;
  }

  s_balance -= net;
  hash_map_replace_number(balances, tx->from, s_balance);

  map_value *originator = hash_map_get(balances, "originator");
  double fee = json_value_data(originator).number + tx->amount * 1e-6;

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

  array_append_ptr(book->block.txs, tx);
  if (!effect_tx(book->balances, book->users, tx)) {
    return false;
  }
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
  return add_tx_xxx(book, "originator", "originator", 70, "reward", add_tx_xxx);
}

char *unsigned_tx_to_str(tx_t *tx, uint64_t *len) {
  char tx_json[1024];
  *len = snprintf(tx_json, 1024,
                  ""
                  "{\n"
                  "  \"from\": \"%s\",\n"
                  "  \"to\": \"%s\",\n"
                  "  \"value\": %g,\n"
                  "  \"for\": \"%s\",\n"
                  "  \"reward\": %s\n"
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

bool mine_genesis_block(book_t *book) {
  const char *genesis_json =
      "{"
      "  \"parent_hash\": "
      "\"0000000000000000000000000000000000000000000000000000000000000000\","
      "  \"block_time\": \"2024-08-15T00:00:00Z\","
      "  \"block_index\": 0,"
      "  \"txs\": ["
      "    {"
      "      \"from\": \"SYSTEM\","
      "      \"to\": \"iudah\","
      "      \"value\": 350000.0,"
      "      \"for\": \"genesis_allocation\","
      "      \"reward\": true"
      "    },"
      "    {"
      "      \"from\": \"SYSTEM\","
      "      \"to\": \"ajmilk\","
      "      \"value\": 350000.0,"
      "      \"for\": \"genesis_allocation\","
      "      \"reward\": true"
      "    },"
      "    {"
      "      \"from\": \"SYSTEM\","
      "      \"to\": \"norwey\","
      "      \"value\": 350000.0,"
      "      \"for\": \"genesis_allocation\","
      "      \"reward\": true"
      "    },"
      "    {"
      "      \"from\": \"SYSTEM\","
      "      \"to\": \"gokayup\","
      "      \"value\": 350000.0,"
      "      \"for\": \"genesis_allocation\","
      "      \"reward\": true"
      "    },"
      "    {"
      "      \"from\": \"SYSTEM\","
      "      \"to\": \"defun\","
      "      \"value\": 350000.0,"
      "      \"for\": \"genesis_allocation\","
      "      \"reward\": true"
      "    },"
      "    {"
      "      \"from\": \"SYSTEM\","
      "      \"to\": \"uthysel\","
      "      \"value\": 350000.0,"
      "      \"for\": \"genesis_allocation\","
      "      \"reward\": true"
      "    },"
      "    {"
      "      \"from\": \"SYSTEM\","
      "      \"to\": \"ricodee\","
      "      \"value\": 350000.0,"
      "      \"for\": \"genesis_allocation\","
      "      \"reward\": true"
      "    },"
      "    {"
      "      \"from\": \"SYSTEM\","
      "      \"to\": \"wloner\","
      "      \"value\": 350000.0,"
      "      \"for\": \"genesis_allocation\","
      "      \"reward\": true"
      "    }"
      "  ]"
      "}";

  block_t blk;
  auto block_json = json_parse_string((char *)genesis_json);
  if (!block_from_json(&blk, block_json, book))
    return false;

  if (!save_block(book))
    return false;

  return true;
}

book_t *load_book() {
  auto home = get_thrift_home();

  book_t *book = zmalloc(sizeof(*book));

  auto head_txt = join_path(home, "HEAD.txt");
  if (file_exists(head_txt)) {
    if (!replay_txs(book, home))
      return NULL;
  } else {
    if (!setup_file_structure(home) || !mine_genesis_block(book)) {
      return NULL;
    }
  }

  return book;
}
