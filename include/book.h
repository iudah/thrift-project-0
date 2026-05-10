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
#include <sodium.h>
#include <stdint.h>
#include <time.h>

#define KEY_BIN_LEN crypto_box_SEEDBYTES
#define MAC_BIN_LEN crypto_onetimeauth_BYTES
#define SALT_BIN_LEN crypto_pwhash_SALTBYTES
#define NONCE_BIN_LEN crypto_aead_xchacha20poly1305_ietf_NPUBBYTES
#define CIPHERTEXT_BIN_LEN                                                     \
  (crypto_sign_SECRETKEYBYTES + crypto_aead_xchacha20poly1305_ietf_ABYTES)
#define PUBLIC_KEY_BIN_LEN crypto_sign_PUBLICKEYBYTES
#define PRIVATE_KEY_BIN_LEN crypto_sign_SECRETKEYBYTES

#define KEY_HEX_LEN (KEY_BIN_LEN * 2)
#define MAC_HEX_LEN (MAC_BIN_LEN * 2)
#define SALT_HEX_LEN (SALT_BIN_LEN * 2)
#define NONCE_HEX_LEN (NONCE_BIN_LEN * 2)
#define CIPHERTEXT_HEX_LEN (CIPHERTEXT_BIN_LEN * 2)
#define PUBLIC_KEY_HEX_LEN (PUBLIC_KEY_BIN_LEN * 2)
#define PRIVATE_KEY_HEX_LEN (PRIVATE_KEY_BIN_LEN * 2)

#define VERSION_BIN_LEN 1
#define HASH160_BIN_LEN 20
#define CHECKSUM_BIN_LEN 4
#define ADDRESS_BIN_LEN                                                        \
  (VERSION_BIN_LEN + HASH160_BIN_LEN + CHECKSUM_BIN_LEN) // 25
#define ADDRESS_STR_MAX 35 // Base58 max output for 25 bytes

#define SHA3_256_BIN_LEN 32
#define BLAKE3_OUT_LEN 32

#define REWARD (64)
#define SYSTEM "SYSTEM"

#define VERSION_LEN (21)

#define NORMAL_ADDR (0x02)

#define ED25519_BIN_LEN (64)
#define ED25519_BIN_BUF_LEN (65)
#define ED25519_HEX_LEN (128)
#define ED25519_HEX_BUF_LEN (129)
#define SHA256_HEX_LEN (64)
#define SHA256_HEX_BUF_LEN (65)
#define SHA256_BIN_LEN (32)
#define SHA256_BIN_BUF_LEN (33)
#define BUFFER_SMALL (1024)
#define BUFFER_BIG (4096)
#define STRFTIME_BUF_LEN (21)

/**
 * @brief Blockchain block structure
 *
 * Contains cryptographic link to previous block, timestamp, and batch of
 * transactions. Forms the fundamental building block of the transaction chain.
 */
typedef struct block {
  char parent_hash[SHA256_HEX_BUF_LEN]; /**< SHA-256 hash of previous block
                                           (null-terminated) */
  char block_time[STRFTIME_BUF_LEN];    /**< ISO 8601 timestamp in format
                                           %Y-%m-%dT%H:%M:%S */
  array_t *txs; /**< Array of transactions included in this block */
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
  uint8_t signature[ED25519_BIN_BUF_LEN];
  uint8_t public_key[SHA256_BIN_BUF_LEN];
  uint64_t nonce;
} tx_t;

typedef struct keystore keystore;

typedef enum : uint8_t {
  ADD_TX = 1 << 0,
  SHOW_BALS = 1 << 2,
  START_NODE = 1 << 3,
  CREATE_WALLET = 1 << 4
} node_task_t;

typedef struct {
  char *thrift_path;
  char *bootstrap_path;
  char *address;
  uint32_t port;
  node_task_t tasks;
  tx_t tx;
} node_config_t;

typedef struct {
  keystore *user_keystore;
  array_t *peers;
  node_config_t config;
  book_t book;
} node_context_t;

void blake3(uint8_t output[BLAKE3_OUT_LEN], const uint8_t *buf, uint32_t n);
bool store_keystore_json(keystore *usr_ks, const uint8_t key[KEY_BIN_LEN]);
bool compute_mac(const char *address, const char *ciphertext_hex,
                 const char *nonce_hex, const char *salt_hex,
                 const uint8_t key[KEY_BIN_LEN], uint8_t hash[MAC_BIN_LEN]);
bool verify_mac(const char *address, const char *ciphertext_hex,
                const char *nonce_hex, const char *salt_hex,
                const uint8_t key[KEY_BIN_LEN],
                const uint8_t hash[MAC_BIN_LEN]);

bool parse_cli_args(int argc, char *argv[], node_config_t *config);

bool sign_buffer(uint8_t signature[ED25519_BIN_LEN], const char *buffer,
                 uint64_t buffer_length, keystore *usr_ks);
bool verify_buffer(const uint8_t signature[ED25519_BIN_LEN], const char *buffer,
                   uint64_t buffer_length,
                   const uint8_t public_key[PUBLIC_KEY_BIN_LEN]);
bool show_balances(book_t *book);
bool load_genesis(book_t *book);
bool load_book(book_t *book);
void copy_public_key(keystore *usr_ks, tx_t *txn);

void request_password(char password_ptr[BUFFER_SMALL]);
char *join_path(const char *parent, const char *child);
bool file_exists(const char *restrict fpath);
bool setup_file_structure(const char *home);
array_t *parse_block(book_t *book, FILE *blocks_file);
bool verify_hash(book_t *book, uint8_t *bytes, uint64_t bytes_size);
bool hash_block(block_t *block, char hash_out[SHA256_HEX_BUF_LEN]);
bool hash_buffer(char *buffer, uint64_t len, char hash_out[SHA256_HEX_BUF_LEN]);
bool hash_file(FILE *file, uint8_t hash_out[SHA256_HEX_BUF_LEN]);
const char *get_thrift_home();
bool set_thrift_home(char *path);
bool set_bootstrap_path(char *path);

bool replay_txs(book_t *book, const char *restrict home_path);

bool audit_block(block_t *blk);
bool effect_block(block_t *blk, book_t *book);
bool block_from_json(block_t *blk, json_value *saved_blk);
bool tx_from_json(tx_t *txn, json_value *saved_tx);
bool effect_tx(hash_map *balances, array_t *users, tx_t *txn);

bool add_tx(node_context_t *ctx, char *reciever, double amount, char *purpose,
            bool reward);

bool add_reward(book_t *book);
bool save_block(block_t *block);

char *hash_bin_to_hex(const uint8_t *hash_bin, uint64_t bin_size,
                      char *hash_hex);
uint8_t *hash_hex_to_bin(const char *hex, uint64_t len, uint8_t *hash_bin);

bool load_keystore(keystore *usr_ks);
keystore *init_keystore();
bool sign_tx(tx_t *txn, keystore *usr_ks);
bool is_valid_tx(tx_t *txn);
char *get_address_from_keystore(keystore *user_keystore);
void get_password(char *password, int max_length);

uint64_t block_to_str(char *buffer, uint64_t len, block_t *blk);

#endif
