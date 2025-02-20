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
void destroy_array(array_t *array);
void array_append_bool(array_t *array, bool value);
void array_append_number(array_t *array, double value);
void array_append_ptr(array_t *array, void *value);
void array_append_int(array_t *array, int i);
int array_get_int(array_t *array, int idx);
void *array_get_ptr(array_t *array, int idx);
void *array_remove_last_ptr(array_t *array);
int array_remove_last_int(array_t *array);

#endif