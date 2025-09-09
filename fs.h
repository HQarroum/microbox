#ifndef MICROBOX_FS_H
#define MICROBOX_FS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <limits.h>
#include <sys/syscall.h>
#include <libgen.h>

#include "utils.h"
#include "sandbox.h"

/**
 * Bind mounts a source path to a destination path.
 * @param base The base path to use for the destination path.
 * @param spec The mount specification.
 * @return 0 on success, -1 on failure.
 */
int microbox_bind_mount(const char* base, const mount_spec_t* spec);

/**
 * Creates a new `tmpfs` filesystem at the given path.
 * @param path The path to create the tmpfs filesystem at.
 * @return 0 on success, -1 on failure.
 */
int microsandbox_create_tmpfs(const char* path);

/**
 * Creates a new overlay filesystem at the given mountpoint.
 * @param src The source directory.
 * @param mountpoint The mountpoint for the overlay filesystem.
 * @return A pointer to the overlayfs_t structure on success, NULL on failure.
 */
overlayfs_t* microsandbox_create_overlayfs(const char* src, const char* mountpoint);

/**
 * Sets up the filesystem for the sandbox using a user-defined root filesystem.
 * This function creates a private mount namespace to isolate the current root,
 * and starts by mounting a directory on the root as a tmpfs.
 * It then creates an overlayfs where the read-only lower directory is the user-defined
 * root filesystem, and the upper directory is a writable tmpfs.
 *
 * @param opts The sandbox options.
 * @return 0 on success, -1 on failure.
 */
int microsandbox_setup_rootfs(const sandbox_options_t* opts);

/**
 * Sets up the sandbox filesystem.
 * There are multiple filesystem mount options possible which we are going
 * to address below.
 *
 * - FS_NONE creates a totally empty filesystem with no mountpoints,
 * it keeps the container completely filesystem-less.
 * - FS_HOST provides the container with full access to the host filesystem.
 * - FS_TMPFS creates a new empty filesystem that's mounted in memory.
 * - FS_ROOTFS mounts a directory from the host as the root filesystem in the container.
 */
int microsandbox_setup_fs(const sandbox_options_t* opts);

#endif /* MICROBOX_FS_H */
