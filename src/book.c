#include "book.h"

#include <errno.h>
#include <memory.h>
#include <sodium.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <zot.h>

#include "ds.h"
#include "ison.h"
#include "sodium/crypto_hash_sha256.h"

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

void set_paths() {
  char tmp_path[512];
  int len = snprintf(tmp_path, sizeof(tmp_path), "%s/.thrift/db/genesis.json",
                     thrift_path);
  GENESIS_FILE_PATH = zstrdup(tmp_path);
  LOG("%s", tmp_path);

  len = snprintf(tmp_path, sizeof(tmp_path), "%s/.thrift/db/blocks.json",
                 thrift_path);
  BLOCKS_FILE_PATH = zstrdup(tmp_path);
  LOG("%s", tmp_path);

  len = snprintf(tmp_path, sizeof(tmp_path), "%s/.thrift/db/hash.hash",
                 thrift_path);
  HASH_FILE_PATH = zstrdup(tmp_path);
  LOG("%s", tmp_path);

  len = snprintf(tmp_path, sizeof(tmp_path), "%s/.thrift/db/~blocks.json",
                 thrift_path);
  BLOCKS_TMP_FILE_PATH = zstrdup(tmp_path);
  LOG("%s", tmp_path);

  len = snprintf(tmp_path, sizeof(tmp_path),
                 "%s/.thrift/db/incomplete_block.json", thrift_path);
  INCOMPLETE_BLOCK_FILE_PATH = zstrdup(tmp_path);
  LOG("%s", tmp_path);

  len = snprintf(tmp_path, sizeof(tmp_path), "%s/.thrift/db/ks.json",
                 thrift_path);
  KEYSTORE_FILE_PATH = zstrdup(tmp_path);
  LOG("%s", tmp_path);

  len = snprintf(tmp_path, sizeof(tmp_path), "%s/.thrift/db/", thrift_path);
  if (access(tmp_path, F_OK) != 0) {
    make_thrift_dirs(tmp_path);
  }
  LOG("%s", GENESIS_FILE_PATH);
  LOG("%s", KEYSTORE_FILE_PATH);
}

bool load_genesis(book_t *book) {

  FILE *genesis_file = fopen(GENESIS_FILE_PATH, "r");

  if (!genesis_file) {
    LOG("Could not open genesis.json: %s", strerror(errno));
    genesis_file = fopen(GENESIS_FILE_PATH, "w");
    if (!genesis_file) {
      LOG_ERROR("Could not write '%s': %s", GENESIS_FILE_PATH, strerror(errno));
      return false;
    }

    const char *genesis_json =
        "{"
        "  \"genesis_time\": \"2024-08-15T00:00:00.000000000Z\", "
        "  \"chain_id\": \"thrift-project-0\", "
        "  \"balances\": ["
        "    { \"name\": \"originator\", \"credit\": 2500000 }"
        "  ]"
        "}";
    fwrite(genesis_json, 1, strlen(genesis_json), genesis_file);

    fclose(genesis_file);
    genesis_file = fopen(GENESIS_FILE_PATH, "r");
  }

  json_value *json = json_parse_file(genesis_file);

  fclose(genesis_file);

  if (json_value_type(json) != DICT)
    return false;

  json_value *balances = json_query(json, "balances");

  if (json_value_type(balances) != LIST)
    return false;

  array_t *balance_list = json_value_data(balances).ptr;

  json_value *bal_detail;
  char *name;
  double credit;

  uint32_t n_balances = array_length(balance_list);
  for (uint32_t i = 0; i < n_balances; i++) {
    bal_detail = array_get(balance_list, i);

    json_value *name_query = json_query(bal_detail, "name");
    if (json_value_type(name_query) != TEXT) {
      return false;
    }

    name = json_value_data(name_query).string;

    json_value *credit_query = json_query(bal_detail, "credit");
    if (json_value_type(credit_query) != FLOATS) {
      return false;
    }

    credit = json_value_data(credit_query).number;

    map_value *book_bal = hash_map_get(book->balances, name);

    if (book_bal) {
      if (json_value_type(book_bal) != FLOATS) {
        return false;
      }
      credit += json_value_data(credit_query).number;
      hash_map_replace_number(book->balances, name, credit);
    } else {
      array_append_str(book->users, name);
      hash_map_add_number(book->balances, name, credit);
    }
  }

  return true;
}

bool load_blocks(book_t *book) {
  FILE *blocks_file = fopen(BLOCKS_FILE_PATH, "r");

  if (!blocks_file) {
    LOG("Could not open blocks.json: %s", strerror(errno));
    return true;
  } else {
    LOG("Opened blocks.json");
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
      if (!parse_block(book, incomplete_block_file))
        return false;
      fclose(incomplete_block_file);
    }
    fclose(incomplete_block_file);
  }
  return true;
}

book_t *load_book() {
  if (thrift_path == NULL) {
    char *HOME = getenv("HOME");
    thrift_path = zstrdup(HOME);
  }

  char path[512];
  memcpy(path, thrift_path, strlen(thrift_path) + 1);

  struct stat st = {0};

  char *paths[] = {"/.thrift", "/db"};

  for (int i = 0; i < 2; i++) {
    strncat(path, paths[i], strlen(paths[i]) + 1);

    if (stat(path, &st) < 0) {
      if (mkdir(path, 07777) < 0) {
        LOG_ERROR("Could not make directory '%s': %s", path, strerror(errno));
        return NULL;
      }
    }
  }

  book_t *book = zcalloc(1, sizeof(*book));
  book->balances = create_hash_map();
  book->users = create_array();
  book->block.txs = create_array();

  if (!load_genesis(book)) {
    return NULL;
  }

  if (!load_blocks(book)) {
    return NULL;
  }

  return book;
}

bool parse_block(book_t *book, FILE *blocks_file) {
  json_value *json = json_parse_file(blocks_file);
  if (!json) {
    LOG_ERROR("Failed to parse blocks.json");
    return false;
  }

  bool is_single_block = json_value_type(json) == DICT;

  if (json_value_type(json) != LIST && !is_single_block) {
    return false;
  }
  array_t *blocks = json_value_data(json).ptr;
  uint64_t no_of_blocks = is_single_block ? 1 : array_length(blocks);

  for (uint64_t i = 0; i < no_of_blocks; i++) {
    json_value *block_json = is_single_block ? json : array_get(blocks, i);

    if (json_value_type(block_json) != DICT) {
      return false;
    }

    char buf[4096];

    uint32_t len = 0;
    block_t block;
    json_value *q = json_query(block_json, "parent_hash");
    if (json_value_type(q) != TEXT) {
      return false;
    }
    memcpy(block.parent_hash, json_value_data(q).string, 64);
    block.parent_hash[64] = 0;
    len += snprintf(buf + len, BUFFER_LENGTH - len, "{\"parent_hash\": \"%s\",",
                    block.parent_hash);

    q = json_query(block_json, "block_time");
    if (json_value_type(q) != TEXT) {
      return false;
    }
    memcpy(block.block_time, json_value_data(q).string, 20);
    block.block_time[20] = 0;
    len += snprintf(buf + len, BUFFER_LENGTH - len, "\"block_time\": \"%s\",",
                    block.block_time);

    // set time last block was added
    if (i == (no_of_blocks - 1)) {
      struct tm tm;
      strptime(block.block_time, "%Y-%m-%dT%H:%M:%SZ", &tm);
      book->last_block_time = timegm(&tm);
    }

    q = json_query(block_json, "block_index");
    if (json_value_type(q) != FLOATS) {
      return false;
    }
    block.block_idx = (int)json_value_data(q).number;
    len += snprintf(buf + len, BUFFER_LENGTH - len, "\"block_index\": %i,",
                    block.block_idx);

    q = json_query(block_json, "txs");
    if (json_value_type(q) != LIST) {
      return false;
    }
    array_t *txs = json_value_data(q).ptr;
    len += snprintf(buf + len, BUFFER_LENGTH - len, "\"txs\": [");
    for (uint64_t j = 0; j < array_length(txs); j++) {
      json_value *saved_tx = array_get(txs, j);

      tx_t tx;

      if (!tx_from_json(&tx, saved_tx, book)) {
        LOG_ERROR("Failed to process transaction from JSON");
        return false;
      }

      len +=
          snprintf(buf + len, BUFFER_LENGTH - len,
                   "{\"from\":\"%s\",\"to\":\"%s\",\"value\":%f,\"for\":\"%s\","
                   "\"reward\":%s},",
                   tx.from, tx.to, tx.amount, tx.purpose,
                   tx.is_reward ? "true" : "false");
    }

    q = json_query(block_json, "hash");
    if (json_value_type(q) != TEXT) {
      return false;
    }
    char *stored_hash = json_value_data(q).string;

    len--;
    len += snprintf(buf + len, BUFFER_LENGTH - len, "]}");

    if (!is_single_block) {
      char hash[32];

      crypto_hash_sha256((uint8_t *)hash, (uint8_t *)buf, len);
      snprintf(buf, sizeof(buf), "%s",
               hash_to_string(hash, (char[HASH_STR_LEN]){}, HASH_STR_LEN));

      if (strncmp(buf, stored_hash, HASH_BYTE_LEN)) {
        printf("Error: Invalid block. Expected %s got %s\n", buf, stored_hash);
        return false;
      }
      memcpy(book->block.parent_hash, stored_hash, 64);
      book->block.parent_hash[64] = 0;
    }
  }

  return true;
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

bool hash_txs(FILE *f, char *hash) {
  fseek(f, 0, SEEK_SET);

  struct stat st;
  fstat(fileno(f), &st);

  unsigned char *bytes = zcalloc(1, st.st_size);

  fread(bytes, 1, st.st_size, f);

  char buf[256];

  crypto_hash_sha256((uint8_t *)hash, bytes, st.st_size);

  FILE *hash_file = fopen(HASH_FILE_PATH, "w");
  fwrite(hash, HASH_STR_LEN, 1, hash_file);
  fclose(hash_file);

  zfree(bytes);

  return true;
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
                  "\"block_index\": %i,"
                  "\"txs\": [",
                  book->block.parent_hash, book->block.block_time,
                  book->block.block_idx);

  uint32_t n_tx = array_length(book->block.txs);

  for (uint64_t i = 0; i < n_tx; i++) {
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

  crypto_hash_sha256((uint8_t *)hash, (uint8_t *)buffer, len);
  if (string_len)
    *string_len = len;

  return true;
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

bool save_block(book_t *book) {
  uint32_t n_tx = array_length(book->block.txs);
  bool complete = n_tx > 8;

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
      LOG_ERROR("thrift could not open %s", INCOMPLETE_BLOCK_FILE_PATH);
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
    uint32_t n_tx = array_length(book->block.txs);
    while (n_tx) {
      tx = array_remove_last_ptr(book->block.txs);
      memcpy(book->block.parent_hash, block_hash, HASH_STR_LEN);
      zfree(tx);
      n_tx--;
    }
    printf("New block hash: %s.\n", block_hash);
    fflush(stdout);
    remove(BLOCKS_TMP_FILE_PATH);
  }
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
