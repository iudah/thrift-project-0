#ifndef ZOT_H
#define ZOT_H

#ifndef __clang__
#define _Nonnull
#define _Nullable
#define constructor(x) __constructor__
#endif

#if defined(__ANDROID__)
#include <android/log.h>
#include <errno.h>

#if !defined(TAG)
#define TAG "zot"
#endif
#endif

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file zot.h
 * @brief Custom memory management and threading utilities.
 */

/**
 * @typedef zsize
 * @brief Type definition for object sizes.
 */
typedef uint64_t zsize;

/**
 * @brief Allocates zero-initialized memory for an array of objects.
 *
 * @param n Number of objects to allocate.
 * @param size Size of each object in bytes.
 * @return Pointer to the allocated zero-initialized memory, or NULL on failure.
 */
void *_Nullable zcalloc(const zsize n, const zsize size);

/**
 * @brief Allocates memory for an object without initializing it.
 *
 * @param size Size of memory to allocate in bytes.
 * @return Pointer to the allocated memory, or NULL on failure.
 */
void *_Nullable zmalloc(zsize size);

/**
 * @brief Reallocates memory to a new size.
 *
 * @param mptr Pointer to the previously allocated memory block.
 * @param size New size for the memory block in bytes.
 * @return Pointer to the reallocated memory, or NULL on failure.
 */
void *_Nullable zrealloc(void *_Nonnull mptr, zsize size);

/**
 * @brief Frees memory allocated by zmalloc, zcalloc, or zrealloc.
 *
 * @param ptr Pointer to the memory to be freed.
 * @return true if the memory was successfully freed, false otherwise.
 */
bool zfree(void *_Nonnull ptr);

/**
 * @brief Creates a new thread.
 *
 * @param thread_ptr Pointer to a pthread_t variable where the thread ID will be
 * stored.
 * @param thread_attr Optional pointer to thread attributes (can be NULL).
 * @param start_routine Pointer to the function to execute in the new thread.
 * @param input Pointer to input data for the thread function (can be NULL).
 * @return 0 on success, or a non-zero error code on failure.
 */
int zthread_create(pthread_t *_Nonnull thread_ptr,
                   pthread_attr_t const *_Nullable thread_attr,
                   void *_Nonnull (*_Nonnull start_routine)(void *_Nonnull),
                   void *_Nullable input);

/**
 * @brief Logs a message to stderr.
 *
 * @param fmt Format string (like printf).
 * @param ... Additional arguments for the format string.
 */
#if defined(__ANDROID__) && defined(USE_ANDROID_LOG)
#define LOG(fmt, ...)                                                          \
  __android_log_print(ANDROID_LOG_INFO, TAG, fmt, ##__VA_ARGS__)
#else
#define LOG(fmt, ...) fprintf(stderr, "[LOG]: " fmt "\n", ##__VA_ARGS__)
#endif
/**
 * @brief Logs an error message to stderr and aborts the program.
 *
 * @param msg Error message format string.
 * @param ... Additional arguments for the format string.
 */
#if defined(__ANDROID__) && defined(USE_ANDROID_LOG)
#define LOG_ERROR(fmt, ...)                                                    \
  __android_log_print(ANDROID_LOG_ERROR, TAG, fmt, ##__VA_ARGS__)
#else
#define LOG_ERROR(msg, ...)                                                    \
  fprintf(stderr, "[ERROR]: %s: " msg "\n", __FUNCTION__, ##__VA_ARGS__);      \
  abort();
#endif

/**
 * @brief Duplicates a string.
 *
 * @param str Pointer to the null-terminated string to duplicate.
 * @return Pointer to the newly allocated duplicate string, or NULL on
 * failure.
 */
char *zstrdup(const char *str);

#ifdef __cplusplus
}
#endif

#endif
