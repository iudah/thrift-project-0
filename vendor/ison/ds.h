#ifndef DS_H
#define DS_H

#include "ison.h"
#include <inttypes.h>

array_t *create_array();
uint64_t array_length(array_t *array);
int array_get_int(array_t *array, int idx);
void destroy_array(array_t *array);
void array_append_bool(array_t *array, bool value);
void array_append_number(array_t *array, double value);
void array_append_ptr(array_t *array, void *value);
void array_append_str(array_t *array, char *value);
void array_append_list(array_t *array, array_t *value);
void array_append_dict(array_t *array, hash_map *value);
void array_append_int(array_t *array, int i);
map_value *array_get(array_t *array, int idx);
int array_get_int(array_t *array, int idx);
void *array_get_ptr(array_t *array, int idx);
void *array_remove_last_ptr(array_t *array);
int array_remove_last_int(array_t *array);

hash_map *create_hash_map();
bool hash_map_add_ptr(hash_map *map, char *key, void *ptr);
bool hash_map_add_dict(hash_map *map, char *key, hash_map *dict);
bool hash_map_add_list(hash_map *map, char *key, array_t *list);
bool hash_map_add_int(hash_map *map, char *key, int integer);
bool hash_map_add_bool(hash_map *map, char *key, bool boolean);
bool hash_map_add_number(hash_map *map, char *key, double d);
bool hash_map_add_str(hash_map *map, char *key, char *str);
map_value *hash_map_get(hash_map *map, char *key);
bool hash_map_replace_number(hash_map *map, char *key, double d);

#endif