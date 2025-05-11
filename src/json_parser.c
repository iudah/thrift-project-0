#include "json_parser.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zot.h>

#include "array.h"

struct json_node *parse_json(FILE *f);
bool tokenize(FILE *, tokens_t *);
bool tokenize_string(char *, tokens_t *);
bool extract_string(char **c, char *json_string);
static inline bool append_token(tokens_t *tokens, token_t token);
bool extract_number(char **cc, double *number);
static inline bool append_value(tokens_t *tokens, char *string, bool boolean,
                                double number, token_t token);
void static inline invalid_char();

void fflush_stdout() { fflush(stdout); }

struct json_node *parse_json(FILE *f) {
  if (!f) {
    LOG_ERROR("Received NULL file pointer");
    return NULL;
  }

  json_node_t *node = zcalloc(1, sizeof(*node));
  tokens_t tokens;
  memset(&tokens, 0, sizeof(tokens));

  if (!tokenize(f, &tokens)) {
    zfree(node);
    return NULL;
  }
  if (!tokens.tokens) {
    zfree(node);
    return NULL;
  }
  if (!parse(&tokens, node)) {
    zfree(node);
    return NULL;
  }

  return node;
}

struct json_node *parse_json_string(char *str) {
  if (!str) {
    LOG_ERROR("Received NULL input string");
    return NULL;
  }

  json_node_t *node = zcalloc(1, sizeof(*node));
  tokens_t tokens;
  memset(&tokens, 0, sizeof(tokens));

  if (!tokenize_string(str, &tokens)) {
    zfree(node);
    return NULL;
  }
  if (!tokens.tokens) {
    zfree(node);
    return NULL;
  }
  if (!parse(&tokens, node)) {
    zfree(node);
    return NULL;
  }

  return node;
}

static inline token_t token_pop(tokens_t *tokens) {
  return tokens->tokens[tokens->tok_front++];
}
static inline token_t token_peek(tokens_t *tokens) {
  return tokens->tokens[tokens->tok_front];
}
static inline union token_value token_value_pop(tokens_t *tokens) {
  return tokens->values[tokens->val_front++];
}
bool push_state(array_t *states, token_t token) {
  array_append_int(states, token);
  return true;
}
token_t peek_n_state(array_t *states, int n) {
  if (states->length < n)
    return 0;
  return states->length ? array_get_int(states, states->length - n) : NO_TOKEN;
}
token_t peek_state(array_t *states) { return peek_n_state(states, 1); }
token_t pop_state(array_t *states) {
  if (!states->length)
    return NO_TOKEN;
  auto state = array_remove_last_int(states);
  return state;
}
bool push_node(array_t *nodes, json_node_t *node) {
  array_append_ptr(nodes, node);
  return true;
}
json_node_t *pop_node(array_t *nodes) {
  if (!nodes->length)
    return NULL;
  auto state = array_remove_last_ptr(nodes);
  return state;
}
void static inline value_error(char *c) {
  fprintf(stderr, "Error: Expected %s.", c);
}

bool static inline append_json_array_value(json_node_t *parent_node,
                                           json_node_t *current_node) {
  if (parent_node == current_node) {
    printf("That is weird?");
    return false;
  }
  switch (current_node->value.value_type) {
  case NUMBER:
    array_append_number(parent_node->value.value.array,
                        current_node->value.value.number);
    break;
  case BOOLEAN:
    array_append_bool(parent_node->value.value.array,
                      current_node->value.value.boolean);
    break;
  case OBJECT:
    array_append_ptr(parent_node->value.value.array,
                     current_node->value.value.node);
    break;
  case ARRAY:
    array_append_ptr(parent_node->value.value.array,
                     current_node->value.value.array);
    break;
  case NIL:
    array_append_ptr(parent_node->value.value.array, NULL);
    break;
  case STRING:
    array_append_ptr(parent_node->value.value.array,
                     current_node->value.value.string);
    break;
  default:
    LOG_ERROR("Unsupported array value type: %d",
              current_node->value.value_type);
    return false;
  }
  return true;
}
const char *token_to_string(token_t token) {
  switch (token) {
  case NO_TOKEN:
    return "NO_TOKEN";
  case CURLY_OPEN:
    return "CURLY_OPEN";
  case CURLY_CLOSE:
    return "CURLY_CLOSE";
  case SQR_OPEN:
    return "SQR_OPEN";
  case SQR_CLOSE:
    return "SQR_CLOSE";
  case COMMA:
    return "COMMA";
  case STRING:
    return "STRING";
  case COLON:
    return "COLON";
  case NUMBER:
    return "NUMBER";
  case OBJECT:
    return "OBJECT";
  case ARRAY:
    return "ARRAY";
  case BOOLEAN:
    return "BOOLEAN";
  case NIL:
    return "NIL";
  case VALUE:
    return "VALUE";
  default:
    return "UNKNOWN_TOKEN";
  }
}

void print_token(token_t token) { puts(token_to_string(token)); }

bool parse(tokens_t *tokens, json_node_t *root_node) {
  token_t current_token;
  array_t *state_stack = new_array();
  array_t *node_stack = new_array();
  json_node_t *current_node = root_node;
  json_node_t *parent_node = 0;
  token_t previous_token = NO_TOKEN;

  while ((current_token = token_pop(tokens))) {
    switch (current_token) {
    case CURLY_OPEN: {
      if (tokens->tok_front > 1 && previous_token != COMMA &&
          previous_token != CURLY_OPEN && previous_token != SQR_OPEN &&
          previous_token != COLON) {
        LOG_ERROR("Unexpected '{' at token position %" PRIu64 ".",
                  tokens->tok_front);
        return false;
      }

      json_node_t *node = current_node->value.value.node =
          zcalloc(1, sizeof(*node));
      current_node->value.value_type = OBJECT;

      push_state(state_stack, CURLY_OPEN);

      push_node(node_stack, parent_node);

      parent_node = current_node;
      current_node = node;

    } break;
    case CURLY_CLOSE: {
      previous_token = peek_state(state_stack);
      if (previous_token == VALUE) {
        pop_state(state_stack);
        previous_token = peek_state(state_stack);
      }
      if (previous_token == CURLY_OPEN) {
        pop_state(state_stack);

        current_node = parent_node;
        parent_node = pop_node(node_stack);

        previous_token = peek_state(state_stack);

        if (previous_token == COLON || previous_token == SQR_OPEN) {
          if (previous_token == COLON)
            pop_state(state_stack);

          previous_token = VALUE;
          push_state(state_stack, previous_token);
        }
      } else {
        LOG_ERROR("Unexpected '}' without matching '{'");
        return false;
      }

    } break;
    case STRING: {
      previous_token = peek_state(state_stack);
      if (previous_token == CURLY_OPEN) {
        auto tmp = token_value_pop(tokens);
        current_node->key = tmp.string;
      } else if (previous_token == COLON || previous_token == SQR_OPEN) {
        if (previous_token == COLON)
          pop_state(state_stack);
        current_node->value.value.string = token_value_pop(tokens).string;

        current_node->value.value_type = STRING;
        push_state(state_stack, VALUE);

      } else {
        LOG_ERROR("Unexpected string at position %" PRIu64 ".",
                  tokens->tok_front);
        return false;
      }

    } break;
    case COLON: {
      previous_token = peek_state(state_stack);

      if (previous_token != CURLY_OPEN) {
        LOG_ERROR("Unexpected ':' outside object context");
        return false;
      }
      previous_token = COLON;
      push_state(state_stack, previous_token);

    } break;
    case COMMA: {
      previous_token = peek_state(state_stack);

      if (previous_token == VALUE) {
        pop_state(state_stack);

        previous_token = peek_state(state_stack);

        switch (previous_token) {
        case CURLY_OPEN: {
          if (token_peek(tokens) == CURLY_CLOSE) {
            LOG_ERROR("Trailing comma in object at position %" PRIu64 ".",
                      tokens->tok_front);
            return false;
          }
          json_node_t *node = zcalloc(1, sizeof(*node));

          current_node->next_pair = node;
          current_node = node;
        } break;
        case SQR_OPEN: {
          if (token_peek(tokens) == SQR_CLOSE) {
            LOG_ERROR("Trailing comma in array at position %" PRIu64 ".",
                      tokens->tok_front);
            return false;
          }

          if (!append_json_array_value(parent_node, current_node))
            return false;
        } break;
        default: {
          fprintf(stderr, "Weird stuff.\n");
          print_token(previous_token);

          return false;
        }
        }
      } else {
        LOG_ERROR("Unexpected comma at position %" PRIu64 ".",
                  tokens->tok_front);

        print_token(previous_token);
        return false;
      }
    } break;
    case SQR_OPEN: {
      if (tokens->tok_front == 1 || !parent_node) {
      }
      previous_token = peek_state(state_stack);

      if (!(!parent_node && tokens->tok_front == 1) &&
          previous_token != COLON && previous_token != SQR_OPEN) {
        LOG_ERROR("Unexpected '[' at token position %" PRIu64 ".",
                  tokens->tok_front);
        return false;
      }
      json_node_t *node = zcalloc(1, sizeof(*node));

      current_node->value.value.array = new_array();
      current_node->value.value_type = ARRAY;

      push_node(node_stack, parent_node);
      parent_node = current_node;
      current_node = node;

      previous_token = SQR_OPEN;
      push_state(state_stack, previous_token);

    } break;
    case SQR_CLOSE: {
      previous_token = peek_state(state_stack);

      if (previous_token != SQR_OPEN && previous_token != VALUE) {
        LOG_ERROR("Unexpected ']' without matching '['");
        return false;
      }

      if (previous_token == VALUE)
        pop_state(state_stack);

      if (current_node->value.value_type != NO_TOKEN) {
        if (!append_json_array_value(parent_node, current_node))
          return false;
      } else {
        destroy_array(parent_node->value.value.array);
        parent_node->value.value.array = NULL;
      }
      zfree(current_node);
      current_node = parent_node;
      parent_node = pop_node(node_stack);

      pop_state(state_stack);

      previous_token = peek_state(state_stack);

      if (previous_token == COLON) {
        pop_state(state_stack);
        previous_token = VALUE;
        push_state(state_stack, previous_token);
      }

    } break;
    case BOOLEAN: {
      previous_token = peek_state(state_stack);
      if (previous_token != COLON && previous_token != SQR_OPEN) {
        LOG_ERROR("Unexpected boolean");
        return false;
      }

      if (previous_token == COLON)
        pop_state(state_stack);

      current_node->value.value.boolean = token_value_pop(tokens).boolean;
      current_node->value.value_type = BOOLEAN;
      push_state(state_stack, VALUE);

    } break;
    case NUMBER: {
      previous_token = peek_state(state_stack);
      if (previous_token != COLON && previous_token != SQR_OPEN) {
        LOG_ERROR("Unexpected number");
        return false;
      }

      if (previous_token == COLON)
        pop_state(state_stack);

      current_node->value.value.number = token_value_pop(tokens).number;
      current_node->value.value_type = NUMBER;
      push_state(state_stack, VALUE);

    } break;
    case NIL: {
      previous_token = peek_state(state_stack);
      if (previous_token != COLON && previous_token != SQR_OPEN) {
        LOG_ERROR("Unexpected null");
        return false;
      }

      if (previous_token == COLON)
        pop_state(state_stack);

      current_node->value.value.null = NULL;
      current_node->value.value_type = NIL;
      push_state(state_stack, VALUE);

    } break;
    default:
      printf("Unused token %s\n", token_to_string(current_token));
      return false;
    }
  }

  return true;
}

int line = 0;
int column = 0;

bool tokenize_string(char *string, tokens_t *tokens) {
  line++;
  column = 0;
  char *c = string;
  while (true) {
    switch (*c) {
    case '{':
      append_token(tokens, CURLY_OPEN);
      break;
    case '}':
      append_token(tokens, CURLY_CLOSE);
      break;
    case '[':
      append_token(tokens, SQR_OPEN);
      break;
    case ']':
      append_token(tokens, SQR_CLOSE);
      break;
    case ':':
      append_token(tokens, COLON);
      break;
    case ',':
      append_token(tokens, COMMA);
      break;
    case '"': {
      append_token(tokens, STRING);
      char *json_string = zcalloc(1, 4096);
      c++;
      column++;
      if (!extract_string(&c, json_string))
        return false;
      append_value(tokens, json_string, false, 0, STRING);
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
      append_token(tokens, NUMBER);
      double number;
      if (!extract_number(&c, &number))
        return false;

      append_value(tokens, NULL, false, number, NUMBER);
      continue;

    } break;
    case 'n':
      if (c[1] == 'u' && c[2] == 'l' && c[3] == 'l') {
        append_token(tokens, NIL);
        c += 3;
        column += 3;
      } else {
        invalid_char();
        return false;
      }
      break;
    case 't':
      if (c[1] == 'r' && c[2] == 'u' && c[3] == 'e') {
        append_token(tokens, BOOLEAN);
        c += 3;
        column += 3;
      } else {
        invalid_char();
        return false;
      }
      append_value(tokens, NULL, true, 0, BOOLEAN);
      break;
    case 'f':
      if (c[1] == 'a' && c[2] == 'l' && c[3] == 's' && c[4] == 'e') {
        append_token(tokens, BOOLEAN);
        c += 4;
        column += 4;
      } else {
        invalid_char();
        return false;
      }
      append_value(tokens, NULL, false, 0, BOOLEAN);
      break;
    case '/':
      if (c[1] != '/') {
        column++;
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
    column++;
    if ((int)*c == EOF || *c == 0) {
      break;
    }
  }
  return true;
}

bool tokenize(FILE *f, tokens_t *tokens) {
  char buffer[4096];
  while (fgets(buffer, sizeof(buffer), f)) {
    if (!tokenize_string(buffer, tokens))
      return false;
  }
  append_token(tokens, NO_TOKEN);
  return true;
}

void static inline invalid_char() {
  LOG_ERROR("Invalid character at line %d column %d.", line, column);
}

static inline bool append_value(tokens_t *tokens, char *string, bool boolean,
                                double number, token_t token) {
  if (!tokens->values)
    tokens->values = zcalloc(tokens->val_limit = 32, sizeof(*tokens->values));
  else if (tokens->val_rear >= tokens->val_limit) {
    tokens->val_limit *= 2;
    void *tmp =
        realloc(tokens->values, tokens->val_limit * sizeof(*tokens->values));
    if (!tmp) {
      fprintf(stderr, "Failed to allocate memory for values.\n");
      return false;
    }
    tokens->values = tmp;
  }
  switch (token) {
  case STRING:
    tokens->values[tokens->val_rear++].string = string;
    break;
  case BOOLEAN:
    tokens->values[tokens->val_rear++].boolean = boolean;
    break;
  case NUMBER:
    tokens->values[tokens->val_rear++].number = number;
    break;
  default:
    break;
  }
  return true;
}

void static inline invalid_number() {
  LOG_ERROR("Invalid number format at line %d column %d.", line, column);
}

bool extract_number(char **cc, double *number) {
  char number_string[4096];
  char *string = number_string;
  char *c = *cc;
  bool zero_is_allowed = true;
  bool is_first_digit = true;
  bool sign_is_allowed = true;
  bool decimal_is_allowed = false;
  bool decimal_is_present = false;
  bool exp_is_allowed = false;
  bool exp_not_closed = false;
  bool exp_is_present = false;
  while (true) {
    switch (*c) {
    case '0': {
      if (!zero_is_allowed) {
        invalid_number();
        return false;
      }
      if (is_first_digit)
        zero_is_allowed = false;

    } // fall-through to allow extra formating

    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      if (!decimal_is_present)
        decimal_is_allowed = true;
      is_first_digit = false;
      sign_is_allowed = false;
      if (!exp_is_present)
        exp_is_allowed = true;
      if (exp_is_present && exp_not_closed) {
        exp_not_closed = false;
        zero_is_allowed = true;
      }
      *string = *c;
      string++;
      break;
    case '+':
    case '-':
      if (!sign_is_allowed) {
        invalid_number();
        return false;
      }
      sign_is_allowed = false;
      *string = *c;
      string++;
      break;
    case '.': {
      if (!decimal_is_allowed) {
        invalid_number();
        return false;
      }
      decimal_is_allowed = false;
      decimal_is_present = true;
      zero_is_allowed = true;
      *string = *c;
      string++;
    } break;
    case 'E':
    case 'e': {
      if (!exp_is_allowed || exp_is_present || is_first_digit) {
        invalid_number();
        return false;
      }
      decimal_is_allowed = false;
      sign_is_allowed = true;
      zero_is_allowed = false;
      is_first_digit = true;
      exp_is_allowed = false;
      exp_not_closed = true;
      exp_is_present = true;
      *string = 'e';
      string++;
    } break;
    case ' ':
    case '\r':
    case '\n':
    case '\b':
    case '\f':
    case '\t':
    case ',':
    case ']':
    case '}':
    default:
      if (exp_not_closed) {
        invalid_number();
        return false;
      };
      *cc = c;
      *string = 0;
      *number = strtod(number_string, NULL);
      return true;
      break;
    }
    c++;
    column++;
    if (*c == '\n')
      break;
  }

  *cc = c;
  *string = 0;
  *number = strtod(number_string, NULL);
  return true;
}

bool extract_string(char **cc, char *json_string) {
  char *string = json_string;
  char *c = *cc;
  while (true) {
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
        column += 4;

        break;
      default:
        LOG_ERROR("Invalid escape sequence \\%c at line %d column %d.", *c,
                  line, column);
        return false;
      }
      *string = escape;
      string++;
    }; break;
    case '"':
      *cc = c;
      return true;
      break;
    default:
      *string = *c;
      string++;
    }
    c++;
    if (*c == '"')
      break;
  }
  *cc = c;
  return true;
}

static inline bool append_token(tokens_t *tokens, token_t token) {
  if (!tokens->tokens)
    tokens->tokens = zcalloc(tokens->tok_limit = 32, sizeof(*tokens->tokens));
  else if (tokens->tok_rear >= tokens->tok_limit) {
    tokens->tok_limit *= 2;
    void *tmp =
        realloc(tokens->tokens, tokens->tok_limit * sizeof(*tokens->tokens));
    if (!tmp) {
      LOG_ERROR("Failed to allocate %" PRIu64 " bytes for tokens: %s",
                tokens->tok_limit * sizeof(*tokens->tokens), strerror(errno));
      return false;
    }
    tokens->tokens = tmp;
  }
  tokens->tokens[tokens->tok_rear++] = token;
  return true;
}

json_value_t *json_get_value(json_node_t *json_node, char *key_string) {
  if (!key_string || !json_node->key) {
    if (!key_string && json_node->key || key_string && json_node->key) {
      // LOG_WARN("Attempted to get value from NULL node");
      return NULL;
    } else
      return &json_node->value;
  }

  uint64_t len = strlen(key_string);

  while (json_node) {
    if (json_node->key && !strncmp(json_node->key, key_string, len))
      return &json_node->value;
    json_node = json_node->next_pair;
  }

  return NULL;
}

json_iter_t *json_iter(json_node_t *json_node) {
  json_iter_t *iter_state = zcalloc(1, sizeof(*iter_state));
  iter_state->current = json_node;
  return iter_state;
}

int json_next(json_iter_t *state, char **key, json_value_t **val) {
  json_node_t *current = state->current;
  if (!current)
    return -1;
  *key = current->key;
  *val = &current->value;
  state->current = current->next_pair;

  return 0;
}
