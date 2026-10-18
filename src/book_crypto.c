#include "book.h"
#include "zot.h"
#include <netinet/in6.h>
#include <sodium/crypto_hash_sha256.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

char *hash_bin_to_hex(const uint8_t *hash_bin, uint64_t bin_size,
                      char *hash_hex) {
  const uint8_t *hash = hash_bin;
  const static char *hex = "0123456789abcdef";
  for (uint64_t i = 0; i < bin_size; ++i) {
#define MASK (0xF)
    hash_hex[i * 2] = hex[(hash_bin[i] >> 4)];
    hash_hex[(i * 2) + 1] = hex[(hash_bin[i] & MASK)];
  }

  hash_hex[bin_size * 2] = 0;

  return hash_hex;
}

uint8_t hex_to_bin(char hex) {
#define A (10)
  if (hex >= '0' && hex <= '9')
    return (uint8_t)(hex - '0');
  if (hex >= 'a' && hex <= 'f')
    return (uint8_t)(A + hex - 'a');
  if (hex >= 'A' && hex <= 'F')
    return (uint8_t)(A + hex - 'A');
  return -1;
}

uint8_t *hash_hex_to_bin(const char *hex, uint64_t len, uint8_t *hash_bin) {
  if (len % 2 != 0) {
    LOG_ERROR("Hex length must be even.");
    return nullptr;
  }

  for (uint32_t i = 0; i < len; i += 2) {
    uint8_t high = hex_to_bin(hex[i]);
    uint8_t low = hex_to_bin(hex[i + 1]);

    if (low == (uint8_t)-1 || high == (uint8_t)-1)
      return nullptr;

    hash_bin[i / 2] = (high << 4) | low;
  }

  return hash_bin;
}

bool hash_buffer(char *buffer, uint64_t len,
                 char hash_out[SHA256_HEX_BUF_LEN]) {

  uint8_t bin[SHA256_BIN_BUF_LEN];

  crypto_hash_sha256(bin, (uint8_t *)buffer, len);
  hash_bin_to_hex(bin, SHA256_BIN_LEN, hash_out);

  return true;
}

bool hash_block(block_t *block, char hash_out[SHA256_HEX_BUF_LEN]) {

  char buffer[BUFFER_BIG];
  auto len = block_to_str(buffer, sizeof(buffer), block);

  return hash_buffer(buffer, len, hash_out);
}

bool hash_file(FILE *file, uint8_t hash_out[SHA256_HEX_BUF_LEN]) {

  if (!file)
    return false;

  crypto_hash_sha256_state state;
  crypto_hash_sha256_init(&state);

  uint8_t bytes[BUFFER_BIG];
  uint64_t size_read;

  while ((size_read = fread(bytes, 1, sizeof(bytes), file))) {

    crypto_hash_sha256_update(&state, bytes, size_read);
  }

  crypto_hash_sha256_final(&state, hash_out);

  return true;
}
