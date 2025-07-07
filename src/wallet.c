#include "libbase58.h"
#include <SimpleFIPS202.h>
#include <blake3.h>
#include <book.h>
#include <inttypes.h>
#include <sodium.h>
#include <string.h>
#include <zot.h>

typedef struct keystore {
  char *address;
  char *cipher;
  uint8_t *ciphertext;
  uint8_t *nonce;
  uint8_t *salt;
  struct {
    char *type;
    struct {
      uint32_t mem;
      uint8_t parallel;
    } params;
  } kdf;
  uint32_t ciphertext_len;
  uint32_t nonce_len;
  uint32_t salt_len;
} keystore;

void blake3(uint8_t output[BLAKE3_OUT_LEN], uint8_t *buf, uint32_t n);
bool store_keystore_json(keystore *ks);

bool generate_key_pair(uint8_t *public_key, uint8_t *private_key) {
  return crypto_sign_keypair(public_key, private_key) != 0;
}

bool create_wallet(char *password) {
  uint8_t private_key[crypto_sign_SECRETKEYBYTES];
  uint8_t public_key[crypto_sign_PUBLICKEYBYTES];
  if (!generate_key_pair(public_key, private_key)) {
    return false;
  }

  uint64_t salt_len = crypto_pwhash_SALTBYTES;
  uint8_t salt[salt_len];
  randombytes_buf(salt, sizeof(salt));

#define KEY_LEN 65
  // #define KEY_LEN crypto_box_SEEDBYTES
  uint8_t key[KEY_LEN];
  if (crypto_pwhash(key, sizeof(key), password, strlen(password), salt,
                    crypto_pwhash_OPSLIMIT_MODERATE,
                    crypto_pwhash_MEMLIMIT_MODERATE,
                    crypto_pwhash_ALG_ARGON2ID13) != 0) {
    return false;
  };

  uint64_t ciphertext_len =
      crypto_sign_SECRETKEYBYTES + crypto_aead_xchacha20poly1305_ietf_ABYTES;
  uint8_t ciphertext[ciphertext_len];
  uint64_t nonce_len = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
  uint8_t nonce[nonce_len];
  randombytes_buf(nonce, sizeof nonce);
#define ADDITIONAL_DATA NULL
#define ADDITIONAL_DATA_LEN 0
  crypto_aead_xchacha20poly1305_ietf_encrypt(
      ciphertext, &ciphertext_len, private_key, 32, ADDITIONAL_DATA,
      ADDITIONAL_DATA_LEN, NULL, nonce, key);

  uint8_t full[25];
  size_t address_len = 40;
  char address[address_len];
  uint8_t hash[BLAKE3_OUT_LEN];
  uint8_t version[21];
#define NORMAL_ADDR 0x02
  version[0] = NORMAL_ADDR;

  blake3(hash, public_key, 32);
  memcpy(version + 1, hash, 20);
  if (SHA3_256(hash, version, 21)) {
    return false;
  }

  memcpy(full, version, 21);

  memcpy(full + 21, hash, 4);
  b58enc(address, &address_len, full, 25);

  uint8_t *tmp_ciphertext = zmalloc(ciphertext_len);
  uint8_t *tmp_nonce = zmalloc(nonce_len);
  uint8_t *tmp_salt = zmalloc(salt_len);

  keystore ks = {
      .address = zstrdup(address),
      .cipher = "xchacha20-poly1305",
      .ciphertext = tmp_ciphertext,
      .ciphertext_len = ciphertext_len,
      .nonce = tmp_nonce,
      .nonce_len = nonce_len,
      .salt = tmp_salt,
      .salt_len = salt_len,
      .kdf = {.type = "argon2id", .params = {.mem = 65536, .parallel = 1}}};

  store_keystore_json(&ks);

  keystore *ks_ptr = zmalloc(sizeof(*ks_ptr));
  memcpy(ks_ptr, &ks, sizeof(ks));

  return ks_ptr;
}

char *keystore_path = NULL;

char *hex_enc(uint8_t *bin, uint32_t len) {
  char hex[2 * len + 1];
  for (size_t i = 0; i < len; i++) {
    snprintf(&hex[i * 2], 3, "%02x", bin[i]);
  }
  return zstrdup(hex);
}

bool store_keystore_json(keystore *ks) {
  if (keystore_path == NULL) {
    char path[512];
    memcpy(path, thrift_path, strlen(thrift_path) + 1);
    strncat(path, "/.thrift/db/ks.json", 28);
    keystore_path = zstrdup(path);
  }

  FILE *ks_file = fopen(keystore_path, "w");
  if (!ks_file) {
    LOG("Could not create file: %s.", keystore_path);
    return false;
  }

  char keystore[1024];
  uint32_t len = snprintf(
      keystore, sizeof(keystore),
      ""
      "{\n"
      "  \"address\": \"%s\",\n"
      "  \"cipher\": \"%s\",\n"
      "  \"ciphertext\": \"%s\",\n"
      "  \"nonce\": \"%s\",\n"
      "  \"salt\": \"%s\",\n"
      "  \"kdf\": {\n"
      "    \"type\": \"%s\",\n"
      "    \"params\": { \"mem\": %" PRIu32 ", \"parallel\": %" PRIu32 " }\n"
      "  }\n"
      "}\n",

      ks->address, ks->cipher, hex_enc(ks->ciphertext, ks->ciphertext_len),
      hex_enc(ks->nonce, ks->nonce_len), hex_enc(ks->salt, ks->salt_len),
      ks->kdf.type, ks->kdf.params.mem, ks->kdf.params.parallel);

  fwrite(keystore, 1, len, ks_file);
  return true;
}
