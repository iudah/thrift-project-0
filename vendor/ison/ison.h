#ifndef ISON_H
#define ISON_H

#include <inttypes.h>
#include <stdio.h>

typedef struct array array_t;
typedef struct hash_node hash_node;
typedef struct hash_map hash_map;
typedef struct map_value map_value;
typedef enum map_value_type map_value_type;
typedef map_value json_value;

typedef union json_value_union {
  char *string;
  int integer;
  void *ptr;
  double number;
  bool boolean;
} json_data;

enum map_value_type {
  UNKNOWN,
  DICT,
  LIST,
  TEXT,
  POINTER,
  INTEGERS,
  FLOATS,
  BOOLEANS,
  NULLS
};

json_value *json_parse_file(FILE *f);

json_value *json_parse_string(char *str);

json_value *json_query(json_value *node, char *key);

map_value_type json_value_type(json_value *node);

json_data json_value_data(json_value *node);

#endif