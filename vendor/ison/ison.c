#include "ds.h"
#include "ison_data.h"
#include <errno.h>
#include <string.h>
#include <zot.h>

typedef enum json_token_type json_token_t;

enum json_token_type {
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
};

struct json_value {};

typedef struct token_stream {
  json_token_t *tokens_array;
  union json_token_value {
    void *null;
    void *ptr;
    char *string;
    int integer;
    double number;
    bool boolean;
  } *token_values;
  uint64_t tokens_count;
  uint64_t next_token_index;
  uint64_t tokens_capacity;
  uint64_t values_count;
  uint64_t next_value_index;
  uint64_t values_capacity;
} token_stream;

static inline bool token_stream_append_token(token_stream *tokens,
                                             json_token_t token) {
  if (!tokens->tokens_array)
    tokens->tokens_array =
        zcalloc(tokens->tokens_capacity = 32, sizeof(*tokens->tokens_array));
  else if (tokens->tokens_count >= tokens->tokens_capacity) {
    tokens->tokens_capacity *= 2;
    void *tmp =
        zrealloc(tokens->tokens_array,
                 tokens->tokens_capacity * sizeof(*tokens->tokens_array));
    if (!tmp) {
      LOG_ERROR("Failed to allocate %" PRIu64 " bytes for tokens: %s",
                tokens->tokens_capacity * sizeof(*tokens->tokens_array),
                strerror(errno));
      return false;
    }
    tokens->tokens_array = tmp;
  }
  tokens->tokens_array[tokens->tokens_count++] = token;
  return true;
}

static int current_line = 0;
static int current_column = 0;

bool parse_json_string_literal(char **pos, char **json_string,
                               uint32_t *buf_size) {
  uint32_t len = 0;
  char *c = *pos;
  while (true) {
    if (len >= *buf_size - 1) {
      *buf_size *= 2;
      char *tmp = zrealloc(*json_string, *buf_size);
      if (!tmp) {
        LOG_ERROR("String allocation failed");
        return false;
      }
      *json_string = tmp;
    }
    switch (*c) {
    case '\\': {
      c++;
      char escape;
      switch (*c) {
      case 'n':
        escape = '\n';
        break;
      case '\\':
        escape = '\\';
        break;
      case '"':
        escape = '"';
        break;
      case 'r':
        escape = '\r';
        break;
      case 't':
        escape = '\t';
        break;
      case 'b':
        escape = '\b';
        break;
      case 'f':
        escape = '\f';
        break;

      case 'u':
        c++;
        escape = (char)strtol((char[]){c[0], c[1], c[2], c[3], 0}, NULL, 16);
        c += 3;
        current_column += 4;

        break;
      default:
        LOG_ERROR("Invalid escape sequence \\%c at line %d column %d.", *c,
                  current_line, current_column);
        return false;
      }
      **json_string = escape;
      (*json_string)++;
    }; break;
    case '"':
      *pos = c;
      return true;
      break;
    default:
      (*json_string)[len++] = *c;
    }
    c++;
  }
}

static inline bool token_stream_append_value(token_stream *tokens, char *string,
                                             void *ptr, bool boolean,
                                             double number,
                                             json_token_t token) {
  if (!tokens->token_values) {
    tokens->token_values =
        zcalloc(tokens->values_capacity = 32, sizeof(*tokens->token_values));
  } else if (tokens->values_count >= tokens->values_capacity) {
    tokens->values_capacity *= 2;
    void *tmp =
        zrealloc(tokens->token_values,
                 tokens->values_capacity * sizeof(*tokens->token_values));
    if (!tmp) {
      fprintf(stderr, "Failed to allocate memory for values.\n");
      return false;
    }
    tokens->token_values = tmp;
  }
  switch (token) {
  case STRING:
    tokens->token_values[tokens->values_count++].string = string;
    break;
  case OBJECT:
    tokens->token_values[tokens->values_count++].ptr = ptr;
    break;
  case ARRAY:
    tokens->token_values[tokens->values_count++].ptr = ptr;
    break;
  case BOOLEAN:
    tokens->token_values[tokens->values_count++].boolean = boolean;
    break;
  case NUMBER:
    tokens->token_values[tokens->values_count++].number = number;
    break;
  default:
    break;
  }
  return true;
}

void static inline invalid_number() {
  LOG_ERROR("Invalid number format at line %d column %d.", current_line,
            current_column);
}

bool parse_json_number_literal(char **pos, double *number) {
  char *endptr;
  *number = strtod(*pos, &endptr);
  if (endptr == *pos || errno == ERANGE) {
    invalid_number();
    return false;
  }
  *pos = endptr;
  return true;
}

void static inline invalid_char() {
  LOG_ERROR("Invalid character at line %d column %d.", current_line,
            current_column);
}

bool tokenize_json_string(char *string, token_stream *tokens) {
  current_line++;
  current_column = 0;
  char *c = string;
  while (true) {
    switch (*c) {
    case '{':
      token_stream_append_token(tokens, CURLY_OPEN);
      break;
    case '}':
      token_stream_append_token(tokens, CURLY_CLOSE);
      break;
    case '[':
      token_stream_append_token(tokens, SQR_OPEN);
      break;
    case ']':
      token_stream_append_token(tokens, SQR_CLOSE);
      break;
    case ':':
      token_stream_append_token(tokens, COLON);
      break;
    case ',':
      token_stream_append_token(tokens, COMMA);
      break;
    case '"': {
      token_stream_append_token(tokens, STRING);
      uint32_t buffsize = 4096;
      char *json_string = zcalloc(1, buffsize);
      c++;
      current_column++;
      if (!parse_json_string_literal(&c, &json_string, &buffsize))
        return false;
      token_stream_append_value(tokens, json_string, NULL, false, 0, STRING);
      zrealloc(json_string, strlen(json_string) + 1);
    } break;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case '+':
    case '-': {
      token_stream_append_token(tokens, NUMBER);
      double number;
      if (!parse_json_number_literal(&c, &number))
        return false;

      token_stream_append_value(tokens, NULL, NULL, false, number, NUMBER);
      continue;

    } break;
    case 'n':
      if (c[1] == 'u' && c[2] == 'l' && c[3] == 'l') {
        token_stream_append_token(tokens, NIL);
        c += 3;
        current_column += 3;
      } else {
        invalid_char();
        return false;
      }
      break;
    case 't':
      if (c[1] == 'r' && c[2] == 'u' && c[3] == 'e') {
        token_stream_append_token(tokens, BOOLEAN);
        c += 3;
        current_column += 3;
      } else {
        invalid_char();
        return false;
      }
      token_stream_append_value(tokens, NULL, NULL, true, 0, BOOLEAN);
      break;
    case 'f':
      if (c[1] == 'a' && c[2] == 'l' && c[3] == 's' && c[4] == 'e') {
        token_stream_append_token(tokens, BOOLEAN);
        c += 4;
        current_column += 4;
      } else {
        invalid_char();
        return false;
      }
      token_stream_append_value(tokens, NULL, NULL, false, 0, BOOLEAN);
      break;
    case '/':
      if (c[1] != '/') {
        current_column++;
        invalid_char();
      }
      while (c[1] != '\n')
        c++;

      break;
    case ' ':
    case '\r':
    case '\n':
    case '\t':
      break;
    default:
      invalid_char();
      return false;
    }
    c++;
    current_column++;
    if ((int)*c == EOF || *c == 0) {
      break;
    }
  }
  return true;
}

static inline json_token_t token_stream_pop(token_stream *tokens) {
  return tokens->tokens_array[tokens->next_token_index++];
}
static inline json_token_t token_stream_peek(token_stream *tokens) {
  return tokens->tokens_array[tokens->next_token_index];
}
static inline union json_token_value
token_stream_pop_value(token_stream *tokens) {
  return tokens->token_values[tokens->next_value_index++];
}

bool state_stack_push(array_t *states, json_token_t token) {
  array_append_int(states, token);
  return true;
}

json_token_t state_stack_peek_n(array_t *states, int n) {
  auto states_length = array_length(states);
  if (array_length(states) < n)
    return 0;
  return states_length ? array_get_int(states, states_length - n) : NO_TOKEN;
}

json_token_t state_stack_peek(array_t *states) {
  return state_stack_peek_n(states, 1);
}

json_token_t state_stack_pop(array_t *states) {
  auto states_length = array_length(states);

  if (!states_length)
    return NO_TOKEN;
  auto state = array_remove_last_int(states);
  return state;
}

void root_stack_push(array_t *root_list, void *root) {
  return array_append_ptr(root_list, root);
}

void *root_stack_pop(array_t *root_list) {
  auto root_list_length = array_length(root_list);

  if (!root_list_length)
    return NULL;
  auto root = array_remove_last_ptr(root_list);
  return root;
}

void key_stack_push(array_t *key_list, char *key) {
  return array_append_str(key_list, key);
}

char *key_stack_pop(array_t *key_list) {
  auto root_list_length = array_length(key_list);

  if (!root_list_length)
    return NULL;
  auto root = array_remove_last_ptr(key_list);
  return root;
}

void list_stack_push(array_t *list_list, void *list) {
  return array_append_ptr(list_list, list);
}

void *list_stack_pop(array_t *list_list) {
  auto list_list_length = array_length(list_list);

  if (!list_list_length)
    return NULL;
  auto list = array_remove_last_ptr(list_list);
  return list;
}

static void insert_json_value(json_token_t previous_token,
                              hash_map *current_root, array_t *current_list,
                              char **key_ptr, token_stream *tokens,
                              void *dict_list, array_t *state_stack,
                              map_value_type type, array_t *key_list) {
  char *key = *key_ptr;
  if (previous_token == COLON) {
    union json_token_value popped_value =
        tokens ? token_stream_pop_value(tokens)
               : (union json_token_value){.null = NULL};
    switch (type) {
    case DICT:
      hash_map_add_dict(current_root, key, dict_list);
      break;
    case LIST:
      hash_map_add_list(current_root, key, dict_list);
      break;
    case TEXT:
      hash_map_add_str(current_root, key, popped_value.string);
      break;
    case POINTER:
    case NULLS:
      hash_map_add_ptr(current_root, key, popped_value.ptr);
      break;
    case INTEGERS:
      hash_map_add_int(current_root, key, popped_value.integer);
      break;
    case FLOATS:
      hash_map_add_number(current_root, key, popped_value.number);
      break;
    case BOOLEANS:
      hash_map_add_bool(current_root, key, popped_value.boolean);
      break;
    default:
      // Optional: handle unexpected type
      break;
    }
    *key_ptr = key_stack_pop(key_list);
    state_stack_pop(state_stack);
  } else {
    union json_token_value popped_value =
        tokens ? token_stream_pop_value(tokens)
               : (union json_token_value){.null = NULL};
    switch (type) {
    case DICT:
      array_append_dict(current_list, dict_list);
      break;
    case LIST:
      array_append_list(current_list, dict_list);
      break;
    case TEXT:
      array_append_str(current_list, popped_value.string);
      break;
    case POINTER:
    case NULLS:
      array_append_ptr(current_list, popped_value.ptr);
      break;
    case INTEGERS:
      array_append_int(current_list, popped_value.integer);
      break;
    case FLOATS:
      array_append_number(current_list, popped_value.number);
      break;
    case BOOLEANS:
      array_append_bool(current_list, popped_value.boolean);
      break;
    default:
      // Optional: handle unexpected type
      break;
    }
  }

  state_stack_push(state_stack, VALUE);
}

bool parse_json_tokens(token_stream *tokens, json_value **root_node) {
  json_token_t current_token;
  array_t *state_stack = create_array();
  array_t *node_stack = create_array();
  array_t *root_list = create_array();
  array_t *list_list = create_array();
  array_t *key_list = create_array();
  hash_map *current_root = NULL;
  array_t *current_list = NULL;
  char *key = NULL;
  void *value;

  json_value *current_value = *root_node = zcalloc(1, sizeof(*current_value));
  json_value *parent_node = 0;
  json_token_t previous_token = NO_TOKEN;

  while ((current_token = token_stream_pop(tokens))) {
    switch (current_token) {
    case CURLY_OPEN: {
      if (tokens->next_token_index > 1 && previous_token != COMMA &&
          previous_token != CURLY_OPEN && previous_token != SQR_OPEN &&
          previous_token != COLON) {
        LOG_ERROR("Unexpected '{' at token position %" PRIu64 ".",
                  tokens->next_token_index);
        return false;
      }

      state_stack_push(state_stack, CURLY_OPEN);

      hash_map *new_root = create_hash_map();
      if (current_value->type == UNKNOWN) {
        current_value->value.ptr = new_root;
        current_value->type = DICT;
      }

      root_stack_push(root_list, current_root);
      root_stack_push(root_list, key);
      current_root = new_root;

    } break;
    case CURLY_CLOSE: {
      previous_token = state_stack_peek(state_stack);

      if (previous_token == VALUE) {
        state_stack_pop(state_stack);
        previous_token = state_stack_peek(state_stack);
      }
      if (previous_token == CURLY_OPEN) {
        state_stack_pop(state_stack);

        hash_map *value = current_root;
        char *value_key = key;
        key = root_stack_pop(root_list);
        current_root = root_stack_pop(root_list);

        previous_token = state_stack_peek(state_stack);

        if (previous_token == COLON || previous_token == SQR_OPEN) {
          insert_json_value(
              previous_token, previous_token == COLON ? current_root : NULL,
              previous_token == SQR_OPEN ? current_list : NULL, &value_key,
              NULL, value, state_stack, DICT, key_list);

          key = value_key;
        }
      } else {
        LOG_ERROR("Unexpected '}' without matching '{'");
        return false;
      }
    } break;
    case STRING: {
      previous_token = state_stack_peek(state_stack);
      if (previous_token == CURLY_OPEN) {
        auto tmp = token_stream_pop_value(tokens);
        key_stack_push(key_list, key);
        key = tmp.string;
      } else if (previous_token == COLON || previous_token == SQR_OPEN) {

        insert_json_value(previous_token, current_root, current_list, &key,
                          tokens, NULL, state_stack, TEXT, key_list);

      } else {
        LOG_ERROR("Unexpected string at position %" PRIu64 ".",
                  tokens->next_token_index);
        return false;
      }

    } break;
    case COLON: {
      previous_token = state_stack_peek(state_stack);

      if (previous_token != CURLY_OPEN) {
        LOG_ERROR("Unexpected ':' outside object context");
        return false;
      }
      previous_token = COLON;
      state_stack_push(state_stack, previous_token);

    } break;
    case COMMA: {
      previous_token = state_stack_peek(state_stack);

      if (previous_token == VALUE) {
        state_stack_pop(state_stack);

        previous_token = state_stack_peek(state_stack);

        switch (previous_token) {
        case CURLY_OPEN: {
          if (token_stream_peek(tokens) == CURLY_CLOSE) {
            LOG_ERROR("Trailing comma in object at position %" PRIu64 ".",
                      tokens->next_token_index);
            return false;
          }
        } break;
        case SQR_OPEN: {
          if (token_stream_peek(tokens) == SQR_CLOSE) {
            LOG_ERROR("Trailing comma in array at position %" PRIu64 ".",
                      tokens->next_token_index);
            return false;
          }

        } break;
        default: {
          fprintf(stderr, "Weird stuff.\n");

          return false;
        }
        }
      } else {
        LOG_ERROR("Unexpected comma at position %" PRIu64 ".",
                  tokens->next_token_index);

        return false;
      }
    } break;
    case SQR_OPEN: {
      if (tokens->next_token_index == 1 || !parent_node) {
      }
      previous_token = state_stack_peek(state_stack);

      if (!(!parent_node && tokens->next_token_index == 1) &&
          previous_token != COLON && previous_token != SQR_OPEN) {
        LOG_ERROR("Unexpected '[' at token position %" PRIu64 ".",
                  tokens->next_token_index);
        return false;
      }

      previous_token = SQR_OPEN;
      state_stack_push(state_stack, previous_token);

      array_t *new_list = create_array();
      if (current_value->type == UNKNOWN) {
        current_value->value.ptr = new_list;
        current_value->type = LIST;
      }

      list_stack_push(list_list, current_list);
      current_list = new_list;

    } break;
    case SQR_CLOSE:

    {
      previous_token = state_stack_peek(state_stack);

      if (previous_token == VALUE) {
        state_stack_pop(state_stack);
        previous_token = state_stack_peek(state_stack);
      }

      if (previous_token == SQR_OPEN) {
        state_stack_pop(state_stack);

        array_t *value = current_list;
        current_list = list_stack_pop(list_list);

        previous_token = state_stack_peek(state_stack);

        if (previous_token == COLON || previous_token == SQR_OPEN) {

          insert_json_value(previous_token,
                            previous_token == COLON ? current_root : NULL,
                            previous_token == SQR_OPEN ? current_list : NULL,
                            &key, NULL, value, state_stack, LIST, key_list);
        }
      } else {
        LOG_ERROR("Unexpected ']' without matching '['");
        return false;
      }
    } break;
    case BOOLEAN: {
      previous_token = state_stack_peek(state_stack);
      if (previous_token != COLON && previous_token != SQR_OPEN) {
        LOG_ERROR("Unexpected boolean");
        return false;
      }

      insert_json_value(previous_token, current_root, current_list, &key,
                        tokens, NULL, state_stack, BOOLEANS, key_list);

    } break;
    case NUMBER: {
      previous_token = state_stack_peek(state_stack);
      if (previous_token != COLON && previous_token != SQR_OPEN) {
        LOG_ERROR("Unexpected number");
        return false;
      }

      insert_json_value(previous_token, current_root, current_list, &key,
                        tokens, NULL, state_stack, FLOATS, key_list);

    } break;
    case NIL: {
      previous_token = state_stack_peek(state_stack);
      if (previous_token != COLON && previous_token != SQR_OPEN) {
        LOG_ERROR("Unexpected null");
        return false;
      }

      insert_json_value(previous_token, current_root, current_list, &key, NULL,
                        NULL, state_stack, NULLS, key_list);

    } break;
    default:
      return false;
    }
  }

  return true;
}

bool tokenize_json_file(FILE *f, token_stream *tokens) {
  current_line = 0;
  current_column = 0;
  char buffer[4096];
  while (fgets(buffer, sizeof(buffer), f)) {
    if (!tokenize_json_string(buffer, tokens))
      return false;
  }
  token_stream_append_token(tokens, NO_TOKEN);
  return true;
}

json_value *json_parse_file(FILE *f) {
  if (!f) {
    LOG_ERROR("Received NULL file pointer");
    return NULL;
  }

  token_stream tokens;
  memset(&tokens, 0, sizeof(tokens));

  if (!tokenize_json_file(f, &tokens)) {
    return NULL;
  }

  if (!tokens.tokens_array) {
    return NULL;
  }

  json_value *value = NULL; // zcalloc(1, sizeof(*value));
  if (!parse_json_tokens(&tokens, &value)) {
    zfree(value);
    return NULL;
  }

  return value;
}

json_value *json_parse_string(char *str) {
  if (!str) {
    LOG_ERROR("Received NULL input string");
    return NULL;
  }

  current_line = 0;
  current_column = 0;
  token_stream tokens;
  memset(&tokens, 0, sizeof(tokens));

  if (!tokenize_json_string(str, &tokens)) {
    return NULL;
  }
  if (!tokens.tokens_array) {
    return NULL;
  }

  json_value *value = NULL; // zcalloc(1, sizeof(*value));
  if (!parse_json_tokens(&tokens, &value)) {
    zfree(value);
    return NULL;
  }

  return value;
}

json_value *json_query(json_value *node, char *key) {
  if (node->type == DICT) {
    return hash_map_get(node->value.ptr, key);
  }
  return NULL;
}

map_value_type json_value_type(json_value *node) { return node->type; }

json_data json_value_data(json_value *node) { return node->value; }
