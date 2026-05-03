#ifndef ISON_DATA_H
#define ISON_DATA_H

#include "ison.h"
#include <inttypes.h>

struct map_value {
  json_data value;
  map_value_type type;
};

#endif