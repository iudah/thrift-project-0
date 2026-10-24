#include "libbase58.h"
#include <SimpleFIPS202.h>
#include <blake3.h>
#include <book.h>
#include <inttypes.h>
#include <sodium.h>
#include <sodium/crypto_box.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <wallet.h>
#include <zot.h>

struct keystore {
  uint8_t     private_key[PRIVATE_KEY_BIN_LEN];
  uint8_t     public_key[PUBLIC_KEY_BIN_LEN];
  char        address[ADDRESS_STR_MAX]; // base58 encoded, null terminated
  const char *cipher;                   // literal string
  uint8_t     ciphertext[CIPHERTEXT_BIN_LEN];
  uint8_t     nonce[NONCE_BIN_LEN];
  uint8_t     salt[SALT_BIN_LEN];
  struct {
    const char *type; // literal string
    struct {
      uint64_t mem;
      uint32_t ops;
    } params;
  } kdf;
  uint32_t ciphertext_len; // actual length used (may be less than array)
  uint32_t nonce_len;
  uint32_t salt_len;
};

keystore *init_keystore(void) {
  return (keystore *)zcalloc(1, sizeof(keystore));
}

bool generate_key_pair(uint8_t public_key[PUBLIC_KEY_BIN_LEN],
                       uint8_t private_key[PRIVATE_KEY_BIN_LEN]) {
  return crypto_sign_keypair(public_key, private_key) == 0;
}

bool derive_encryption_key(const char   *password,
                           const uint8_t salt[SALT_BIN_LEN], uint32_t opslimit,
                           uint64_t memlimit, uint8_t key_out[KEY_BIN_LEN]) {
  return crypto_pwhash(key_out, KEY_BIN_LEN, password, strlen(password), salt,
                       opslimit, memlimit, crypto_pwhash_ALG_ARGON2ID13) == 0;
}

bool assemble_keystore(keystore *usr_ks, const char *address,
                       const uint8_t private_key[PRIVATE_KEY_BIN_LEN],
                       const uint8_t public_key[PUBLIC_KEY_BIN_LEN],
                       const uint8_t ciphertext[CIPHERTEXT_BIN_LEN],
                       uint64_t      ciphertext_len,
                       const uint8_t nonce[NONCE_BIN_LEN], uint64_t nonce_len,
                       const uint8_t salt[SALT_BIN_LEN], uint64_t salt_len,
                       uint64_t mem_limit, uint64_t op_limit) {
  if (!usr_ks) {
    return false;
  }

  memcpy(usr_ks->private_key, private_key, PRIVATE_KEY_BIN_LEN);
  memcpy(usr_ks->public_key, public_key, PUBLIC_KEY_BIN_LEN);
  strncpy(usr_ks->address, address, ADDRESS_STR_MAX - 1);
  usr_ks->address[ADDRESS_STR_MAX - 1] = '\0';
  usr_ks->cipher                       = "xchacha20-poly1305";

  memcpy(usr_ks->ciphertext, ciphertext, ciphertext_len);
  usr_ks->ciphertext_len = ciphertext_len;
  memcpy(usr_ks->nonce, nonce, nonce_len);
  usr_ks->nonce_len = nonce_len;
  memcpy(usr_ks->salt, salt, salt_len);
  usr_ks->salt_len = salt_len;

  usr_ks->kdf.type       = "argon2id";
  usr_ks->kdf.params.mem = mem_limit;
  usr_ks->kdf.params.ops = op_limit;

  return true;
}

bool compute_address(char address[ADDRESS_STR_MAX], uint32_t *address_len,
                     const uint8_t public_key[PUBLIC_KEY_BIN_LEN]) {
  uint8_t address_bin[ADDRESS_BIN_LEN];
  uint8_t blake3_hash[BLAKE3_OUT_LEN];
  uint8_t payload[VERSION_BIN_LEN + HASH160_BIN_LEN]; // 1 + 20 = 21
  uint8_t sha3_hash[SHA3_256_BIN_LEN];

  payload[0] = NORMAL_ADDR;
  blake3(blake3_hash, public_key, PUBLIC_KEY_BIN_LEN);
  memcpy(payload + 1, blake3_hash, HASH160_BIN_LEN); // 20 bytes

  if (SHA3_256(sha3_hash, payload, VERSION_BIN_LEN + HASH160_BIN_LEN) != 0) {
    LOG("Could not compute hash (SHA3-256)");
    return false;
  }

  memcpy(address_bin, payload, VERSION_BIN_LEN + HASH160_BIN_LEN); // 21 bytes
  memcpy(address_bin + VERSION_BIN_LEN + HASH160_BIN_LEN, sha3_hash,
         CHECKSUM_BIN_LEN); // +4 = 25

  *address_len = ADDRESS_STR_MAX;
  b58enc(address, address_len, address_bin, ADDRESS_BIN_LEN);
  return true;
}

bool pubkey_matches_address(const uint8_t public_key[SHA256_BIN_BUF_LEN],
                            const char   *address) {
  char     computed_address[ADDRESS_STR_MAX] = {0};
  uint32_t len                               = 0;
  return (bool)((int)compute_address(computed_address, &len, public_key) &&
                (strncmp(computed_address, address, ADDRESS_STR_MAX) == 0));
}

bool create_wallet(keystore *usr_ks, char *pword, node_context_t *ctx) {
  if (keystore_exists(ctx)) {
    LOG("Keystore exists.");
    return false;
  }

  char password[BUFFER_SMALL];
  if (!pword) {
    request_password(password);
    pword = password;
  }

  uint8_t private_key[PRIVATE_KEY_BIN_LEN];
  uint8_t public_key[PUBLIC_KEY_BIN_LEN];
  if (!generate_key_pair(public_key, private_key)) {
    LOG("Failed to generate key pair.");
    return false;
  }

  uint8_t salt[SALT_BIN_LEN];
  randombytes_buf(salt, sizeof(salt));

  uint32_t op_limit  = crypto_pwhash_OPSLIMIT_MODERATE;
  uint64_t mem_limit = crypto_pwhash_MEMLIMIT_MODERATE;

  uint8_t key[KEY_BIN_LEN];
  if (!derive_encryption_key(pword, salt, op_limit, mem_limit, key)) {
    LOG("Failed to derive encryption key.");
    return false;
  }

  uint8_t  ciphertext[CIPHERTEXT_BIN_LEN];
  uint64_t ciphertext_len = CIPHERTEXT_BIN_LEN;
  uint8_t  nonce[NONCE_BIN_LEN];
  randombytes_buf(nonce, sizeof(nonce));

  crypto_aead_xchacha20poly1305_ietf_encrypt(ciphertext, &ciphertext_len,
                                             private_key, PRIVATE_KEY_BIN_LEN,
                                             nullptr, 0, nullptr, nonce, key);

  char     address[ADDRESS_STR_MAX] = {0};
  uint32_t address_len              = ADDRESS_STR_MAX;
  if (!compute_address(address, &address_len, public_key)) {
    LOG("Failed to compute address.");
    return false;
  }

  if (!assemble_keystore(usr_ks, address, private_key, public_key, ciphertext,
                         ciphertext_len, nonce, NONCE_BIN_LEN, salt,
                         SALT_BIN_LEN, mem_limit, op_limit)) {
    return false;
  }

  if (!store_keystore_json(usr_ks, key, ctx)) {
    LOG("Failed to store keystore.");
    return false;
  }
  return true;
}

bool store_keystore_json(keystore *usr_ks, const uint8_t key[KEY_BIN_LEN],
                         node_context_t *ctx) {
  char *ks_file_path = join_path(get_thrift_home(ctx), "keystore.json");
  FILE *ks_file      = fopen(ks_file_path, "wb");
  if (!ks_file) {
    LOG("Could not create file: %s.", ks_file_path);
    return false;
  }

  char ciphertext_hex[CIPHERTEXT_HEX_LEN + 1];
  char nonce_hex[NONCE_HEX_LEN + 1];
  char salt_hex[SALT_HEX_LEN + 1];

  strcpy(ciphertext_hex,
         hash_bin_to_hex(usr_ks->ciphertext, usr_ks->ciphertext_len,
                         (char[CIPHERTEXT_HEX_LEN + 1]){0}));
  strcpy(nonce_hex, hash_bin_to_hex(usr_ks->nonce, usr_ks->nonce_len,
                                    (char[NONCE_HEX_LEN + 1]){0}));
  strcpy(salt_hex, hash_bin_to_hex(usr_ks->salt, usr_ks->salt_len,
                                   (char[SALT_HEX_LEN]){0}));

  uint8_t mac[MAC_BIN_LEN];
  compute_mac(usr_ks->address, ciphertext_hex, nonce_hex, salt_hex, key, mac);
  char mac_hex[MAC_HEX_LEN + 1];
  strcpy(mac_hex,
         hash_bin_to_hex(mac, MAC_BIN_LEN, (char[MAC_HEX_LEN + 1]){0}));

  char keystore_json[BUFFER_SMALL];
  int  len = snprintf(keystore_json, sizeof(keystore_json),
                      "{\n"
                       "  \"address\": \"%s\",\n"
                       "  \"cipher\": \"%s\",\n"
                       "  \"ciphertext\": \"%s\",\n"
                       "  \"nonce\": \"%s\",\n"
                       "  \"salt\": \"%s\",\n"
                       "  \"kdf\": {\n"
                       "    \"type\": \"%s\",\n"
                       "    \"params\": { \"mem\": %" PRIu64 ", \"ops\": %" PRIu32
                      " }\n"
                       "  },\n"
                       "  \"mac\": \"%s\"\n"
                       "}\n",
                      usr_ks->address, usr_ks->cipher, ciphertext_hex, nonce_hex,
                      salt_hex, usr_ks->kdf.type, usr_ks->kdf.params.mem,
                      usr_ks->kdf.params.ops, mac_hex);

  fwrite(keystore_json, 1, len, ks_file);
  fclose(ks_file);
  return true;
}

uint32_t concat_mac_text_source(const char *address, const char *ciphertext_hex,
                                const char *nonce_hex, const char *salt_hex,
                                char text[BUFFER_SMALL]) {
  return snprintf(text, BUFFER_SMALL, "%s|%s|%s|%s", address, ciphertext_hex,
                  nonce_hex, salt_hex);
}

bool compute_mac(const char *address, const char *ciphertext_hex,
                 const char *nonce_hex, const char *salt_hex,
                 const uint8_t key[KEY_BIN_LEN], uint8_t hash[MAC_BIN_LEN]) {
  char   text[BUFFER_SMALL];
  size_t text_len = concat_mac_text_source(address, ciphertext_hex, nonce_hex,
                                           salt_hex, text);
  return crypto_onetimeauth(hash, (const uint8_t *)text, text_len, key) == 0;
}

bool verify_mac(const char *address, const char *ciphertext_hex,
                const char *nonce_hex, const char *salt_hex,
                const uint8_t key[KEY_BIN_LEN],
                const uint8_t hash[MAC_BIN_LEN]) {
  char   text[BUFFER_SMALL];
  size_t text_len = concat_mac_text_source(address, ciphertext_hex, nonce_hex,
                                           salt_hex, text);
  return crypto_onetimeauth_verify(hash, (const uint8_t *)text, text_len,
                                   key) == 0;
}

bool load_keystore(keystore *usr_ks, char *pword, node_context_t *ctx) {
  char password[BUFFER_SMALL];
  if (!pword) {
    request_password(password);
    pword = password;
  }

  char *ks_file_path = join_path(get_thrift_home(ctx), "keystore.json");
  FILE *ks_file      = fopen(ks_file_path, "r");
  if (!ks_file) {
    LOG("Could not open file: %s.", ks_file_path);
    return false;
  }

  json_value *ks_json = json_parse_file(ks_file);
  fclose(ks_file);
  if (!ks_json || json_value_type(ks_json) != DICT) {
    LOG("Malformed JSON, expected an object.");
    return false;
  }

  json_value *query;

  query            = json_query(ks_json, "salt");
  char   *salt_hex = json_value_data(query).string;
  uint8_t salt[SALT_BIN_LEN];
  hash_hex_to_bin(salt_hex, strlen(salt_hex), salt);

  query                = json_query(ks_json, "kdf");
  query                = json_query(query, "params");
  json_value *mem_json = json_query(query, "mem");
  uint64_t    memlimit = (uint64_t)json_value_data(mem_json).integer;
  json_value *ops_json = json_query(query, "ops");
  uint32_t    op_limit = (uint32_t)json_value_data(ops_json).integer;

  uint8_t key[KEY_BIN_LEN];
  if (!derive_encryption_key(pword, salt, op_limit, memlimit, key)) {
    LOG("Failed to derive encryption key.");
    return false;
  }

  query                  = json_query(ks_json, "ciphertext");
  char   *ciphertext_hex = json_value_data(query).string;
  uint8_t ciphertext[CIPHERTEXT_BIN_LEN];
  size_t  ciphertext_len = strlen(ciphertext_hex) / 2;
  hash_hex_to_bin(ciphertext_hex, strlen(ciphertext_hex), ciphertext);

  query             = json_query(ks_json, "nonce");
  char   *nonce_hex = json_value_data(query).string;
  uint8_t nonce[NONCE_BIN_LEN];
  hash_hex_to_bin(nonce_hex, strlen(nonce_hex), nonce);

  uint8_t  private_key[PRIVATE_KEY_BIN_LEN];
  uint64_t private_key_len = PRIVATE_KEY_BIN_LEN;
  if (crypto_aead_xchacha20poly1305_ietf_decrypt(
          private_key, &private_key_len, nullptr, ciphertext, ciphertext_len,
          nullptr, 0, nonce, key) != 0) {
    LOG("Invalid password.");
    return false;
  }

  uint8_t public_key[PUBLIC_KEY_BIN_LEN];
  crypto_sign_ed25519_sk_to_pk(public_key, private_key);

  query         = json_query(ks_json, "address");
  char *address = json_value_data(query).string;

  query           = json_query(ks_json, "mac");
  char   *mac_hex = json_value_data(query).string;
  uint8_t mac[MAC_BIN_LEN];
  hash_hex_to_bin(mac_hex, strlen(mac_hex), mac);

  if (!verify_mac(address, ciphertext_hex, nonce_hex, salt_hex, key, mac)) {
    LOG("Keystore has been tampered with.");
    return false;
  }

  return assemble_keystore(usr_ks, address, private_key, public_key, ciphertext,
                           ciphertext_len, nonce, NONCE_BIN_LEN, salt,
                           SALT_BIN_LEN, memlimit, op_limit);
}

void request_password(char password[BUFFER_SMALL]) {
  get_password(password, BUFFER_SMALL);
}

bool sign_buffer(uint8_t signature[ED25519_BIN_LEN], const char *buffer,
                 uint64_t buffer_length, keystore *usr_ks) {
  uint64_t sig_len = ED25519_BIN_LEN;
  return crypto_sign_ed25519_detached(signature, &sig_len,
                                      (const uint8_t *)buffer, buffer_length,
                                      usr_ks->private_key) == 0;
}

bool verify_buffer(const uint8_t signature[ED25519_BIN_LEN], const char *buffer,
                   uint64_t      buffer_length,
                   const uint8_t public_key[PUBLIC_KEY_BIN_LEN]) {
  return crypto_sign_ed25519_verify_detached(signature, (const uint8_t *)buffer,
                                             buffer_length, public_key) == 0;
}

void copy_public_key(keystore *usr_ks, tx_t *txn) {
  memcpy(txn->public_key, usr_ks->public_key, PUBLIC_KEY_BIN_LEN);
}

char *get_address_from_keystore(keystore *user_keystore) {
  return user_keystore->address;
}

bool keystore_exists(node_context_t *ctx) {
  char *keystore_path = join_path(get_thrift_home(ctx), "keystore.json");
  int   ret           = access(keystore_path, F_OK);
  return ret == 0;
}
