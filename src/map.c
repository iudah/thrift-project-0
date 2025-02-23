#include "map.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

map_t *new_map() { return calloc(1, sizeof(map_t)); }

void map_append_xxx(map_t *map, void *key, union ds_val value,
                    ds_value_enum_t type) {
  if (map->length == map->capacity) {
    map->capacity += 8;
    if (!map->pairs) {
      map->pairs = malloc(map->capacity * sizeof(*map->pairs));
    } else {
      void *tmp = realloc(map->pairs, map->capacity * sizeof(*map->pairs));

      if (!tmp) {
        perror("OOM");
        abort();
      }

      map->pairs = tmp;
    }
  }
  switch (type) {
    case PTR_V:
      map->pairs[map->length].value.value.ptr = value.ptr;
      break;
    case NUMBER_V:
      map->pairs[map->length].value.value.number = value.number;
      break;
  }
  map->pairs[map->length].key = key;
  map->pairs[map->length].value.value_type = type;
  map->length++;
}

void map_append_number(map_t *map, void *key, double value) {
  union ds_val val = {.number = value};
  return map_append_xxx(map, key, val, NUMBER_V);
}
void map_append_ptr(map_t *map, void *key, void *value) {
  union ds_val val = {.ptr = value};
  return map_append_xxx(map, key, val, PTR_V);
}
map_pair_t *map_get(map_t *map, void *key, int (*cmp)(void *k1, void *k2)) {
  map_pair_t *pair = map->pairs;
  uint64_t i = 0;
  while (i < map->length) {
    if (cmp ? !cmp(map->pairs[i].key, key) : (key == map->pairs[i].key)) {
      return map->pairs + i;
    }
    i++;
  }
  return NULL;
}

static _Thread_local map_t *current_map = NULL;
static _Thread_local uint64_t idx = 0;
void map_iter(map_t *map) {
  current_map = map;
  idx = 0;
  return;
}

int map_next(void **key, ds_value_t **val) {
  if (current_map && (idx < current_map->length)) {
    *key = current_map->pairs[idx].key;
    *val = &current_map->pairs[idx].value;
    idx++;
    return 0;
  }
  return -1;
}