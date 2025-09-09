#include "fs.h"
#include <stdio.h>

static const char* dev_dirs[] = {
  "/dev/null",
  "/dev/zero",
  "/dev/random",
  "/dev/urandom",
  "/dev/tty"
};

/**
 * Join two paths together.
 * @param path1 The first path.
 * @param path2 The second path.
 * @return A new string containing the joined path, or NULL on failure.
 */
static char* path_join(const char* path1, const char* path2) {
  size_t len1 = strlen(path1);
  size_t len2 = strlen(path2);
  char* result = calloc(len1 + len2 + 2, sizeof(char));

  if (!result) {
    return (NULL);
  }
  snprintf(result, len1 + len2 + 2, "%s/%s", path1, path2);
  return (result);
}

/**
 * Bind mounts a source path to a destination path.
 * @param base The base path to use for the destination path.
 * @param spec The mount specification.
 * @return 0 on success, -1 on failure.
 */
int microbox_bind_mount(const char* base, const mount_spec_t* spec) {
  char target[PATH_MAX] = {0};

  if (!base || !spec || !spec->host || !spec->dest) {
    errno = EINVAL;
    return (-1);
  }

  if (snprintf(target, sizeof(target), "%s%s", base, spec->dest) < 0) {
    return (-1);
  }

  // Ensure the source path exists.
  struct stat st;
  if (stat(spec->host, &st) < 0) {
    return (-1);
  }

  if (S_ISDIR(st.st_mode)) {
    // Recursively ensure that the destination deep paths exist.
    if (mkdirp(target) < 0) {
      return (-1);
    }
  } else if (S_ISREG(st.st_mode) || S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) {
    char* target_copy = strdup(target);
    char* dir_name = dirname(target_copy);

    if (mkdirp(dir_name) < 0) {
      return (-1);
    }
    int fd = open(target, O_CREAT, 0644);
    if (fd < 0) {
      return (-1);
    }
    close(fd);
    free(target_copy);
  } else {
    return (-1);
  }

  // Mount the source path to the destination path.
  if (mount(spec->host, target, NULL, MS_BIND | MS_REC, NULL) < 0) {
    return (-1);
  }

  // Remount as read-only if specified.
  if (spec->mode == MNT_RO) {
    if (mount(NULL, target, NULL, MS_BIND | MS_REMOUNT | MS_RDONLY | MS_NOSUID, 0) < 0) {
      return (-1);
    }
  }

  return (0);
}

/**
 * Creates a new `tmpfs` filesystem at the given path.
 * @param path The path to create the tmpfs filesystem at.
 * @return 0 on success, -1 on failure.
 */
int microsandbox_create_tmpfs(const char* path) {
  if (!path) {
    errno = EINVAL;
    return (-1);
  }

  // Create the base dir in current root.
  if (mkdir_safe(path, 0755) < 0) {
    return (-1);
  }

  // Mount a tmpfs on the given path.
  if (mount("tmpfs", path, "tmpfs", MS_NOSUID | MS_NODEV, "mode=700,size=512m") < 0) {
    return (-1);
  }

  return (0);
}

/**
 * Creates a new overlay filesystem at the given mountpoint.
 * @param src The source directory.
 * @param mountpoint The mountpoint for the overlay filesystem.
 * @return A pointer to the overlayfs_t structure on success, NULL on failure.
 */
overlayfs_t* microsandbox_create_overlayfs(const char* src, const char* mountpoint) {
  overlayfs_t* fs = calloc(1, sizeof(overlayfs_t));
  if (!fs) {
    return (NULL);
  }

  const char* upper = "upper";
  const char* work = "work";
  const char* merged = "merged";

  // Create the paths.
  fs->lowerdir = strdup(src);
  fs->upperdir = path_join(mountpoint, upper);
  fs->workdir = path_join(mountpoint, work);
  fs->mergedir = path_join(mountpoint, merged);

  int fd = open(mountpoint, O_DIRECTORY | O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return (NULL);
  }

  // Create the `upper`, `work` and `merged` directories.
  if (mkdirat(fd, upper, 0755) < 0 ||
      mkdirat(fd, work, 0755) < 0 ||
      mkdirat(fd, merged, 0755) < 0) {
    close(fd);
    return (NULL);
  }
  close(fd);

  // Create the overlay options.
  size_t size = strlen(src) +
    strlen(fs->upperdir) +
    strlen(fs->workdir) +
    strlen("lowerdir=,") +
    strlen("upperdir=,") +
    strlen("workdir=") +
    1;

  char* o = calloc(size, sizeof(char));
  snprintf(o, size, "lowerdir=%s,upperdir=%s,workdir=%s", src, fs->upperdir, fs->workdir);

  // Create the overlay filesystem.
  if (mount("overlay", fs->mergedir, "overlay", 0, o) < 0) {
    free(o);
    free(fs->mergedir);
    free(fs->workdir);
    free(fs->upperdir);
    free(fs->lowerdir);
    free(fs);
    return (NULL);
  }

  free(o);
  return (fs);
}

/**
 * Mounts the /proc filesystem.
 * @param opts The sandbox options.
 * @return 0 on success, a negative error code on failure.
 */
int microbox_bind_mount_proc(const char* base) {
  char target[PATH_MAX] = {0};

  if (!base) {
    errno = EINVAL;
    return (-1);
  }

  if (snprintf(target, sizeof target, "%s/proc", base) < 0) {
    return (-1);
  }

  // Recursively ensure that the destination deep paths exist.
  if (mkdirp(target) < 0) {
    return (-errno);
  }

  // Mount a proc filesystem on the /proc directory.
  if (mount("proc", target, "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL) < 0) {
    return (-errno);
  }

  return (0);
}

/**
 * This helper function mounts the /dev filesystem in the sandbox.
 * @param base The base directory on which to mount the /dev filesystem.
 * @return 0 on success, a negative error code on failure.
 */
int microbox_bind_mount_dev(const char* base) {
  char target[PATH_MAX] = {0};
  char pts[PATH_MAX] = {0};
  char ptmx[PATH_MAX] = {0};
  char shm[PATH_MAX] = {0};

  if (!base) {
    errno = EINVAL;
    return (-1);
  }

  // The `/dev` directory in the base.
  if (snprintf(target, sizeof(target), "%s/dev", base) < 0) {
    return (-1);
  }

  // Recursively ensure that the destination deep paths exist.
  if (mkdirp(target) < 0) {
    return (-errno);
  }

  // Mount a tmpfs filesystem on the /dev directory.
  if (mount("tmpfs", target, "tmpfs", MS_NOSUID | MS_NOEXEC, "mode=755,size=2m") < 0) {
    return (-errno);
  }

  // Mount a devpts filesystem on the /dev/pts directory.
  if (snprintf(pts, sizeof(pts), "%s/dev/pts", base) < 0) {
    return (-1);
  }
  if (mkdirp(pts) < 0) {
    return (-errno);
  }
  if (mount("devpts", pts, "devpts", MS_NOSUID | MS_NOEXEC, "newinstance,ptmxmode=0666,mode=620") < 0) {
    if (errno != EINVAL) {
      return (-errno);
    }
  }

  if (snprintf(ptmx, sizeof(ptmx), "%s/dev/ptmx", base) < 0) {
    return (-1);
  }
  unlink(ptmx);

  if (symlink("pts/ptmx", ptmx) < 0) {
    return (-errno);
  }

  if (snprintf(shm, sizeof(shm), "%s/dev/shm", base) < 0) {
    return (-1);
  }

  if (mkdirp(shm) < 0) {
    return (-errno);
  }

  if (mount("tmpfs", shm, "tmpfs", MS_NOSUID | MS_NOEXEC, "mode=1777,size=64m") < 0) {
    return (-errno);
  }

  /* Bind-mount a small device allowlist from the host */
  for (size_t i = 0; i < SIZE_OF(dev_dirs); i++) {
    const mount_spec_t spec = {
      .host = (char*) dev_dirs[i],
      .dest = (char*) dev_dirs[i],
      .mode = MNT_RW
    };
    microbox_bind_mount(base, &spec);
  }

  return (0);
}

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
int microsandbox_setup_rootfs(const sandbox_options_t* opts) {
  // Ensure the root filesystem is mounted as private, so that changes
  // made within the container do not affect the host filesystem, and
  // changes made outside the container do not affect the container.
  if (mount(NULL, "/", NULL, MS_PRIVATE | MS_REC, NULL) < 0) {
    return (-1);
  }

  // Verify that the root filesystem directory is set.
  if (!is_directory(opts->rootfs)) {
    errno = ENOENT;
    return (-1);
  }

  // Mount a tmpfs on /box.
  const char tmpfs_mount[] = "/box";
  if (microsandbox_create_tmpfs(tmpfs_mount) < 0) {
    return (-1);
  }

  // Mount an `overlayfs` on the `tmpfs` mountpoint.
  const char overlay_mount[] = "/box/overlay";

  // Create the overlay mount point.
  if (mkdir(overlay_mount, 0755) < 0) {
    return (-1);
  }

  overlayfs_t* fs = microsandbox_create_overlayfs(opts->rootfs, overlay_mount);
  if (fs == NULL) {
    return (-1);
  }

  // Bind mount the user-specified mountpoints.
  for (size_t i = 0; i < opts->mounts_len; ++i) {
    if (microbox_bind_mount(fs->mergedir, &opts->mounts[i]) < 0) {
      return (-1);
    }
  }

  // Mount /proc.
  if (opts->mount_proc) {
    if (microbox_bind_mount_proc(fs->mergedir) < 0) {
      return (-errno);
    }
  }

  // Mount /dev.
  if (opts->mount_dev) {
    if (microbox_bind_mount_dev(fs->mergedir) < 0) {
      return (-errno);
    }
  }

  // Switch to the merged directory.
  if (chdir(fs->mergedir) < 0) {
    return (-errno);
  }

  // Create a directory to hold the old root filesystem.
  if (mkdir(".old_root", 0700) < 0) {
    return (-errno);
  }

  // Pivot root to the merged directory.
  if (syscall(SYS_pivot_root, ".", "./.old_root") < 0) {
    return (-errno);
  }

  // By default we want to be in the root.
  if (chdir("/") < 0) {
    return (-errno);
  }

  // We unmount the old root filesystem.
  if (umount2("/.old_root", MNT_DETACH) < 0) {
    return (-errno);
  }

  // Remove the old root directory.
  if (rmdir("/.old_root") < 0) {
    return (-errno);
  }

  free(fs->lowerdir);
  free(fs->upperdir);
  free(fs->workdir);
  free(fs->mergedir);
  free(fs);
  return (0);
}

int microsandbox_setup_tmpfs(const sandbox_options_t* opts) {
  const char *base = "/box";

  if (!opts) {
    errno = EINVAL;
    return (-1);
  }

  /* Keep mount events local. */
  if (mount(NULL, "/", NULL, MS_PRIVATE | MS_REC, NULL) < 0) {
    return (-1);
  }

  /* Create empty tmpfs as future root. */
  if (microsandbox_create_tmpfs(base) < 0) {
    return (-1);
  }

  /* Optional: /proc and minimal /dev inside the tmpfs root. */
  if (opts->mount_proc) {
    if (microbox_bind_mount_proc(base) < 0) {
      return (-1);
    }
  }

  if (opts->mount_dev) {
    if (microbox_bind_mount_dev(base) < 0) {
      return (-1);
    }
  }

  /* Apply user bind-mounts into the tmpfs root. */
  for (size_t i = 0; i < opts->mounts_len; ++i) {
    if (microbox_bind_mount(base, &opts->mounts[i]) < 0) {
      return (-1);
    }
  }

  /* Enter the tmpfs root. */
  if (chdir(base) < 0) {
    return (-errno);
  }

  // Create a directory for the old root filesystem.
  if (mkdir(".old_root", 0700) < 0) {
    return (-errno);
  }

  // Pivot root into the new root filesystem.
  if (syscall(SYS_pivot_root, ".", "./.old_root") < 0) {
    return (-errno);
  }

  // Change directory to the new root filesystem.
  if (chdir("/") < 0) {
    return (-errno);
  }

  // Unmount the old root filesystem.
  if (umount2("/.old_root", MNT_DETACH) < 0) {
    return (-errno);
  }

  // Remove the old root directory.
  if (rmdir("/.old_root") < 0) {
    return (-errno);
  }

  return (0);
}

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
int microsandbox_setup_fs(const sandbox_options_t* opts) {
  switch (opts->fs_mode) {
    case FS_HOST:
      // Nothing to do here.
      return (0);
    case FS_TMPFS:
      return (microsandbox_setup_tmpfs(opts));
    case FS_ROOTFS:
      return (microsandbox_setup_rootfs(opts));
    default:
      return (-EINVAL);
  }
}
