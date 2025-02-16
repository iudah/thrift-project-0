#ifndef ARRAY_H
#define ARRAY_H

#include "ds.h"
#include <stdint.h>

typedef struct array {
  ds_value_t *values;
  uint64_t length;
  uint64_t capacity;
} array_t;

array_t *new_array();
void array_append_number(array_t *array, double value);
void array_append_ptr(array_t *array, void *value);

#endif