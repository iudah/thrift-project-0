#include "book.h"

#include <asm-generic/fcntl.h>
#include <bits/fcntl.h>
#include <bits/flock.h>
#include <memory.h>
#include <sodium.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/user.h>
#include <time.h>
#include <unistd.h>
#include <zot.h>

#ifdef _WIN32
#define stat    _stat
#define S_IRUSR _S_IREAD
#define S_IWUSR _S_IWRITE
#endif

#include "ds.h"
#include "genesis_header.h"
#include "ison.h"

char *unsigned_tx_to_str(tx_t *txn, uint64_t *len);

bool effect_tx(book_t *book, array_t *users, tx_t *txn) {
  (void)users;
  if (!is_valid_tx(txn)) {
    LOG("Transaction is invalid");
    return false;
  }

  auto total = txn->amount + txn->fee;
  if (!tft_debit(book, txn->from, total))
    return false;

  if (!tft_credit(book, txn->to, txn->amount))
    return false;

  book->block.fee += txn->fee;

  return true;
}

bool add_tx(node_context_t *ctx, char *reciever, int64_t amount, char *purpose,
            bool reward) {

  uint64_t str_len = 0;
  tx_t     txn     = {.from = get_address_from_keystore(ctx->user_keystore),
                      reciever,
              purpose ? purpose : "",
                      amount,
                      reward};
  char    *tx_str  = unsigned_tx_to_str(&txn, &str_len);

  bool txn_signed =
      sign_buffer(txn.signature, tx_str, str_len, ctx->user_keystore);
  zfree(tx_str);

  if (!txn_signed)
    return false;

  copy_public_key(ctx->user_keystore, &txn);

  if (!effect_tx(&ctx->book, ctx->book.users, &txn)) {
    return false;
  }

  tx_t *txn_ptr = zmalloc(sizeof(*txn_ptr));
  memcpy(txn_ptr, &txn, sizeof(txn));

  array_append_ptr(ctx->book.block.txs, txn_ptr);

  // NOTE: Shouldn't txn be sent over to a peer the CLI is being used?
  // TODO: Do the above.
  return save_block(&ctx->book, ctx->book.miner, ctx);
}

bool show_balances(book_t *book) {
  printf("Accounts                   Balances\n");
  printf("-----------------------------------\n");

  uint32_t n_user = array_length(book->users);
  for (uint32_t i = 0; i < n_user; i++) {
    char      *account      = array_get_ptr(book->users, i);
    map_value *usr_bal_json = hash_map_get(book->balances, account);
    printf("%-18s %18lld\n", account, json_value_data(usr_bal_json).integer);
  }

  printf("Tx Hash: %s.\n", book->block.parent_hash);
  return true;
}

int64_t compute_reward(int64_t block_idx) {
  if (block_idx <= 0)
    return 0;
  int64_t halvings = block_idx / HALVING_INTERVAL;
  if (halvings >= N_HALVINGS)
    return 0;
  return INITIAL_REWARD >> halvings;
}

char *unsigned_tx_to_str(tx_t *txn, uint64_t *len) {
  char tx_json[BUFFER_SMALL];
  *len = snprintf(tx_json, BUFFER_SMALL,
                  ""
                  "{"
                  "\"from\":\"%s\","
                  "\"to\":\"%s\","
                  "\"value\":%" PRId64 ","
                  "\"fee\":%" PRId64 ","
                  "\"for\":\"%s\""
                  "}",
                  txn->from, txn->to, txn->amount, txn->fee, txn->purpose);
  return zstrdup(tx_json);
}

bool sign_tx(tx_t *txn, keystore *usr_ks) {
  uint64_t len;
  char    *tx_str       = unsigned_tx_to_str(txn, &len);
  bool     return_state = sign_buffer(txn->signature, tx_str, len, usr_ks);
  zfree(tx_str);
  return return_state;
}

bool is_valid_tx(tx_t *txn) {
  uint64_t len;
  char    *tx_str   = unsigned_tx_to_str(txn, &len);
  bool     validity = pubkey_matches_address(txn->public_key, txn->from) &&
                  verify_buffer(txn->signature, tx_str, len, txn->public_key);
  zfree(tx_str);
  return validity;
}

bool set_block_time(block_t *blk) {
  if (!blk)
    return false;

  time_t     now     = time(nullptr);
  struct tm *tm_info = gmtime(&now);

  strftime(blk->block_time, sizeof(blk->block_time), "%FT%T", tm_info);

  return true;
}

bool tft_credit(book_t *book, char *user_address, int64_t amount) {
  map_value *user_rec = hash_map_get(book->balances, user_address);
  if (user_rec) {
    if (json_value_type(user_rec) != INTEGERS) {
      return false;
    }
    int64_t user_balance = json_value_data(user_rec).integer + amount;
    hash_map_replace_int(book->balances, user_address, user_balance);
  } else {
    array_append_str(book->users, user_address);
    hash_map_add_int(book->balances, user_address, amount);
  }
  return true;
}

bool tft_debit(book_t *book, char *user_address, int64_t amount) {
  map_value *user_rec = hash_map_get(book->balances, user_address);
  if (!user_rec) {
    return false;
  }
  if (json_value_type(user_rec) != INTEGERS) {
    return false;
  }

  int64_t user_balance = json_value_data(user_rec).integer;
  if (user_balance < amount)
    return false;

  user_balance -= amount;

  hash_map_replace_int(book->balances, user_address, user_balance);
  return true;
}

static void book_move(book_t *dst, book_t *src) {
  dst->balances        = src->balances;
  dst->users           = src->users;
  dst->miner           = src->miner;
  dst->total_supply    = src->total_supply;
  dst->block           = src->block;
  dst->last_block_time = src->last_block_time;
  zfree(src);
}

bool effect_block(block_t *blk, book_t *book) {
  book_t *scratch = book_copy(book);
  if (!scratch)
    return false;

  scratch->block.fee = 0;

  uint64_t ntx = array_length(blk->txs);
  for (uint64_t i = 0; i < ntx; ++i) {
    tx_t *txn = array_get_ptr(blk->txs, i);
    if (!effect_tx(scratch, scratch->users, txn))
      return false;
  }

  int64_t reward = compute_reward(blk->block_idx);
  if (scratch->total_supply + reward > MAX_SUPPLY)
    reward = MAX_SUPPLY - scratch->total_supply;
  if (reward < 0)
    reward = 0;

  int64_t miner_credit = reward + scratch->block.fee;
  if (miner_credit > 0 && blk->miner[0] != '\0') {
    if (!tft_credit(scratch, blk->miner, miner_credit))
      return false;
  }
  scratch->total_supply += reward;
  scratch->block.fee = 0;

  book_move(book, scratch);
  return true;
}

bool mine_genesis_block(book_t *book) {

  auto genesis_obj = json_parse_string((char *)genesis_json);
  if (!genesis_obj)
    return false;

  book->block.block_idx = 0;
  memset(book->block.parent_hash, '0', SHA256_HEX_LEN);

  json_value *time = json_query(genesis_obj, "genesis_time");
  if (!time || json_value_type(time) != TEXT)
    return false;
  strncpy(book->block.block_time, json_value_data(time).string,
          STRFTIME_BUF_LEN - 1);

  book->block.miner[0] = '\0';
  book->block.txs      = create_array();

  json_value *allocs = json_query(genesis_obj, "allocations");
  if (!allocs || json_value_type(allocs) != LIST)
    return false;

  array_t *backers   = json_value_data(allocs).ptr;
  auto     n_backers = array_length(backers);
  for (uint64_t i = 0; i < n_backers; ++i) {
    hash_map   *backer  = array_get_ptr(backers, i);
    json_value *address = hash_map_get(backer, "address");
    json_value *amount  = hash_map_get(backer, "amount");

    if (!address || json_value_type(address) != TEXT || !amount ||
        json_value_type(amount) != INTEGERS)
      continue;

    auto backer_address = json_value_data(address).string;
    auto backer_amount  = json_value_data(amount).integer;

    tft_credit(book, backer_address, backer_amount);
    book->total_supply += backer_amount;
  }

  hash_buffer((char *)genesis_json, strlen(genesis_json),
              book->block.parent_hash);

  book->block.block_idx = 1;

  return true;
}

static bool lock_thrift_process(int file_dsc) {
  struct flock lock = {
      .l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 0};
  return fcntl(file_dsc, F_SETLK, &lock) != -1;
}
static bool unlock_thrift_process(int file_dsc) {
  struct flock lock = {
      .l_type = F_UNLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 0};
  return fcntl(file_dsc, F_SETLK, &lock) != -1;
}

bool load_book(book_t *book, node_context_t *ctx) {
  auto home      = get_thrift_home(ctx);
  auto lock_path = join_path(get_thrift_home(ctx), "LOCK");

  book->lock_fd = open(lock_path, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
  if (book->lock_fd == -1) {
    return false;
  }

  if (!lock_thrift_process(book->lock_fd)) {
    close(book->lock_fd);
    book->lock_fd = -1;
    return false;
  }

  book->users    = create_array();
  book->balances = create_hash_map();

  if (!mine_genesis_block(book))
    return false;

  auto head_txt = join_path(home, "HEAD.txt");
  if (file_exists(head_txt)) {
    if (!replay_txs(book, home))
      return false;
  } else {
    if (!setup_file_structure(home)) {
      return false;
    }
  }

  book->block.txs       = create_array();
  book->last_block_time = time(nullptr);
  set_block_time(&book->block);

  return true;
}

book_t *book_copy(book_t *src) {
  if (!src)
    return nullptr;

  book_t *book = zmalloc(sizeof(*book));
  if (!book)
    return nullptr;

  book->miner           = src->miner;
  book->total_supply    = src->total_supply;
  book->last_block_time = src->last_block_time;

  book->users = create_array();
  auto n_user = array_length(src->users);
  for (uint32_t i = 0; i < n_user; ++i) {
    array_append_ptr(book->users, array_get_ptr(src->users, i));
  }

  book->balances = hash_map_copy(src->balances);

  book->block     = src->block;
  book->block.txs = create_array();
  auto n_txs      = array_length(src->block.txs);
  for (uint32_t i = 0; i < n_txs; ++i) {
    tx_t *txn = array_get_ptr(src->block.txs, i);
    if (!txn)
      continue;
    tx_t *copy = zmalloc(sizeof(*copy));
    if (!copy)
      continue;
    memcpy(copy, txn, sizeof(*copy));
    array_append_ptr(book->block.txs, copy);
  }

  return book;
}

bool close_book(book_t *book) {
  if (book->lock_fd <= 2)
    return true;
  bool unlocked = unlock_thrift_process(book->lock_fd);
  close(book->lock_fd);
  book->lock_fd = -1;
  return unlocked;
}
