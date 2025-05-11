#include "array.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <zot.h>

#include "ds.h"

extern int errno;

array_t *new_array() { return zcalloc(1, sizeof(array_t)); }

void destroy_array(array_t *array) {
  if (array->capacity > 0)
    zfree(array->values);
  zfree(array);
}

void array_append_xxx(array_t *array, union ds_val value,
                      ds_value_enum_t type) {
  if (array->length == array->capacity) {
    const size_t new_capacity = array->capacity + 8;
    void *tmp = realloc(array->values, new_capacity * sizeof(*array->values));

    if (!tmp) {
      LOG_ERROR("Memory allocation failed: %s", strerror(errno));
      abort();
    }

    array->values = tmp;
    array->capacity = new_capacity;
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
  default:
    LOG_ERROR("Invalid value type: %d", type);
    abort();
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
  if (idx < 0 || idx >= array->length) {
    LOG_ERROR("Index %d out of bounds (size: %" PRIu64 ")", idx, array->length);
    return 0;
  }
  ds_value_t value = array->values[idx];
  return value.value.d;
}

void *array_get_ptr(array_t *array, int idx) {
  if (idx < 0 || idx >= array->length) {
    LOG_ERROR("Index %d out of bounds (size: %" PRIu64 ")", idx, array->length);
    return NULL;
  }
  ds_value_t value = array->values[idx];
  return value.value.ptr;
}

void *array_remove_last_ptr(array_t *array) {
  if (array->length == 0) {
    LOG_ERROR("Attempted to remove from empty array");
    return NULL;
  }
  ds_value_t value = array->values[--array->length];
  return value.value.ptr;
}

int array_remove_last_int(array_t *array) {
  if (array->length == 0) {
    LOG_ERROR("Attempted to remove from empty array");
    return 0;
  }
  ds_value_t value = array->values[--array->length];
  return value.value.d;
}
