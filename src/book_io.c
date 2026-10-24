#include "book.h"
#include "ds.h"
#include "genesis_header.h"
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

  struct stat sbs;

  return stat(fpath, &sbs) == 0;
}

array_t *parse_block(book_t *book, FILE *blocks_file) {
  json_value *saved_block = json_parse_file(blocks_file);
  if (!saved_block)
    return nullptr;

  array_t *blocks = create_array();

  if (json_value_type(saved_block) == LIST) {
    auto saved_blocks = json_value_data(saved_block).ptr;
    auto len          = array_length(saved_blocks);
    for (uint32_t i = 0; i < len; ++i) {
      auto saved_blk = array_get_ptr(saved_blocks, i);
      auto block     = zmalloc(sizeof(block_t));
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

bool save_block(book_t *book, char *miner, node_context_t *ctx) {
  block_t *block = &book->block;
  if (array_length(block->txs) == 0) {
    return false;
  }

  if (miner && block->miner != miner) {
    strncpy(block->miner, miner, ADDRESS_STR_MAX - 1);
    block->miner[ADDRESS_STR_MAX - 1] = '\0';
  }

  char buffer[BUFFER_BIG];
  char block_hash_hex[SHA256_HEX_BUF_LEN];

  auto len = block_to_str(buffer, sizeof(buffer), block);
  hash_buffer(buffer, len, block_hash_hex);

#define EXT_LEN (15)
  char hash_path[SHA256_HEX_BUF_LEN + EXT_LEN];
  snprintf(hash_path, sizeof(hash_path), "blocks/%s.json", block_hash_hex);

  auto block_path = join_path(get_thrift_home(ctx), hash_path);

  FILE *block_file = fopen(block_path, "wb");
  if (!block_file)
    return false;
  fprintf(block_file, "%s", buffer);
  fclose(block_file);

  FILE *head = fopen(join_path(get_thrift_home(ctx), "HEAD.txt"), "wb");
  if (!head)
    return false;
  fprintf(head, "%s", block_hash_hex);
  fclose(head);

  memcpy(book->block.parent_hash, block_hash_hex, SHA256_HEX_BUF_LEN);
  book->block.txs       = create_array();
  book->last_block_time = time(nullptr);
  set_block_time(&book->block);
  ++book->block.block_idx;
  return true;
}

char *join_path(const char *parent, const char *child) {
  auto  parent_len = strlen(parent);
  auto  len        = parent_len + strlen(child) + 2;
  char *path       = zmalloc(len);
  strncpy(path, parent, len);

  path[parent_len++] = '/';
  path[parent_len++] = '\0';

  strncat(path, child, len - parent_len);

  return path;
}

char *extract_parent_hash(char *buffer) {
  auto tmp = strstr(buffer, "parent_hash");
  if (!tmp)
    return nullptr;

  tmp = strchr(tmp, ':');
  if (!tmp)
    return nullptr;

  tmp = strchr(tmp, '"');
  if (!tmp)
    return nullptr;

  char *current_hash           = tmp + 1;
  current_hash[SHA256_HEX_LEN] = '\0';

  return current_hash;
}

array_t *list_blocks(const char *blocks_path, char *head_block) {

  array_t *paths = create_array();

  char genesis_hash[SHA256_HEX_BUF_LEN];
  hash_buffer((char *)genesis_json, strlen(genesis_json), genesis_hash);

  char  hashjson[SHA256_HEX_BUF_LEN + EXT_LEN];
  char *current_hash = head_block;

  while (true) {
    snprintf(hashjson, sizeof(hashjson), "%s.json", current_hash);

    auto block_path = join_path(blocks_path, hashjson);

    FILE *file = fopen(block_path, "rb");
    if (!file)
      return nullptr;

    array_append_str(paths, zstrdup(hashjson));

    char buffer[BUFFER_SMALL];
    auto read_size = fread(buffer, 1, BUFFER_SMALL, file);
    read_size      = read_size >= BUFFER_SMALL ? BUFFER_SMALL - 1 : read_size;
    buffer[read_size] = 0;

    fclose(file);

    current_hash = extract_parent_hash(buffer);
    if (!current_hash)
      return nullptr;

    if (strncmp(current_hash, genesis_hash, SHA256_HEX_LEN) == 0)
      break;
  }

  return paths;
}

bool replay_txs(book_t *book, const char *restrict home_path) {
  auto  head_path = join_path(home_path, "HEAD.txt");
  FILE *head      = fopen(head_path, "rb");
  if (!head)
    return false;

  char buffer[BUFFER_BIG];
  char head_block[SHA256_HEX_BUF_LEN];
  char hash_out[SHA256_HEX_BUF_LEN];

  auto read_size        = fread(head_block, 1, SHA256_HEX_LEN, head);
  head_block[read_size] = 0;
  fclose(head);

  auto blocks_path = join_path(home_path, "blocks");

  array_t *hashpaths = list_blocks(blocks_path, head_block);

  auto len = array_length(hashpaths);
  for (uint32_t i = len; i-- > 0;) {
    char *hashjson   = array_get_ptr(hashpaths, i);
    auto  block_path = join_path(blocks_path, hashjson);
    auto  blockjson  = fopen(block_path, "rb");
    if (!blockjson)
      return false;

    auto read_size    = fread(buffer, 1, sizeof(buffer), blockjson);
    buffer[read_size] = 0;

    fclose(blockjson);

    hash_buffer(buffer, read_size, hash_out);

    if (strncmp(hash_out, hashjson, SHA256_HEX_LEN))
      return false;

    block_t blk;
    auto    block_json = json_parse_string(buffer);
    block_from_json(&blk, block_json);

    if (!effect_block(&blk, book))
      return false;
  }

  memcpy(&book->block.parent_hash, hash_out, SHA256_HEX_LEN);
  book->block.block_idx = len + 1;
  set_block_time(&book->block);

  return true;
}

const char *get_thrift_home(node_context_t *ctx) {

  if (ctx->config.thrift_path != nullptr) {
    return ctx->config.thrift_path;
  }

  const char *thrift_env = getenv("THRIFT_HOME");
  if (thrift_env != nullptr && thrift_env[0] != '\0') {
    ctx->config.thrift_path = zstrdup(thrift_env);
    return ctx->config.thrift_path;
  }

  const char *home = getenv("HOME");
  if (home != nullptr && home[0] != '\0') {
    ctx->config.thrift_path = join_path(home, ".thrift/");
    return ctx->config.thrift_path;
  }

  return nullptr;
}

bool setup_file_structure(const char *home) {
  if (!file_exists(home) && make_dir(home) != 0)
    return false;
  auto path = join_path(home, "blocks");
  if (file_exists(path))
    return true;
  return make_dir(path) == 0;
}

bool load_peers(array_t *peers, node_context_t *ctx) {
  if (!peers)
    return false;

  FILE *peers_json = fopen(join_path(get_thrift_home(ctx), "peers.json"), "rb");
  if (!peers_json)
    return false;

  json_value *json = json_parse_file(peers_json);
  fclose(peers_json);

  if (!json || json_value_type(json) != LIST)
    return false;

  array_t *saved_peers = json_value_data(json).ptr;
  uint32_t n_peers     = array_length(saved_peers);
  for (uint32_t i = 0; i < n_peers; ++i) {
    json_value *saved_peer = array_get_ptr(saved_peers, i);
    if (!saved_peer || json_value_type(saved_peer) != DICT)
      continue;

    peer_t *peer = zcalloc(1, sizeof(*peer));

    json_value *query = json_query(saved_peer, "ip");
    if (!query || json_value_type(query) != TEXT) {
      zfree(peer);
      continue;
    }
    peer->ip = zstrdup(json_value_data(query).string);

    query = json_query(saved_peer, "port");
    if (!query) {
      zfree(peer);
      continue;
    }
    if (json_value_type(query) == INTEGERS)
      peer->port = json_value_data(query).integer;
    else {
      zfree(peer);
      continue;
    }

    query = json_query(saved_peer, "name");
    if (query && json_value_type(query) == TEXT)
      peer->name = zstrdup(json_value_data(query).string);

    array_append_ptr(peers, peer);
  }

  return true;
}

bool save_peers(array_t *peers, node_context_t *ctx) {

  FILE *peers_json = fopen(join_path(get_thrift_home(ctx), "peers.json"), "wb");
  fwrite("[", 1, 1, peers_json);

  uint32_t n_peers = array_length(peers);
  for (uint32_t i = 0; i < n_peers; ++i) {
    peer_t *peer = array_get_ptr(peers, i);
    fprintf(peers_json, "%s{\"name\":\"%s\",\"ip\": \"%s\", \"port\":%d}",
            i > 0 ? "," : "", peer->name, peer->ip, peer->port);
  }
  fwrite("]", 1, 1, peers_json);
  fclose(peers_json);

  return true;
}
