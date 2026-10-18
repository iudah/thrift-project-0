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

#define UNIT_SCALE (10000000LL)
#define UNIT_SCALE_DIGITS (7)

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

#define MAX_SUPPLY (2514829359389LL)
#define INITIAL_REWARD (64 * UNIT_SCALE / 10)
#define HALVING_INTERVAL (100)

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
  char miner[ADDRESS_STR_MAX];
  array_t *txs; /**< Array of transactions included in this block */
  int64_t fee;
  int block_idx;
  uint64_t nonce;
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
  char *miner;
  int64_t total_supply;
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
  int64_t amount; /**< Transfer amount in native currency */
  int64_t fee;    /**< Transaction fee in native currency */
  uint8_t signature[ED25519_BIN_BUF_LEN];
  uint8_t public_key[SHA256_BIN_BUF_LEN];
} tx_t;

typedef struct keystore keystore;

typedef enum : uint8_t {
  ADD_TX = 1 << 0,
  SHOW_BALS = 1 << 2,
  START_NODE = 1 << 3,
  CREATE_WALLET = 1 << 4
} node_task_t;

typedef struct {
  char *miner;
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

typedef struct {
  char *name;
  char *ip;
  int port;
} peer_t;

typedef struct {
  char *method;
  char *path;
  char *content_type;
  char *payload;
} request_metadata_t;

typedef enum { JSON_RESP, TEXT_RESP } response_t;

typedef bool (*endpoint_handler_t)(request_metadata_t *meta,
                                   node_context_t *ctx, char *buffer,
                                   int connection_fd);

// =====================================================================
// Group 1: Book and transaction core (src/book.c)
// =====================================================================
bool add_tx(node_context_t *ctx, char *reciever, int64_t amount, char *purpose,
            bool reward);
bool show_balances(book_t *book);
bool audit_block(block_t *blk);
bool effect_block(block_t *blk, book_t *book);
bool load_book(book_t *book);
bool set_block_time(block_t *blk);
book_t *book_copy(book_t *src);
bool tft_credit(book_t *book, char *user_address, int64_t amount);
bool tft_debit(book_t *book, char *user_address, int64_t amount);

// =====================================================================
// Group 2: Wallet and keystore (src/wallet.c, src/get_password.c)
// =====================================================================
void get_password(char *password, int max_length);
keystore *init_keystore(void);
bool create_wallet(keystore *usr_ks);
bool load_keystore(keystore *usr_ks);
bool sign_buffer(uint8_t signature[ED25519_BIN_LEN], const char *buffer,
                 uint64_t buffer_length, keystore *usr_ks);
bool verify_buffer(const uint8_t signature[ED25519_BIN_LEN], const char *buffer,
                   uint64_t buffer_length,
                   const uint8_t public_key[PUBLIC_KEY_BIN_LEN]);
void copy_public_key(keystore *usr_ks, tx_t *txn);
char *get_address_from_keystore(keystore *user_keystore);
bool store_keystore_json(keystore *usr_ks, const uint8_t key[KEY_BIN_LEN]);
bool pubkey_matches_address(const uint8_t public_key[SHA256_BIN_BUF_LEN],
                            const char *address);

// =====================================================================
// Group 3: Networking, HTTP handlers, and routing
// (src/node_net.c, src/http_handlers.c, src/router.c)
// =====================================================================
bool peer_from_json(peer_t *peer, json_value *saved_peer);
bool run_node(node_context_t *ctx, char *address, int port);
void stop_listening(void);
void set_handlers_peers(array_t *peer_list);
bool handle_balances(request_metadata_t *meta, node_context_t *ctx,
                     char *buffer, int connection_fd);
bool handle_status(request_metadata_t *meta, node_context_t *ctx, char *buffer,
                   int connection_fd);
bool handle_add_tx(request_metadata_t *meta, node_context_t *ctx, char *buffer,
                   int connection_fd);
bool handle_add_node(request_metadata_t *meta, node_context_t *ctx,
                     char *buffer, int connection_fd);
bool handle_peers(request_metadata_t *meta, node_context_t *ctx, char *buffer,
                  int connection_fd);
bool handle_exit(request_metadata_t *meta, node_context_t *ctx, char *buffer,
                 int connection_fd);
bool handle_create_wallet(request_metadata_t *meta, node_context_t *ctx,
                          char *buffer, int connection_fd);
void reg_endpoints(void);
bool register_route(char *endpoint, endpoint_handler_t handler);
bool http_message(int file_descriptor, response_t type, const char *msg,
                  uint64_t msg_len, bool is_server_resp, char *method,
                  char *endpoint);
bool requested_block_hex(json_value *json, char *block_hash_hex);

// =====================================================================
// Group 4: JSON serialization for blocks and transactions (src/book_json.c)
// =====================================================================
uint64_t block_to_str(char *buffer, uint64_t len, block_t *blk);
bool tx_from_json(tx_t *txn, json_value *saved_tx);
bool block_from_json(block_t *blk, json_value *saved_blk);

// =====================================================================
// Group 5: Cryptographic helpers (src/book_crypto.c)
// =====================================================================
char *hash_bin_to_hex(const uint8_t *hash_bin, uint64_t bin_size,
                      char *hash_hex);
uint8_t *hash_hex_to_bin(const char *hex, uint64_t len, uint8_t *hash_bin);
bool hash_buffer(char *buffer, uint64_t len, char hash_out[SHA256_HEX_BUF_LEN]);
bool hash_block(block_t *block, char hash_out[SHA256_HEX_BUF_LEN]);

// =====================================================================
// Group 6: File system and persistence (src/book_io.c)
// =====================================================================
bool file_exists(const char *restrict fpath);
bool save_block(book_t *book, char *miner);
char *join_path(const char *parent, const char *child);
bool replay_txs(book_t *book, const char *restrict home_path);
bool set_bootstrap_path(char *path);
bool set_thrift_home(char *path);
const char *get_thrift_home(void);
bool setup_file_structure(const char *home);
array_t *list_blocks(const char *blocks_path, char *head_block);
bool save_peers(array_t *peers);
bool load_peers(array_t *peers);

// =====================================================================
// Group 7: Command-line argument parsing (src/cli.c)
// =====================================================================
bool parse_cli_args(int argc, char *argv[], node_config_t *config);

void blake3(uint8_t output[BLAKE3_OUT_LEN], const uint8_t *buf, uint32_t n);
bool compute_mac(const char *address, const char *ciphertext_hex,
                 const char *nonce_hex, const char *salt_hex,
                 const uint8_t key[KEY_BIN_LEN], uint8_t hash[MAC_BIN_LEN]);
bool verify_mac(const char *address, const char *ciphertext_hex,
                const char *nonce_hex, const char *salt_hex,
                const uint8_t key[KEY_BIN_LEN],
                const uint8_t hash[MAC_BIN_LEN]);

bool load_genesis(book_t *book);
void request_password(char password_ptr[BUFFER_SMALL]);
array_t *parse_block(book_t *book, FILE *blocks_file);
bool verify_hash(book_t *book, uint8_t *bytes, uint64_t bytes_size);
bool hash_file(FILE *file, uint8_t hash_out[SHA256_HEX_BUF_LEN]);
bool effect_tx(book_t *book, array_t *users, tx_t *txn);

bool add_reward(book_t *book);
bool sign_tx(tx_t *txn, keystore *usr_ks);
bool is_valid_tx(tx_t *txn);

#endif
