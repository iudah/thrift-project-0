#include "book.h"
#include "zot.h"
#include <netinet/in6.h>
#include <sodium/crypto_hash_sha256.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

char *hash_to_string(char *hash_bin, uint64_t bin_size, char *hash_hex) {
  char *hash = hash_bin;
  const static char *hex = "0123456789abcdef";
  for (uint64_t i = 0; i < bin_size; ++i) {
    hash_hex[i * 2] = hex[(hash_bin[i] >> 4)];
    hash_hex[i * 2 + 1] = hex[(hash_bin[i] & 15)];
  }
  hash_hex[bin_size * 2] = 0;

  return hash_hex;
}

bool verify_hash(book_t *book, uint8_t *bytes, uint64_t bytes_size) {
  char hash[33];
  hash_txs(bytes, bytes_size, hash);

  if (!strncmp(hash, book->block.parent_hash, 32)) {
    return true;
  }

  char expected_hash[65], actual_hash[65];
  LOG_ERROR("Hash verification failed\nExpected: %s\nActual:   %s",
            hash_to_string(hash, 32, expected_hash),
            hash_to_string(book->block.parent_hash, 32, actual_hash));
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
  hash_to_string((char *)bin, 32, hash);

  return true;
}

bool hash_block(block_t *block, char hash[65]) {

  char buffer[4096];
  auto len = block_to_str(buffer, sizeof(buffer), block);

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
