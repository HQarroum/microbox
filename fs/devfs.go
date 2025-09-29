//go:build linux

package fs

import (
	"errors"
	"os"
	"path"

	"golang.org/x/sys/unix"
)

/**
 * List of essential device files to bind-mount from host into the sandbox /dev.
 */
var devAllowlist = []string{
	"/dev/null",
	"/dev/zero",
	"/dev/random",
	"/dev/urandom",
	"/dev/tty",
}

/**
 * Create a symlink from src to dest, removing dest if it exists.
 * @param src the source path
 * @param dest the destination path
 * @return error if any
 */
func linkDev(src, dest string) error {
	if src == "" || dest == "" {
		return unix.EINVAL
	}

	_ = os.Remove(dest)
	if err := os.Symlink(src, dest); err != nil && !errors.Is(err, os.ErrExist) {
		return err
	}
	return nil
}

/**
 * Setup /dev in the sandbox rootfs.
 * This includes mounting a tmpfs on /dev, setting up /dev/pts and /dev/shm,
 * and bind-mounting a set of essential device files from the host.
 * @param base the root path of the sandbox filesystem
 * @return error if any
 */
func MountDev(base string) error {
	if base == "" {
		return unix.EINVAL
	}

	// Create /dev directory and mount a `tmpfs` onto it.
	dev := path.Join(base, "/dev")
	if err := os.MkdirAll(dev, 0o755); err != nil {
		return err
	}
	if err := unix.Mount("tmpfs", dev, "tmpfs", unix.MS_NOSUID|unix.MS_NOEXEC|unix.MS_STRICTATIME, "mode=755,size=65536k"); err != nil {
		return err
	}

	// Create /dev/pts and mount `devpts` onto it.
	pts := path.Join(base, "/dev/pts")
	if err := os.MkdirAll(pts, 0o755); err != nil {
		return err
	}
	if err := unix.Mount("devpts", pts, "devpts", unix.MS_NOSUID|unix.MS_NOEXEC, "newinstance,ptmxmode=0666,mode=0620"); err != nil && !errors.Is(err, unix.EINVAL) {
		return err
	}

	// /dev/ptmx as symlink to /dev/pts/ptmx.
	if err := linkDev("/dev/pts/ptmx", path.Join(base, "/dev/ptmx")); err != nil {
		return err
	}

	// Create /dev/shm as a `tmpfs`.
	shm := path.Join(base, "/dev/shm")
	if err := os.MkdirAll(shm, 0o777); err != nil {
		return err
	}
	if err := unix.Mount("tmpfs", shm, "tmpfs", unix.MS_NOSUID|unix.MS_NOEXEC|unix.MS_NODEV, "mode=1777,size=65536k"); err != nil {
		return err
	}

	// Create /dev/mqueue as a `mqueue` filesystem.
	mqueue := path.Join(base, "/dev/mqueue")
	if err := os.MkdirAll(mqueue, 0o755); err != nil {
		return err
	}
	if err := unix.Mount("mqueue", mqueue, "mqueue", unix.MS_NOSUID|unix.MS_NOEXEC|unix.MS_NODEV, ""); err != nil && !errors.Is(err, unix.EINVAL) {
		return err
	}

	// Symlink /dev/fd to /proc/self/fd.
	if err := linkDev("/proc/self/fd", path.Join(base, "/dev/fd")); err != nil {
		return err
	}

	// Symlink /dev/stdin to /proc/self/fd/0.
	if err := linkDev("/proc/self/fd/0", path.Join(base, "/dev/stdin")); err != nil {
		return err
	}

	// Symlink /dev/stdout to /proc/self/fd/1.
	if err := linkDev("/proc/self/fd/1", path.Join(base, "/dev/stdout")); err != nil {
		return err
	}

	// Symlink /dev/stderr to /proc/self/fd/2.
	if err := linkDev("/proc/self/fd/2", path.Join(base, "/dev/stderr")); err != nil {
		return err
	}

	// Symlink /dev/core to /proc/kcore.
	if err := linkDev("/proc/kcore", path.Join(base, "/dev/core")); err != nil {
		return err
	}

	// Bind-mount allow-list of device files from host.
	for _, p := range devAllowlist {
		spec := MountSpec{Host: p, Dest: p, RO: false}
		if err := BindMount(base, spec); err != nil {
			// best-effort; continue if a device is missing
			continue
		}
	}

	return nil
}
