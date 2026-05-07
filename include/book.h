/**
 * @file book.h
 * @brief Ledger management system for tracking balances and transactions
 *
 * Defines core structures and operations for a blockchain-based accounting
 * system. Manages account balances, transaction processing, and block chain
 * operations.
 */

#ifndef BOOK_H
#define BOOK_H

#include <ison.h>
#include <time.h>

/**
 * @brief Blockchain block structure
 *
 * Contains cryptographic link to previous block, timestamp, and batch of
 * transactions. Forms the fundamental building block of the transaction chain.
 */
typedef struct block {
  char parent_hash[65]; /**< SHA-256 hash of previous block (null-terminated) */
  char block_time[21];  /**< ISO 8601 timestamp in format %Y-%m-%dT%H:%M:%S */
  array_t *txs;         /**< Array of transactions included in this block */
  int block_idx;
} block_t;

/**
 * @brief Main ledger structure containing financial state and transaction
 * records
 *
 * Maintains current account balances, pending transactions, and cryptographic
 * verification hash of transaction history.
 */
typedef struct book {
  hash_map *balances; /**< Map of account names to balance values */
  array_t *users;     /**< Array of account names */
  struct block
      block; /**< SHA-256 hash of transaction history (null-terminated) */
  time_t last_block_time;
} book_t;

/**
 * @brief Financial transaction structure
 *
 * Represents a value transfer between accounts, including optional metadata
 * about the transaction purpose and reward status.
 */
typedef struct {
  char *from;     /**< Sending account name */
  char *to;       /**< Receiving account name */
  char *purpose;  /**< Description of transaction purpose */
  double amount;  /**< Transfer amount in native currency */
  bool is_reward; /**< Flag indicating system-generated reward transaction */
  uint8_t signature[65];
  uint8_t public_key[33];
  uint64_t nonce;
} tx_t;

typedef struct keystore keystore;

bool show_balances(book_t *book);
bool load_genesis(book_t *book);
/**
 * @brief Initialize ledger state from persistent storage
 * @return Pointer to newly allocated book structure
 * @note Loads genesis block, transaction history, and verifies chain integrity
 */
book_t *load_book();

char *join_path(const char *parent, const char *child);
bool file_exists(const char *restrict fpath);
bool setup_file_structure(const char *home);
array_t *parse_block(book_t *book, FILE *blocks_file);
bool verify_hash(book_t *book, uint8_t *bytes, uint64_t bytes_size);
bool hash_txs(uint8_t *bytes, uint64_t byte_size, char hash[65]);
bool hash_block(block_t *blk, char hash[65]);
bool hash_buffer(char *buffer, uint64_t len, char hash[65]);
bool hash_file(FILE *f, uint8_t hash_out[65]);
const char *get_thrift_home();
bool set_thrift_home(char *path);

bool replay_txs(book_t *book, const char *restrict home_path);

bool audit_block(block_t *blk);
bool effect_block(block_t *blk, book_t *book);
bool block_from_json(block_t *blk, json_value *saved_blk);
bool tx_from_json(tx_t *tx, json_value *saved_tx);
bool effect_tx(hash_map *balances, array_t *users, tx_t *tx);

bool add_tx(book_t *book, char *from, char *to, double amount, char *purpose);
static bool add_tx_xxx(book_t *book, char *from, char *to, double amount,
                       char *purpose, void *xxx);

bool add_reward(book_t *book);
bool save_block(block_t *block);

char *hash_to_string(char *hash_bin, uint64_t bin_size, char *hash_hex);

extern char *bootstrap_path;

extern char *thrift_path;

extern keystore *user_keystore;

keystore *load_keystore(char *password);
bool sign_tx(tx_t *tx, keystore *ks);
bool is_valid_tx(tx_t *tx);
bool sign_buffer(uint8_t signature[65], char *buffer, uint64_t buffer_len,
                 keystore *ks);
bool verify_buffer(uint8_t signature[65], char *buffer, uint64_t buffer_length,
                   uint8_t *public_key);
char *get_address_from_keystore();
void get_password(char *password, int max_length);

uint8_t *hex_decode(const char *hex, uint32_t len);

char *hex_encode(uint8_t *bin, uint32_t len);

void set_paths();

uint64_t block_to_str(char *buffer, uint64_t len, block_t *blk);

#define HASH_STR_LEN 65
#define HASH_BYTE_LEN 33
#define BUFFER_LENGTH 4096

extern char *GENESIS_FILE_PATH;
extern char *BLOCKS_FILE_PATH;
extern char *BLOCKS_TMP_FILE_PATH;
extern char *HASH_FILE_PATH;
extern char *INCOMPLETE_BLOCK_FILE_PATH;
extern char *KEYSTORE_FILE_PATH;

#endif
