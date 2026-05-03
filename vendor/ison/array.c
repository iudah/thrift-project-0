#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <zot.h>

#include "ds.h"
#include "ison_data.h"

extern int errno;

struct array {
  map_value *values;
  uint64_t length;
  uint64_t capacity;
};

array_t *create_array() { return zcalloc(1, sizeof(array_t)); }

uint64_t array_length(array_t *array) { return array->length; }

void destroy_array(array_t *array) {
  if (array->capacity > 0)
    zfree(array->values);
  zfree(array);
}

void array_append_xxx(array_t *array, map_value *value) {
  if (array->length == array->capacity) {
    const size_t new_capacity = array->capacity + 8;

    void *tmp = array->values ? zrealloc(array->values,
                                         new_capacity * sizeof(*array->values))
                              : zcalloc(new_capacity, sizeof(*array->values));

    if (!tmp) {
      LOG_ERROR("Memory allocation failed");
      abort();
    }

    array->values = tmp;
    array->capacity = new_capacity;
  }

  memcpy(&array->values[array->length], value, sizeof(*value));
  array->length++;
}

void array_append_number(array_t *array, double value) {
  map_value val = {.value.number = value, .type = FLOATS};
  return array_append_xxx(array, &val);
}

void array_append_bool(array_t *array, bool value) {
  map_value val = {.value.boolean = value, .type = BOOLEANS};
  return array_append_xxx(array, &val);
}

void array_append_ptr(array_t *array, void *value) {
  map_value val = {.value.ptr = value, .type = POINTER};
  return array_append_xxx(array, &val);
}
void array_append_dict(array_t *array, hash_map *value) {
  map_value val = {.value.ptr = value, .type = DICT};
  return array_append_xxx(array, &val);
}
void array_append_list(array_t *array, array_t *value) {
  map_value val = {.value.ptr = value, .type = LIST};
  return array_append_xxx(array, &val);
}
void array_append_str(array_t *array, char *value) {
  map_value val = {.value.ptr = value, .type = TEXT};
  return array_append_xxx(array, &val);
}

void array_append_int(array_t *array, int value) {
  map_value val = {.value.integer = value, .type = INTEGERS};
  return array_append_xxx(array, &val);
}

int array_get_int(array_t *array, int idx) {
  if (idx < 0 || idx >= array->length) {
    LOG_ERROR("Index %d out of bounds (size: %" PRIu64 ")", idx, array->length);
    return 0;
  }
  map_value value = array->values[idx];
  return value.value.integer;
}

map_value *array_get(array_t *array, int idx) {
  if (idx < 0 || idx >= array->length) {
    LOG_ERROR("Index %d out of bounds (size: %" PRIu64 ")", idx, array->length);
    return NULL;
  }

  return &array->values[idx];
}

void *array_get_ptr(array_t *array, int idx) {
  if (idx < 0 || idx >= array->length) {
    LOG_ERROR("Index %d out of bounds (size: %" PRIu64 ")", idx, array->length);
    return NULL;
  }
  map_value value = array->values[idx];
  return (void *)value.value.ptr;
}

void *array_remove_last_ptr(array_t *array) {
  if (array->length == 0) {
    LOG_ERROR("Attempted to remove from empty array");
    return NULL;
  }
  map_value value = array->values[--array->length];
  return (void *)value.value.ptr;
}

int array_remove_last_int(array_t *array) {
  if (array->length == 0) {
    LOG_ERROR("Attempted to remove from empty array");
    return 0;
  }
  map_value value = array->values[--array->length];
  return value.value.integer;
}
