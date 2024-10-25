#include "array.h"
#include <stdlib.h>

array_t *new_array() { return calloc(1, sizeof(array_t)); }
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
  case NUMBER_V:
    array->values[array->length].value.number = value.number;
    break;
  }
  array->values[array->length].value_type = type;
  array->length++;
}

void array_append_number(array_t *array, double value);
void array_append_ptr(array_t *array, void *value) {
  union ds_val val = {.ptr = value};
  return array_append_xxx(array, val, PTR_V);
}
