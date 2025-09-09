#include "utils.h"

/**
 * Allocate memory using calloc and exit on failure.
 * @param __count The number of elements to allocate.
 * @param __size The size of each element.
 * @return A pointer to the allocated memory.
 */
void *xcalloc(size_t __count, size_t __size) {
  void *ptr = calloc(__count, __size);
  if (!ptr) {
    fprintf(stderr, "could not allocate memory\n");
    exit(EXIT_FAILURE);
  }
  return (ptr);
}

/**
 * Allocate memory for a string.
 * @param __str The string to allocate memory for.
 * @return A pointer to the allocated memory.
 */
char *xstrdup(const char *__str) {
  char *ptr = strdup(__str);
  if (!ptr) {
    fprintf(stderr, "could not allocate memory\n");
    exit(EXIT_FAILURE);
  }
  return (ptr);
}

/**
 * Allocate memory for a string with a specified size.
 * @param __str The string to allocate memory for.
 * @param __size The size of the string.
 * @return A pointer to the allocated memory.
 */
char *xstrndup(const char *__str, size_t __size) {
  char *ptr = strndup(__str, __size);
  if (!ptr) {
    fprintf(stderr, "could not allocate memory\n");
    exit(EXIT_FAILURE);
  }
  return (ptr);
}

/**
 * Reallocate memory for a string.
 * @param __ptr The string to reallocate memory for.
 * @param __size The new size of the string.
 * @return A pointer to the reallocated memory.
 */
void* xrealloc(void* __ptr, size_t __size) {
  void *ptr = realloc(__ptr, __size);
  if (!ptr) {
    fprintf(stderr, "could not allocate memory\n");
    exit(EXIT_FAILURE);
  }
  return (ptr);
}

/**
 * Print an error message and exit.
 * @param fmt The format string.
 * @param ... The arguments to the format string.
 */
void die(const char *fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, "\n");
  exit(EXIT_FAILURE);
}

/**
 * Checks if a path is a directory.
 * @param path The path to check.
 * @return 1 if the path is a directory, 0 otherwise.
 */
int is_directory(const char* path) {
  struct stat st;

  // Ensure the path is valid.
  if (!path || strlen(path) == 0) {
    errno = EINVAL;
    return (0);
  }

  // Verify that the path exists and is accessible.
  if (access(path, F_OK) < 0) {
    return (0);
  }

  // Ensure the path is a directory.
  if (stat(path, &st) != 0) {
    return (0);
  }

  return (S_ISDIR(st.st_mode));
}

/**
 * Creates a directory safely, ignoring existing directories.
 * @param path The path to create.
 * @param mode The mode to create the directory with.
 * @return 0 on success, -1 on failure.
 */
int mkdir_safe(const char *path, mode_t mode) {
  if (mkdir(path, mode) == -1 && errno != EEXIST) {
    return (-1);
  }
  return (0);
}

static int maybe_mkdir(const char* path, mode_t mode)
{
    struct stat st;
    errno = 0;

    /* Try to make the directory */
    if (mkdir(path, mode) == 0)
        return 0;

    /* If it fails for any reason but EEXIST, fail */
    if (errno != EEXIST)
        return -1;

    /* Check if the existing path is a directory */
    if (stat(path, &st) != 0)
        return -1;

    /* If not, fail with ENOTDIR */
    if (!S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }

    errno = 0;
    return 0;
}

/**
 * Creates a directory and its parent directories.
 * @param path The path to create.
 * @return 0 on success, -1 on failure.
 */
int mkdirp(const char *path) {
  char *_path = NULL;
  char *p;
  int result = -1;
  mode_t mode = 0777;
  errno = 0;

  /* Copy string so it's mutable */
  _path = strdup(path);
  if (_path == NULL)
    goto out;

  /* Iterate the string */
  for (p = _path + 1; *p; p++) {
    if (*p == '/') {
      /* Temporarily truncate */
      *p = '\0';

      if (maybe_mkdir(_path, mode) != 0) {
        goto out;
      }

      *p = '/';
    }
  }

  if (maybe_mkdir(_path, mode) != 0) {
    goto out;
  }

  result = 0;

out:
  free(_path);
  return result;
}

/**
 * Writes data to a file.
 * @param path The path to the file.
 * @param data The data to write.
 * @return 0 on success, -1 on failure.
 */
int write_file(const char *path, const char *data) {
  int fd = open(path, O_WRONLY);

  if (fd < 0) {
    return (-errno);
  }
  if (write(fd, data, strlen(data)) != (ssize_t)strlen(data)) {
    close(fd);
    return (-errno);
  }
  close(fd);
  return (0);
}
