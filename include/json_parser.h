#ifndef JSON_PARSER_H
#define JSON_PARSER_H

#include <stdint.h>
#include <stdio.h>

typedef enum {
  NO_TOKEN,
  CURLY_OPEN,
  CURLY_CLOSE,
  SQR_OPEN,
  SQR_CLOSE,
  COMMA,
  STRING,
  COLON,
  NUMBER,
  OBJECT,
  ARRAY,
  BOOLEAN,
  NIL,
  VALUE
} token_t;

// typedef enum { JSONNONODE, JSONOBJECT, JSONARRAY, JSONVALUE } jsen_t;

// typedef enum { JSONBOOLFALSE, JSONBOOLTRUE } json_bool_enum_t;

typedef struct {
  union {
    char *string;
    void *array;
    void *null;
    struct json_node *node;
    double number;
    bool boolean;
  } value;
  token_t value_type;
} json_value_t;

typedef struct json_node {
  char *key;
  json_value_t value;
  struct json_node *next_pair;
} json_node_t;

typedef struct {
  token_t *tokens;
  union token_value {
    void *null;
    char *string;
    double number;
    bool boolean;
  } *values;
  uint64_t tok_rear;
  uint64_t tok_front;
  uint64_t tok_limit;
  uint64_t val_rear;
  uint64_t val_front;
  uint64_t val_limit;
} tokens_t;

typedef struct {
  json_node_t *current;
} json_iter_t;

json_node_t *parse_json(FILE *f);
struct json_node *parse_json_string(char *str);
bool tokenize(FILE *f, tokens_t *tokens);
bool parse(tokens_t *tokens, json_node_t *node);
json_node_t *json_get(json_node_t *json_node, char *key_string);
json_value_t *json_get_value(json_node_t *json_node, char *key_string);
json_iter_t *json_iter(json_node_t *json_node);
int json_next(json_iter_t *state, char **key, json_value_t **val);

#endif
