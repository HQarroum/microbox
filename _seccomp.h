#ifndef MICROBOX_SECCOMP_H
#define MICROBOX_SECCOMP_H

#include <sys/types.h>
#include <seccomp.h>
#include <string.h>
#include <errno.h>

/**
 * Install a “default-allow + denylist” seccomp filter.
 *
 * @param deny         array of syscall names to block with EPERM
 * @param deny_len     number of entries in deny
 * @param allow_ovr    array of syscall names to *allow* even if in deny
 * @param allow_len    number of entries in allow_ovr
 * @return 0 on success, negative errno on failure
 */
int microbox_setup_seccomp(
  const char **deny,
  size_t deny_len,
  const char **allow_ovr,
  size_t allow_len
);

/**
 * @return the length of the default denylist.
 */
size_t docker_default_denylist_len(void);

/**
 * @return the default denylist.
 */
const char** microbox_get_docker_default_denylist();

#endif /* MICROBOX_SECCOMP_H */
