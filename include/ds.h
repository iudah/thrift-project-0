#ifndef DS_H
#define DS_H

#include <stdint.h>

typedef enum { PTR_V, NUMBER_V } ds_value_enum_t;

union ds_val {
  void *ptr;
  double number;
};

typedef struct ds_value {
  union ds_val value;
  ds_value_enum_t value_type;
} ds_value_t;

uint64_t get_length(void *ds);
void *trim_ds(void *ds);

#endif