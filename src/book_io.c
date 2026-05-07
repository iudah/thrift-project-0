#include "book.h"
#include "ds.h"
#include "ison.h"
#include "zot.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define make_dir(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define make_dir(path) mkdir(path, 0777)
#endif

bool file_exists(const char *restrict fpath) {

  struct stat sb;

  return stat(fpath, &sb) == 0;
}

bool hash_file_and_save(char *fpath, uint8_t hash_out[HASH_STR_LEN]) {
  FILE *f = fopen(fpath, "rb");

  if (!f)
    return false;

  hash_file(f, hash_out);

  fclose(f);

  FILE *hash_file = fopen(HASH_FILE_PATH, "w");
  if (hash_file) {
    fwrite(hash_file, 1, HASH_STR_LEN, hash_file);
    fclose(hash_file);
  }

  return true;
}

array_t *parse_block(book_t *book, FILE *blocks_file) {
  json_value *saved_block = json_parse_file(blocks_file);
  if (!saved_block)
    return NULL;

  array_t *blocks = create_array();

  if (json_value_type(saved_block) == LIST) {
    auto saved_blocks = json_value_data(saved_block).ptr;
    auto len = array_length(saved_blocks);
    for (uint32_t i = 0; i < len; ++i) {
      auto saved_blk = array_get_ptr(saved_blocks, i);
      auto block = zmalloc(sizeof(block_t));
      block_from_json(block, saved_blk);

      array_append_ptr(blocks, block);
    }
  } else {
    auto block = zmalloc(sizeof(block_t));
    block_from_json(block, saved_block);

    array_append_ptr(blocks, block);
  }

  return blocks;
}

bool save_block(block_t *block) {
  if (array_length(block->txs) == 0) {
    return false;
  }

  char buffer[4096];
  char block_hash_hex[65];

  auto len = block_to_str(buffer, sizeof(buffer), block);
  hash_buffer(buffer, len, block_hash_hex);

  char hash_path[80];
  snprintf(hash_path, sizeof(hash_path), "blocks/%s.json", block_hash_hex);

  auto path = join_path(get_thrift_home(), hash_path);

  FILE *f = fopen(path, "wb");
  if (!f)
    return false;
  fprintf(f, "%s", buffer);
  fclose(f);

  FILE *head = fopen(join_path(get_thrift_home(), "HEAD.txt"), "wb");
  if (!head)
    return false;
  fprintf(head, "%s", block_hash_hex);
  fclose(head);

  return true;
}

char *join_path(const char *parent, const char *child) {
  auto parent_len = strlen(parent);
  auto len = parent_len + strlen(child) + 2;
  char *path = zmalloc(len);
  strncpy(path, parent, len);

  path[parent_len++] = '/';
  path[parent_len++] = '\0';

  strncat(path, child, len - parent_len);

  return path;
}

char *extract_parent_hash(char *buffer) {
  auto tmp = strstr(buffer, "parent_hash");
  if (!tmp)
    return NULL;

  tmp = strchr(tmp, ':');
  if (!tmp)
    return NULL;

  tmp = strchr(tmp, '"');
  if (!tmp)
    return NULL;

  char *current_hash = tmp + 1;
  current_hash[64] = '\0';

  return current_hash;
}

bool is_genesis_parent(char hash[64]) {

  uint8_t i;
  for (i = 0; i < 64; ++i) {
    if (hash[i] != '0')
      break;
  }
  return (i == 64);
}

array_t *list_blocks(const char *blocks_path, char *head_block) {

  array_t *paths = create_array();

  char hashjson[70];
  char *current_hash = head_block;

  while (true) {
    snprintf(hashjson, sizeof(hashjson), "%s.json", current_hash);

    auto block_path = join_path(blocks_path, hashjson);

    FILE *file = fopen(block_path, "rb");
    if (!file)
      return NULL;

    array_append_str(paths, zstrdup(hashjson));

    char buffer[85];
    auto read_size = fread(buffer, 1, 84, file);
    buffer[read_size] = 0;

    fclose(file);

    current_hash = extract_parent_hash(buffer);
    if (!current_hash)
      return NULL;

    if (is_genesis_parent(current_hash))
      break;
  }

  return paths;
}

bool replay_txs(book_t *book, const char *restrict home_path) {
  auto head_path = join_path(home_path, "HEAD.txt");
  FILE *head = fopen(head_path, "rb");
  if (!head)
    return false;

  char buffer[4096];
  char head_block[65];
  char hash_out[65];
  auto read_size = fread(head_block, 1, 65, head);
  head_block[read_size] = 0;
  fclose(head);

  auto blocks_path = join_path(home_path, "blocks");

  array_t *hashpaths = list_blocks(blocks_path, head_block);

  auto len = array_length(hashpaths);
  for (uint32_t i = len; i-- > 0;) {
    char *hashjson = array_get_ptr(hashpaths, i);
    auto path = join_path(blocks_path, hashjson);
    auto f = fopen(path, "rb");
    if (!f)
      return false;

    auto read_size = fread(buffer, 1, sizeof(buffer), f);
    buffer[read_size] = 0;

    fclose(f);

    hash_buffer(buffer, read_size, hash_out);

    if (strncmp(hash_out, hashjson, 64))
      return false;

    block_t blk;
    auto block_json = json_parse_string(buffer);
    block_from_json(&blk, block_json);

    if (!audit_block(&blk))
      return false;
    if (!effect_block(&blk, book))
      return false;
  }

  memcpy(&book->block.parent_hash, hash_out, 64);

  return true;
}

static char *thrift_home = NULL;
bool set_thrift_home(char *path) {
  thrift_home = zstrdup(path);
  return true;
}

const char *get_thrift_home() {

  if (thrift_home != NULL) {
    return thrift_home;
  }

  const char *thrift_env = getenv("THRIFT_HOME");
  if (thrift_env != NULL && thrift_env[0] != '\0') {
    thrift_home = zstrdup(thrift_env);
    return thrift_home;
  }

  const char *home = getenv("HOME");
  if (home != NULL && home[0] != '\0') {
    size_t len = strlen(home) + strlen("/.thrift") + 1;
    thrift_home = (char *)zmalloc(len);
    if (thrift_home != NULL) {
      thrift_home = join_path(home, ".thrift/");
    }
    return thrift_home;
  }

  return NULL;
}

bool setup_file_structure(const char *home) {
  if (!file_exists(home) && make_dir(home) != 0)
    return false;
  auto path = join_path(home, "blocks");
  if (!file_exists(path) && make_dir(path) != 0)
    return false;

  return true;
}
