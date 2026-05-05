#include "book.h"
#include "zot.h"
#include <netinet/in6.h>
#include <sodium/crypto_hash_sha256.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

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

bool verify_hash(book_t *book, uint8_t *bytes, uint64_t bytes_size) {
  char hash[HASH_BYTE_LEN];
  hash_txs(bytes, bytes_size, hash);

  if (!strncmp(hash, book->block.parent_hash, 32)) {
    return true;
  }

  char expected_hash[HASH_STR_LEN], actual_hash[HASH_STR_LEN];
  LOG_ERROR("Hash verification failed\nExpected: %s\nActual:   %s",
            hash_to_string(hash, expected_hash, HASH_STR_LEN),
            hash_to_string(book->block.parent_hash, actual_hash, HASH_STR_LEN));
  return false;
}

bool hash_txs(uint8_t *bytes, uint64_t byte_size, char *hash) {
  char buf[256];

  crypto_hash_sha256((uint8_t *)hash, bytes, byte_size);

  FILE *hash_file = fopen(HASH_FILE_PATH, "w");
  fwrite(hash, HASH_STR_LEN, 1, hash_file);
  fclose(hash_file);

  zfree(bytes);

  return true;
}

bool hash_buffer(char *buffer, uint64_t len, char hash[65]) {

  uint8_t bin[33];
  crypto_hash_sha256(bin, (uint8_t *)buffer, len);
  hash_to_string(hash, (char *)bin, 65);

  return true;
}

bool hash_block(book_t *book, char hash[65]) {

  char buffer[4096];
  auto len = block_to_str(buffer, sizeof(buffer), &book->block);

  return hash_buffer(buffer, len, hash);
}

bool hash_file(FILE *f, uint8_t hash_out[HASH_STR_LEN]) {

  if (!f)
    return false;

  crypto_hash_sha256_state state;
  crypto_hash_sha256_init(&state);

  uint8_t bytes[4096];
  uint64_t size_read;

  while ((size_read = fread(bytes, 1, sizeof(bytes), f))) {

    crypto_hash_sha256_update(&state, bytes, size_read);
  }

  crypto_hash_sha256_final(&state, hash_out);

  return true;
}
