#include <blake3.h>

void blake3(uint8_t output[BLAKE3_OUT_LEN], uint8_t *buf, uint32_t n) {
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  blake3_hasher_update(&hasher, buf, n);
  blake3_hasher_finalize(&hasher, output, BLAKE3_OUT_LEN);
}