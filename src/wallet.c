#include "libbase58.h"
#include <SimpleFIPS202.h>
#include <blake3.h>
#include <book.h>
#include <inttypes.h>
#include <sodium.h>
#include <string.h>
#include <zot.h>

#define KEY_LEN 65
// #define KEY_LEN crypto_box_SEEDBYTES
#define SALT_LEN crypto_pwhash_SALTBYTES
#define NONCE_LEN crypto_aead_xchacha20poly1305_ietf_NPUBBYTES
#define CIPHERTEXT_LEN                                                         \
  (crypto_sign_SECRETKEYBYTES + crypto_aead_xchacha20poly1305_ietf_ABYTES)
#define PUBLIC_KEY_LEN crypto_sign_PUBLICKEYBYTES
#define PRIVATE_KEY_LEN crypto_sign_SECRETKEYBYTES

struct keystore {
  uint8_t *private_key;
  uint8_t *public_key;
  char *address;
  char *cipher;
  uint8_t *ciphertext;
  uint8_t *nonce;
  uint8_t *salt;
  struct {
    char *type;
    struct {
      uint64_t mem;
      uint32_t ops;
    } params;
  } kdf;
  uint32_t ciphertext_len;
  uint32_t nonce_len;
  uint32_t salt_len;
};

keystore *user_keystore;

void request_password(char **password_ptr);
void blake3(uint8_t output[BLAKE3_OUT_LEN], uint8_t *buf, uint32_t n);
bool store_keystore_json(keystore *ks, uint8_t *key);
bool compute_mac(char *address, char *ciphertext_hex, char *nonce_hex,
                 char *salt_hex, uint8_t *key,
                 uint8_t hash[crypto_onetimeauth_BYTES]);
bool verify_mac(char *address, char *ciphertext_hex, char *nonce_hex,
                char *salt_hex, uint8_t *key, uint8_t *hash);

bool generate_key_pair(uint8_t *public_key, uint8_t *private_key) {
  return crypto_sign_keypair(public_key, private_key) == 0;
}

bool derive_encryption_key(char *password, uint8_t *salt, uint32_t opslimit,
                           uint64_t memlimit, uint8_t *key_out) {

  return crypto_pwhash(key_out, KEY_LEN, password, strlen(password), salt,
                       opslimit, memlimit, crypto_pwhash_ALG_ARGON2ID13) == 0;
}

keystore *assemble_keystore(char *address, uint8_t *private_key,
                            uint8_t *public_key, uint8_t *ciphertext,
                            uint64_t ciphertext_len, uint8_t *nonce,
                            uint64_t nonce_len, uint8_t *salt,
                            uint64_t salt_len, uint64_t mem_limit,
                            uint64_t op_limit) {
  keystore ks = {.private_key = private_key,
                 .public_key = public_key,
                 .address = zstrdup(address),
                 .cipher = "xchacha20-poly1305",
                 .ciphertext = zmalloc(ciphertext_len),
                 .ciphertext_len = ciphertext_len,
                 .nonce = zmalloc(nonce_len),
                 .nonce_len = nonce_len,
                 .salt = zmalloc(salt_len),
                 .salt_len = salt_len,
                 .kdf = {.type = "argon2id",
                         .params = {.mem = mem_limit, .ops = op_limit}}};

  memcpy(ks.ciphertext, ciphertext, ciphertext_len);
  memcpy(ks.nonce, nonce, nonce_len);
  memcpy(ks.salt, salt, salt_len);

  keystore *ks_ptr = zmalloc(sizeof(*ks_ptr));
  memcpy(ks_ptr, &ks, sizeof(ks));
  return ks_ptr;
}

bool compute_address(char *address, uint32_t *address_len,
                     uint8_t *public_key) {
  uint8_t full[25];
  uint8_t hash[BLAKE3_OUT_LEN];
  uint8_t version[21];
#define NORMAL_ADDR 0x02
  version[0] = NORMAL_ADDR;

  blake3(hash, public_key, 32);
  memcpy(version + 1, hash, 20);
  if (SHA3_256(hash, version, 21)) {
    LOG("Could not compute hash (SHA3-256)");
    return false;
  }

  memcpy(full, version, 21);

  memcpy(full + 21, hash, 4);
  b58enc(address, address_len, full, 25);

  return true;
}

keystore *create_wallet(char *password) {
  request_password(&password);

  uint8_t private_key[PRIVATE_KEY_LEN];
  uint8_t public_key[PUBLIC_KEY_LEN];
  if (!generate_key_pair(public_key, private_key)) {
    LOG("Failed to generate key pair.");
    return NULL;
  }

  uint64_t salt_len = SALT_LEN;
  uint8_t salt[salt_len];
  randombytes_buf(salt, sizeof(salt));

  uint32_t op_limit = crypto_pwhash_OPSLIMIT_MODERATE;
  uint64_t mem_limit = crypto_pwhash_MEMLIMIT_MODERATE;

  uint8_t key[KEY_LEN];

  if (!derive_encryption_key(password, salt, op_limit, mem_limit, key)) {
    LOG("Failed to derive encryption key.");
    return NULL;
  };

  uint64_t ciphertext_len = CIPHERTEXT_LEN;
  uint8_t ciphertext[ciphertext_len];
  uint64_t nonce_len = NONCE_LEN;
  uint8_t nonce[nonce_len];
  randombytes_buf(nonce, sizeof nonce);
#define ADDITIONAL_DATA NULL
#define ADDITIONAL_DATA_LEN 0
  crypto_aead_xchacha20poly1305_ietf_encrypt(
      ciphertext, &ciphertext_len, private_key, PRIVATE_KEY_LEN,
      ADDITIONAL_DATA, ADDITIONAL_DATA_LEN, NULL, nonce, key);

  uint32_t address_len = 40;
  char address[address_len];

  if (!compute_address(address, &address_len, public_key)) {
    LOG("Failed to compute address.");
    return NULL;
  }

  keystore *ks = assemble_keystore(address, private_key, public_key, ciphertext,
                                   ciphertext_len, nonce, nonce_len, salt,
                                   salt_len, mem_limit, op_limit);
  store_keystore_json(ks, key);

  return ks;
}

char *keystore_path = NULL;

char *hex_encode(uint8_t *bin, uint32_t len) {
  char hex[2 * len + 1];
  for (size_t i = 0; i < len; i++) {
    snprintf(&hex[i * 2], 3, "%02x", bin[i]);
  }
  return zstrdup(hex);
}

bool from_hex_char(char hex, uint8_t *dec) {
  if ('0' <= hex && hex <= '9') {
    *dec = hex - '0';
    return true;
  }
  if ('a' <= hex && hex <= 'f') {
    *dec = hex - 'a' + 10;
    return true;
  }
  if ('A' <= hex && hex <= 'F') {
    *dec = hex - 'A' + 10;
    return true;
  }
  LOG("Invalid hex.");

  return false;
}

uint8_t *hex_decode(const char *hex, uint32_t len) {
  if (len % 2 != 0) {
    LOG("Hex length must be even.");
    return NULL;
  }

  uint8_t *dec = zcalloc(len / 2, sizeof(*dec));
  if (!dec) {
    LOG("Failed to allocate memory for decoding.");
    return NULL;
  }

  for (uint32_t i = 0; i < len; i += 2) {
    uint8_t high;
    uint8_t low;

    if (!from_hex_char(hex[i], &high) || !from_hex_char(hex[i + 1], &low)) {
      LOG("Invalid hex: %c%c", hex[i], hex[i + 1]);
      return NULL;
    }
    dec[i / 2] = (high << 4) | low;
  }

  return dec;
}

bool store_keystore_json(keystore *ks, uint8_t *key) {
  if (keystore_path == NULL) {
    char path[512];
    memcpy(path, thrift_path, strlen(thrift_path) + 1);
    strncat(path, "/.thrift/db/ks.json", 28);
    keystore_path = zstrdup(path);
  }

  FILE *ks_file = fopen(keystore_path, "w+");
  if (!ks_file) {
    LOG("Could not create file: %s.", keystore_path);
    return false;
  }

  char *ciphertext_hex = hex_encode(ks->ciphertext, ks->ciphertext_len);
  char *nonce_hex = hex_encode(ks->nonce, ks->nonce_len);
  char *salt_hex = hex_encode(ks->salt, ks->salt_len);

  uint32_t mac_len = crypto_onetimeauth_BYTES;
  uint8_t mac[mac_len];
  compute_mac(ks->address, ciphertext_hex, nonce_hex, salt_hex, key, mac);

  char keystore[1024];
  uint32_t len = snprintf(keystore, sizeof(keystore),
                          ""
                          "{\n"
                          "  \"address\": \"%s\",\n"
                          "  \"cipher\": \"%s\",\n"
                          "  \"ciphertext\": \"%s\",\n"
                          "  \"nonce\": \"%s\",\n"
                          "  \"salt\": \"%s\",\n"
                          "  \"kdf\": {\n"
                          "    \"type\": \"%s\",\n"
                          "    \"params\": { \"mem\": %" PRIu64
                          ", \"ops\": %" PRIu32 " }\n"
                          "  },\n"
                          "  \"mac\": \"%s\"\n"
                          "}\n",

                          ks->address, ks->cipher, ciphertext_hex, nonce_hex,
                          salt_hex, ks->kdf.type, ks->kdf.params.mem,
                          ks->kdf.params.ops, hex_encode(mac, mac_len));

  fwrite(keystore, 1, len, ks_file);
  fclose(ks_file);
  return true;
}

uint32_t concat_mac_text_source(char *address, char *ciphertext_hex,
                                char *nonce_hex, char *salt_hex,
                                char text[1024]) {

  return snprintf(text, 1024, "%s|%s|%s|%s", address, ciphertext_hex, nonce_hex,
                  salt_hex);
}

bool compute_mac(char *address, char *ciphertext_hex, char *nonce_hex,
                 char *salt_hex, uint8_t *key,
                 uint8_t hash[crypto_onetimeauth_BYTES]) {

  char text[1024];
  return crypto_onetimeauth(hash, (uint8_t *)text,
                            concat_mac_text_source(address, ciphertext_hex,
                                                   nonce_hex, salt_hex, text),
                            key) == 0;
}

bool verify_mac(char *address, char *ciphertext_hex, char *nonce_hex,
                char *salt_hex, uint8_t *key, uint8_t *hash) {

  char text[1024];
  return crypto_onetimeauth_verify(
             hash, (uint8_t *)text,
             concat_mac_text_source(address, ciphertext_hex, nonce_hex,
                                    salt_hex, text),
             key) == 0;
}

keystore *load_keystore(char *password) {
  request_password(&password);

  if (keystore_path == NULL) {
    char path[512];
    memcpy(path, thrift_path, strlen(thrift_path) + 1);
    strncat(path, "/.thrift/db/ks.json", 28);
    keystore_path = zstrdup(path);
  }

  FILE *ks_file = fopen(keystore_path, "r");
  if (!ks_file) {
    LOG("Could not open file: %s.", keystore_path);
    return create_wallet(password);
  }

  json_value *ks_json = json_parse_file(ks_file);
  fclose(ks_file);
  if (json_value_type(ks_json) != DICT) {
    LOG("Malformed JSON, expected an object.");
    return NULL;
  }

  json_value *q = json_query(ks_json, "salt");
  char *salt_hex = json_value_data(q).string;

  uint64_t salt_len = SALT_LEN;
  uint8_t *salt = hex_decode(salt_hex, strlen(salt_hex));

  auto kdf = json_query(ks_json, "kdf");
  auto params = json_query(kdf, "params");
  q = json_query(params, "mem");
  uint32_t memlimit = (uint32_t)json_value_data(q).number;

  q = json_query(params, "ops");
  uint64_t op_limit = (uint64_t)json_value_data(q).number;

  uint8_t key[KEY_LEN];

  if (!derive_encryption_key(password, salt, op_limit, memlimit, key)) {
    LOG("Failed to derive encryption key.");
    return NULL;
  };

  q = json_query(ks_json, "ciphertext");
  char *ciphertext_hex = json_value_data(q).string;

  uint64_t ciphertext_len = CIPHERTEXT_LEN;
  uint8_t *ciphertext = hex_decode(ciphertext_hex, strlen(ciphertext_hex));

  q = json_query(ks_json, "nonce");
  char *nonce_hex = json_value_data(q).string;

  uint64_t nonce_len = NONCE_LEN;
  uint8_t *nonce = hex_decode(nonce_hex, strlen(nonce_hex));

  uint64_t private_key_len = PRIVATE_KEY_LEN;
  uint8_t private_key[private_key_len];

#define ADDITIONAL_DATA NULL
#define ADDITIONAL_DATA_LEN 0
  if (crypto_aead_xchacha20poly1305_ietf_decrypt(
          private_key, &private_key_len, NULL, ciphertext, ciphertext_len,
          ADDITIONAL_DATA, ADDITIONAL_DATA_LEN, nonce, key)) {
    LOG("Invalid password.");
    return NULL;
  }

  uint8_t public_key[PUBLIC_KEY_LEN];
  crypto_sign_ed25519_sk_to_pk(public_key, private_key);

  q = json_query(ks_json, "address");
  char *address = json_value_data(q).string;

  q = json_query(ks_json, "mac");
  char *mac_hex = json_value_data(q).string;

  if (!verify_mac(address, ciphertext_hex, nonce_hex, salt_hex, key,
                  hex_decode(mac_hex, strlen(mac_hex)))) {
    LOG("Keystore has been tampered with.");
    return NULL;
  }

  return assemble_keystore(address, private_key, public_key, ciphertext,
                           ciphertext_len, nonce, nonce_len, salt, salt_len,
                           memlimit, op_limit);
}

void request_password(char **password_ptr) {

  if (!*password_ptr) {
    char *password_buff = zmalloc(256);
    *password_ptr = password_buff;
    get_password(password_buff, 256);
  }
}

bool sign_buffer(uint8_t signature[129], char *buffer, uint64_t buffer_length,
                 keystore *ks) {
  uint64_t signature_len = 129;
  return crypto_sign_ed25519_detached(signature, &signature_len,
                                      (uint8_t *)buffer, buffer_length,
                                      ks->private_key) == 0;
}

bool verify_buffer(uint8_t signature[129], char *buffer, uint64_t buffer_length,
                   uint8_t *public_key) {
  return crypto_sign_ed25519_verify_detached(signature, (uint8_t *)buffer,
                                             buffer_length, public_key) == 0;
}

char *get_address_from_keystore() { return user_keystore->address; }
