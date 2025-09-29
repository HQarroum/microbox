//go:build linux

package fs

import (
	"errors"
	"fmt"
	"os"
	"path"

	"golang.org/x/sys/unix"
)

/**
 * List of /proc paths to be mounted read-only.
 */
var readOnlyProcPaths = []string{
	"/proc/sys",
	"/proc/sysrq-trigger",
	"/proc/irq",
	"/proc/bus",
	"/proc/fs",
}

/**
 * List of /proc paths to be masked from the procfs.
 */
var maskedProcPaths = []string{
	"/proc/asound",
	"/proc/acpi",
	"/proc/interrupts",
	"/proc/kcore",
	"/proc/keys",
	"/proc/latency_stats",
	"/proc/timer_list",
	"/proc/timer_stats",
	"/proc/sched_debug",
	"/proc/scsi",
	"/proc/firmware",
	"/proc/devices/virtual/powercap",
}

func isDirectory(path string) (bool, error) {
	st, err := os.Lstat(path)
	if err != nil {
		return false, err
	}
	return st.Mode().IsDir(), nil
}

/**
 * Setup /proc in the sandbox rootfs by mounting a proc filesystem.
 * @param base the root path of the sandbox filesystem
 * @return error if any, the result of the mount operation otherwise
 */
func MountProc(base string) error {
	if base == "" {
		return unix.EINVAL
	}

	target := path.Join(base, "/proc")
	if err := os.MkdirAll(target, 0o755); err != nil {
		return err
	}

	if err := unix.Mount("proc", target, "proc", unix.MS_NOSUID|unix.MS_NOEXEC|unix.MS_NODEV, ""); err != nil {
		return err
	}

	// Mask selected subpaths
	for _, sub := range maskedProcPaths {
		t := path.Join(base, sub)

		// Skip if missing
		if _, err := os.Lstat(t); err != nil {
			if errors.Is(err, os.ErrNotExist) || errors.Is(err, unix.ENOTDIR) {
				continue
			}
			return fmt.Errorf("stat %s: %w", t, err)
		}

		dir, err := isDirectory(t)
		if err != nil {
			return fmt.Errorf("isDirectory %s: %w", t, err)
		}

		if dir {
			// Mask directory with an empty (read-only) tmpfs
			// (read-only avoids writes leaking into the mask)
			if err := unix.Mount("tmpfs", t, "tmpfs",
				unix.MS_NOSUID|unix.MS_NOEXEC|unix.MS_NODEV|unix.MS_RDONLY, "size=0"); err != nil {
				// Best-effort: some proc subdirs may refuse; continue
				continue
			}
		} else {
			// Mask file by bind-mounting /dev/null on top, then RO remount
			if err := unix.Mount("/dev/null", t, "", unix.MS_BIND, ""); err != nil {
				continue
			}
			if err := unix.Mount("", t, "", unix.MS_BIND|unix.MS_REMOUNT|unix.MS_RDONLY|
				unix.MS_NOSUID|unix.MS_NODEV|unix.MS_NOEXEC, ""); err != nil {
				_ = unix.Unmount(t, unix.MNT_DETACH)
				continue
			}
		}
	}

	// Make selected subpaths read-only using bind+remount
	for _, sub := range readOnlyProcPaths {
		t := path.Join(base, sub)

		// Skip if missing (kernels/configs vary)
		if _, err := os.Lstat(t); err != nil {
			if errors.Is(err, os.ErrNotExist) {
				continue
			}
			return fmt.Errorf("stat %s: %w", t, err)
		}

		// First bind the path to itself
		if err := unix.Mount(t, t, "", unix.MS_BIND, ""); err != nil {
			// If bind fails, continue to next path
			continue
		}

		// Then remount that bind as read-only and safe
		flags := uintptr(unix.MS_BIND |
			unix.MS_REMOUNT |
			unix.MS_RDONLY |
			unix.MS_NOSUID |
			unix.MS_NODEV |
			unix.MS_NOEXEC)

		if err := unix.Mount("", t, "", flags, ""); err != nil {
			_ = unix.Unmount(t, unix.MNT_DETACH)
			continue
		}
	}

	return nil
}
