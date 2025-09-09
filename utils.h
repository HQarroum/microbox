#ifndef UTILS_H
#define UTILS_H

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>

/**
 * Utility macro to check the result of a system call.
 */
#define CHECK(call) \
    do { \
        if ((call) < 0) { \
            fprintf(stderr, "%s failed: %s\n", #call, strerror(errno)); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

/**
 * Utility macro to calculate the size of an array.
 */
#define SIZE_OF(x) (sizeof(x) / sizeof(*(x)))

/**
 * Allocate memory using calloc and exit on failure.
 * @param __count The number of elements to allocate.
 * @param __size The size of each element.
 * @return A pointer to the allocated memory.
 */
void *xcalloc(size_t __count, size_t __size);

/**
 * Allocate memory for a string.
 * @param __str The string to allocate memory for.
 * @return A pointer to the allocated memory.
 */
char *xstrdup(const char *__str);

/**
 * Allocate memory for a string with a specified size.
 * @param __str The string to allocate memory for.
 * @param __size The size of the string.
 * @return A pointer to the allocated memory.
 */
char *xstrndup(const char *__str, size_t __size);

/**
 * Reallocate memory for a string.
 * @param __ptr The string to reallocate memory for.
 * @param __size The new size of the string.
 * @return A pointer to the reallocated memory.
 */
void* xrealloc(void* __ptr, size_t __size);

/**
 * Verifies access to a path.
 * @param path The path to verify access for.
 * @param mode The access mode to verify.
 * @return The result of the access operation.
 */
int xaccess(const char *path, int mode);

/**
 * Print an error message and exit.
 * @param fmt The format string.
 * @param ... The arguments to the format string.
 */
void die(const char *fmt, ...);

/**
 * Checks if a path is a directory.
 * @param path The path to check.
 * @return 1 if the path is a directory, 0 otherwise.
 */
int is_directory(const char* path);

/**
 * Creates a directory safely, ignoring existing directories.
 * @param path The path to create.
 * @param mode The mode to create the directory with.
 * @return 0 on success, -1 on failure.
 */
int mkdir_safe(const char *path, mode_t mode);

/**
 * Creates a directory and its parent directories.
 * @param path The path to create.
 * @return 0 on success, -1 on failure.
 */
int mkdirp(const char *path);

/**
 * Writes data to a file.
 * @param path The path to the file.
 * @param data The data to write.
 * @return 0 on success, -1 on failure.
 */
int write_file(const char *path, const char *data);

#endif /* UTILS_H */
