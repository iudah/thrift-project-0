#include "array.h"
#include "ds.h"
#include <stdlib.h>

array_t *new_array() { return calloc(1, sizeof(array_t)); }
void destroy_array(array_t *array) {
  if (array->capacity > 0)
    free(array->values);
  free(array);
  return;
}
void array_append_xxx(array_t *array, union ds_val value,
                      ds_value_enum_t type) {
  if (array->length == array->capacity) {
    array->capacity += 8;
    if (!array->values) {
      array->values = malloc(array->capacity * sizeof(*array->values));
    } else {
      void *tmp =
          realloc(array->values, array->capacity * sizeof(*array->values));

      if (!tmp) {
        perror("OOM");
        abort();
      }

      array->values = tmp;
    }
  }
  switch (type) {
  case PTR_V:
    array->values[array->length].value.ptr = value.ptr;
    break;
  case INT_V:
    array->values[array->length].value.d = value.d;
    break;
  case NUMBER_V:
    array->values[array->length].value.number = value.number;
    break;
  case BOOLEAN_V:
    array->values[array->length].value.boolean = value.boolean;
    break;
  }
  array->values[array->length].value_type = type;
  array->length++;
}

void array_append_number(array_t *array, double value) {
  union ds_val val = {.number = value};
  return array_append_xxx(array, val, NUMBER_V);
}

void array_append_bool(array_t *array, bool value) {
  union ds_val val = {.boolean = value};
  return array_append_xxx(array, val, BOOLEAN_V);
}

void array_append_ptr(array_t *array, void *value) {
  union ds_val val = {.ptr = value};
  return array_append_xxx(array, val, PTR_V);
}

void array_append_int(array_t *array, int value) {
  union ds_val val = {.d = value};
  return array_append_xxx(array, val, INT_V);
}
int array_get_int(array_t *array, int idx) {
  auto value = array->values[idx];
  // if
  return value.value.d;
}
void *array_get_ptr(array_t *array, int idx) {
  auto value = array->values[idx];
  // if
  return value.value.ptr;
}
void *array_remove_last_ptr(array_t *array) {
  if (!array->length) {
    fprintf(stderr, "Error: Array limit reached.\n");
    return 0;
  }
  auto value = array->values[--array->length];
  return value.value.ptr;
}
int array_remove_last_int(array_t *array) {
  if (!array->length) {
    fprintf(stderr, "Error: Array limit reached.\n");
    return 0;
  }
  auto value = array->values[--array->length];
  return value.value.d;
}
