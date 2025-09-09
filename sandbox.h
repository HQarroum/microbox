#ifndef SANDBOX_H
#define SANDBOX_H

#include <stdint.h>
#include <sys/types.h>
#include <sched.h>
#include <string.h>
#include <unistd.h>
#include <linux/sched.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/prctl.h>
#include <linux/prctl.h>
#include <linux/capability.h>

#include "utils.h"
#include "net.h"
#include "netns.h"

/**
 * Create a notification pipe.
 */
#define MAKE_PIPE(sync_pipe) \
  do { \
    if (pipe(sync_pipe) < 0) { \
      fprintf(stderr, "pipe() failed: %s\n", strerror(errno)); \
      _exit(127); \
    } \
  } while (0)

/**
 * Wait for parent process to signal readiness.
 */
#define WAIT_FOR_PARENT(sync_pipe) \
  do { \
    char go; \
    if (read(sync_pipe[0], &go, 1) < 0) { \
      fprintf(stderr, "read() failed: %s\n", strerror(errno)); \
      _exit(127); \
    } \
    close(sync_pipe[0]); \
  } while (0)

/**
 * Signal child process to continue.
 */
#define SIGNAL_CHILD(sync_pipe) \
  do { \
    if (write(sync_pipe[1], "X", 1) < 0) { \
      fprintf(stderr, "write() failed: %s\n", strerror(errno)); \
      _exit(127); \
    } \
    close(sync_pipe[1]); \
  } while (0)

/**
 * Flag type for sandbox creation.
 */
typedef unsigned long long sandbox_flag_t;

typedef struct {

  /**
   * PID file descriptor.
   */
  int pidfd;

  /**
   * Process identifier.
   */
  pid_t pid;
} sandbox_process_t;

/**
 * The mount mode (read-only or read-write).
 */
typedef enum {
  MNT_RO,
  MNT_RW
} mnt_mode_t;

/**
 * The filesystem mode (temporary, host, or rootfs).
 */
typedef enum {
  FS_TMPFS,
  FS_HOST,
  FS_ROOTFS
} fs_mode_t;

/**
 * Structure describing one bind mount specification.
 */
typedef struct {

  /**
   * Path of a directory to bind mount from the host.
   */
  char *host;

  /**
   * Destination path in the sandbox.
   */
  char *dest;

  /**
   * Mount mode (read-only or read-write).
   */
  mnt_mode_t mode;
} mount_spec_t;

/**
 * Structure describing one environment variable.
 */
typedef struct {
  char* name;
  char* value;
} env_var_t;

/**
 * The `overlayfs_t` struct describes the components
 * of an overlay filesystem.
 */
typedef struct {
  char* lowerdir;
  char* upperdir;
  char* workdir;
  char* mergedir;
} overlayfs_t;

typedef struct {

  /**
   * The filesystem mode (temporary, host, or rootfs) to use
   * within the sandbox.
   */
  fs_mode_t fs_mode;

  /**
   * The network mode (none, host, or bridge) to use
   * within the sandbox.
   */
  net_mode_t net_mode;

  /**
   * The root filesystem path to mount in the sandbox.
   */
  char* rootfs;

  /**
   * The hostname to associate with the sandbox.
   */
  char* hostname;

  /**
   * The CPU count allocation to grant to the sandbox.
   * @example 0.5
   */
  double cpus;

  /**
   * The memory allocation to grant to the sandbox.
   * @example 1024
   */
  uint64_t memory;

  /**
   * The mount points to bind mount from the host.
   */
  mount_spec_t* mounts;

  /**
   * The number of mount points to bind mount from the host.
   */
  size_t mounts_len;

  /**
   * Whether to mount /proc in the sandbox.
   */
  int mount_proc;

  /**
   * Whether to mount /dev in the sandbox.
   */
  int mount_dev;

  /**
   * The environment variables to set in the sandbox.
   */
  env_var_t* env;

  /**
   * The number of environment variables to set in the sandbox.
   */
  size_t env_len;

  /**
   * The list of allowed system calls override.
   */
  char** syscalls_allow;

  /**
   * The number of allowed system calls override.
   */
  size_t syscalls_allow_len;

  /**
   * The list of denied system calls.
   */
  char** syscalls_deny;

  /**
   * The number of denied system calls.
   */
  size_t syscalls_deny_len;

  char** cmd;
  int cmdc;
} sandbox_options_t;

/**
 * Spawns a new process in a new sandbox.
 * @param opts The sandbox options.
 * @param process The sandbox process.
 * @return 0 on success, -1 on failure.
 */
int microbox_sandbox_spawn(const sandbox_options_t* opts, sandbox_process_t *process);

/**
 * Wait for the sandboxed process to exit.
 * @param proc The sandbox process to wait for.
 * @return The exit status of the sandboxed process.
 */
int microbox_sandbox_wait(const sandbox_process_t *proc);

#endif /* SANDBOX_H */
