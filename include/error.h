#ifndef ERROR_H
#define ERROR_H

#include <stdio.h>

#define LOG(fmt, ...) fprintf(stderr, "[LOG]: " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(msg, ...) \
  fprintf(stderr, "[ERROR]: %s: " msg "\n", __FUNCTION__, ##__VA_ARGS__)

#endif