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

#include <time.h>

#include "array.h"
#include "map.h"

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
} block_t;

/**
 * @brief Main ledger structure containing financial state and transaction
 * records
 *
 * Maintains current account balances, pending transactions, and cryptographic
 * verification hash of transaction history.
 */
typedef struct book {
  map_t *balances; /**< Map of account names to balance values */
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
} tx_t;

/**
 * @brief Initialize ledger state from persistent storage
 * @return Pointer to newly allocated book structure
 * @note Loads genesis block, transaction history, and verifies chain integrity
 */
book_t *load_book();

bool show_balances(book_t *book);

bool add_tx(book_t *book, char *from, char *to, double amount, char *purpose);

bool add_reward(book_t *book);

bool save_block(book_t *book);

#endif