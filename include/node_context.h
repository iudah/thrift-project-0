#ifndef NODE_CONTEXT_H
#define NODE_CONTEXT_H

#include "book.h"
#include "ison.h"

typedef struct {
  book_t *book;
  array_t *peers;
} node_context_t;

#endif
