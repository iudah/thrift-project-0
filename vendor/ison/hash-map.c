#include "ds.h"
#include "ison_data.h"
#include <siphash.h>
#include <stdbool.h>
#include <stdint.h>
#include <zot.h>

#define HASHMAP_LIMIT 32
#define HASH_LEN 16

char key[16];

void __attribute__((__constructor__)) init() { arc4random_buf(key, 16); }

struct hash_node {
  char *key;
  map_value value;
};

struct hash_map {
  hash_node **nodes;
};

hash_map *create_hash_map() {
  hash_map *map = zmalloc(sizeof(*map));
  map->nodes = zmalloc(HASHMAP_LIMIT * sizeof(*map->nodes));

  return map;
}

uint64_t compute_hash(char *string) {
  uint8_t digest[HASH_LEN];
  siphash(string, strlen(string), key, digest, HASH_LEN);
#if (HASH_LEN == 16)
  uint64_t ms64b = *(uint64_t *)digest;
  uint64_t ls64b = *(uint64_t *)(digest + 8);
  uint64_t hash_int = ms64b ^ ls64b;
#else
  uint64_t hash_int = *(uint64_t *)digest;
#endif

#if (HASHMAP_LIMIT > 0 && (HASHMAP_LIMIT & (HASHMAP_LIMIT - 1)) == 0)
  // Power-of-two map limit
  uint64_t index = hash_int & (HASHMAP_LIMIT - 1);
#else
  uint64_t index = hash_int % HASHMAP_LIMIT;
#endif

  return index;
}

bool hash_map_add(hash_map *map, char *key, map_value *value) {
  uint32_t key_len = strlen(key) + 1;
  // Allocate memory for a new hash node (managed by custom GC)
  hash_node *node = zmalloc(sizeof(*node));

  // Compute the hash index for the key
  size_t idx = compute_hash(key);

  // Check if there is already a node at the computed index
  if (map->nodes[idx] != NULL) {
    // If the existing node has a key, it's a direct node, not yet a collision
    // list
    if (map->nodes[idx]->key) {
      // If the key matches the existing node, it's a duplicate
      if (!strncmp(key, map->nodes[idx]->key, key_len)) {
        LOG_ERROR("Duplicate key \"%s\". Found '%s'", key,
                  map->nodes[idx]->key);
        return false;
      } else {
        // Collision detected with a different key that hashed to the same index
        // Promote the existing node to an array to handle multiple keys
        hash_node *conflicting_node = map->nodes[idx];

        // Copy the existing node's key and value to the new node
        node->key = conflicting_node->key;
        memcpy(&node->value, &conflicting_node->value, sizeof(node->value));

        // Mark the original node as a collision holder by nullifying its key
        conflicting_node->key = NULL;

        // Create a new array to hold nodes with the same hash index
        array_t *array = create_array();
        conflicting_node->value.value.ptr = array;

        // Add the copied conflicting node to the collision array
        array_append_ptr(array, node);

        // Allocate a new node for the key-value pair being added
        node = zmalloc(sizeof(*node));

        // Add the new node to the collision array
        array_append_ptr(array, node);
      }
    } else {
      // Existing node is already a collision array (key is NULL)

      // Retrieve the collision array from the existing node
      array_t *array = (void *)map->nodes[idx]->value.value.ptr;
      uint64_t len = array_length(array);

      // Check for duplicate keys within the collision array
      for (uint64_t i = 0; i < len; i++) {
        hash_node *existing_node = array_get_ptr(array, i);

        if (!strncmp(key, existing_node->key, key_len)) {
          LOG_ERROR("Duplicate key \"%s\".", key);
          return false;
        }
      }

      // No duplicates found, append the new node to the collision array
      array_append_ptr(array, node);
    }
  } else {
    // If no node exists at the hash index, directly insert the new node
    map->nodes[idx] = node;
  }

  // Assign the key and copy the value into the new node
  node->key = zstrdup(key);
  memcpy(&node->value, value, sizeof(*value));

  return true;
}

bool hash_map_add_ptr(hash_map *map, char *key, void *ptr) {
  map_value val = {.value.ptr = ptr, .type = POINTER};

  return hash_map_add(map, key, &val);
}
bool hash_map_add_dict(hash_map *map, char *key, hash_map *ptr) {
  map_value val = {.value.ptr = ptr, .type = DICT};

  return hash_map_add(map, key, &val);
}
bool hash_map_add_list(hash_map *map, char *key, array_t *list) {
  map_value val = {.value.ptr = list, .type = LIST};

  return hash_map_add(map, key, &val);
}
bool hash_map_add_int(hash_map *map, char *key, int integer) {
  map_value val = {.value.integer = integer, .type = INTEGERS};

  return hash_map_add(map, key, &val);
}
bool hash_map_add_bool(hash_map *map, char *key, bool boolean) {
  {
    map_value val = {.value.boolean = boolean, .type = BOOLEANS};

    return hash_map_add(map, key, &val);
  }
}
bool hash_map_add_number(hash_map *map, char *key, double number) {
  map_value val = {.value.number = number, .type = FLOATS};

  return hash_map_add(map, key, &val);
}
bool hash_map_add_str(hash_map *map, char *key, char *str) {
  map_value val = {.value.ptr = str, .type = TEXT};

  return hash_map_add(map, key, &val);
}

map_value *hash_map_get(hash_map *map, char *key) {
  // Compute hash index for the key
  size_t idx = compute_hash(key);
  hash_node *node = map->nodes[idx];

  // Return NULL immediately if bucket is empty
  if (node == NULL) {
    return NULL;
  }

  // Handle single-node bucket
  if (node->key != NULL) {
    if (strcmp(node->key, key) == 0) {
      return &node->value;
    }
    return NULL; // Key doesn't match single node
  }

  // Handle collision chain
  if (node->value.value.ptr != NULL) {
    array_t *array = node->value.value.ptr;
    uint64_t len = array_length(array);

    for (uint64_t i = 0; i < len; i++) {
      hash_node *chain_node = array_get_ptr(array, i);
      if (chain_node->key && strcmp(chain_node->key, key) == 0) {
        return &chain_node->value;
      }
    }
  }

  return NULL; // Key not found in collision chain
}

bool hash_map_replace(hash_map *map, char *key, map_value *value) {
  // Compute hash index for the key
  size_t idx = compute_hash(key);
  hash_node *node = map->nodes[idx];

  // Return NULL immediately if bucket is empty
  if (node == NULL) {
    return false;
  }

  // Handle single-node bucket
  if (node->key != NULL) {
    if (strcmp(node->key, key) == 0) {
      memcpy(&node->value, value, sizeof(*value));
      return true;
    }
    return false; // Key doesn't match single node
  }

  // Handle collision chain
  if (node->value.value.ptr != NULL) {
    array_t *array = node->value.value.ptr;
    uint64_t len = array_length(array);

    for (uint64_t i = 0; i < len; i++) {
      hash_node *chain_node = array_get_ptr(array, i);
      if (chain_node->key && strcmp(chain_node->key, key) == 0) {
        memcpy(&chain_node->value, value, sizeof(*value));

        return true;
      }
    }
  }

  return false; // Key not found in collision chain
}

bool hash_map_replace_number(hash_map *map, char *key, double number) {
  map_value val = {.value.number = number, .type = FLOATS};

  return hash_map_replace(map, key, &val);
}
