#ifndef MAP_H
#define MAP_H

#include "ds.h"
#include <stdint.h>

typedef struct map_pair {
  void *key;
  ds_value_t value;
} map_pair_t;

typedef struct map {
  map_pair_t *pairs;
  uint64_t length;
  uint64_t capacity;
} map_t;

map_t *new_map();
void map_append_number(map_t *map, void *key, double value);
void map_append_ptr(map_t *map, void *key, void *value);
map_pair_t *map_get(map_t *map, void *key, int (*cmp)(void *k1, void *k2));
void map_iter(map_t *map);
int map_next(void **key, ds_value_t **val);

#endif