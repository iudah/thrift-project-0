#include <mem.h>
#include <stdlib.h>
#include <string.h>
#include <zot.h>

typedef uint64_t zsize;

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define Z_USE_ASAN 1
#endif
#endif

void *zmalloc(const zsize size) {
#if Z_USE_ASAN
  return malloc(size);
#else
  return allocate(size);
#endif
}

void *zcalloc(const zsize count, const zsize unit_size) {
#if Z_USE_ASAN
  return calloc(count, unit_size);
#else
  if (!count || !unit_size)
    return NULL;

  uint64_t size = count * unit_size;
  /* check mul overflow */
  if (unit_size != size / count)
    return NULL;

  void *ptr = zmalloc(size);
  if (!ptr)
    return NULL;

  // Paginate loop because a single memset triggers UBSan on Android
  uintptr_t mem = (uintptr_t)ptr;
#define MEMSET_LIMIT 65535
  uint32_t page = 0;
  for (; (page + MEMSET_LIMIT) < size; page += MEMSET_LIMIT) {
    memset((void *)(mem + page), 0, MEMSET_LIMIT);
  }
  memset((void *)(mem + page), 0, size - page);

  return ptr;
#endif
}

void *zrealloc(void *ptr, const zsize size) {
#if Z_USE_ASAN
  return realloc(ptr, size);
#else
  return reallocate(ptr, size);
#endif
}

bool zfree(void *ptr) {
#if Z_USE_ASAN
  free(ptr);
  return true;
#else
  return claim(ptr);
#endif
}

int zthread_create(pthread_t *_Nonnull thread_ptr,
                   pthread_attr_t const *_Nullable thread_attr,
                   void *_Nonnull (*_Nonnull start_routine)(void *_Nonnull),
                   void *_Nullable input) {
  return create_thread(thread_ptr, thread_attr, start_routine, input);
}

char *zstrdup(const char *s) {
  uint32_t s_length = strlen(s) + 1;
  char *str = zmalloc(s_length);
  memcpy(str, s, s_length);
  return str;
}

static void __attribute__((constructor(102))) on_load() {}
