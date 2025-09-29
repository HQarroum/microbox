//go:build linux

package fs

import (
	"fmt"
	"os"
	"path/filepath"

	"golang.org/x/sys/unix"
)

/**
 * Filesystem options for setting up the sandbox filesystem.
 */
type FsOpts struct {
	Nameservers []string
	Hostname    string
	FS          FsMount
	ReadOnly    bool
	MountRO     []MountSpec
	MountRW     []MountSpec
	Storage     uint64
}

/**
 * Filesystem type.
 * rootfs, tmpfs, host
 */
type FsType int

/**
 * Filesystem type information.
 */
type FsMount struct {

	// Filesystem type.
	Mode FsType

	// Rootfs path (if Mode==FsRootfs).
	Path string
}

/**
 * Bind mount type.
 */
type MountSpec struct {
	Host string
	Dest string
	RO   bool
}

/**
 * Filesystem modes.
 */
const (
	FsTmpfs FsType = iota
	FsHost
	FsRootfs
)

/**
 * @return a string representation of filesystem mode.
 */
func (m FsType) String() string {
	switch m {
	case FsTmpfs:
		return "tmpfs"
	case FsHost:
		return "host"
	case FsRootfs:
		return "rootfs"
	default:
		return "unknown"
	}
}

/**
 * Overlay filesystem structure.
 */
type overlayFS struct {
	lower string
	upper string
	work  string
	merge string
}

/**
 * Check if a path is a directory.
 * @param path the path to check
 * @return true if the path is a directory, false otherwise
 */
func isDir(path string) bool {
	fi, err := os.Stat(path)
	return err == nil && fi.IsDir()
}

/**
 * Bind-mount a host path to a target path within the sandbox.
 * Creates the target path if it doesn't exist.
 * @param base the root path of the sandbox filesystem
 * @param spec the mount specification
 * @return error if any
 */
func BindMount(base string, spec MountSpec) error {
	if base == "" || spec.Host == "" || spec.Dest == "" {
		return unix.EINVAL
	}
	target := filepath.Join(base, spec.Dest)

	// Source must exist.
	st := &unix.Stat_t{}
	if err := unix.Stat(spec.Host, st); err != nil {
		return err
	}

	switch st.Mode & unix.S_IFMT {
	case unix.S_IFDIR:
		if err := os.MkdirAll(target, 0o755); err != nil {
			return err
		}
	case unix.S_IFREG, unix.S_IFCHR, unix.S_IFBLK, unix.S_IFIFO, unix.S_IFSOCK:
		if err := os.MkdirAll(filepath.Dir(target), 0o755); err != nil {
			return err
		}
		// Create a placeholder file if needed so the bind mount has a target.
		f, err := os.OpenFile(target, os.O_CREATE, 0o644)
		if err != nil {
			return err
		}
		_ = f.Close()
	case unix.S_IFLNK:
		return fmt.Errorf("bind-mounting symlinks is not supported: %s", spec.Host)
	default:
		return fmt.Errorf("unsupported source file type: %s", spec.Host)
	}

	// Mount the source to the target.
	if err := unix.Mount(spec.Host, target, "", unix.MS_BIND|unix.MS_REC|unix.MS_NOSUID|unix.MS_NODEV, ""); err != nil {
		return err
	}

	// Optional read-only remount.
	if spec.RO {
		if err := unix.Mount("", target, "", unix.MS_BIND|unix.MS_REMOUNT|unix.MS_RDONLY|unix.MS_NOSUID|unix.MS_NODEV, ""); err != nil {
			return err
		}
	}
	return nil
}

/**
 * Create a `tmpfs` at the specified path.
 * @param path the path to create the `tmpfs` at
 * @param storage the size of the `tmpfs` in megabytes
 * @return error if any
 */
func createTmpfs(path string, storage uint64) error {
	if path == "" {
		return unix.EINVAL
	}
	if err := os.MkdirAll(path, 0o755); err != nil {
		return err
	}
	return unix.Mount("tmpfs", path, "tmpfs", unix.MS_NOSUID|unix.MS_NODEV, fmt.Sprintf("mode=755,size=%dm", storage/1024/1024))
}

/**
 * Create an `overlayfs` with the specified lower (read-only) and upper (read-write) layers.
 * The upper layer is created on a `tmpfs` at the specified mountpoint.
 * @param src the lower (read-only) layer
 * @param mountpoint the mountpoint for the `tmpfs` and `overlayfs`
 * @return the created overlayFS structure and error if any
 */
func createOverlay(src, mountpoint string) (*overlayFS, error) {
	if src == "" || mountpoint == "" {
		return nil, unix.EINVAL
	}

	fs := &overlayFS{
		lower: src,
		upper: filepath.Join(mountpoint, "upper"),
		work:  filepath.Join(mountpoint, "work"),
		merge: filepath.Join(mountpoint, "merged"),
	}

	// Make the directories.
	if err := os.MkdirAll(fs.upper, 0o755); err != nil {
		return nil, err
	}
	if err := os.MkdirAll(fs.work, 0o755); err != nil {
		return nil, err
	}
	if err := os.MkdirAll(fs.merge, 0o755); err != nil {
		return nil, err
	}

	// Mount overlay.
	opts := fmt.Sprintf("lowerdir=%s,upperdir=%s,workdir=%s", fs.lower, fs.upper, fs.work)
	if err := unix.Mount("overlay", fs.merge, "overlay", 0, opts); err != nil {
		return nil, err
	}

	return fs, nil
}

/**
 * Pivot to a new root filesystem.
 * @param newRoot the new root filesystem path
 * @return error if any
 */
func pivotTo(newRoot string) error {
	if err := os.Chdir(newRoot); err != nil {
		return err
	}

	if err := os.MkdirAll(".old_root", 0o700); err != nil {
		return err
	}

	// Use unix.PivotRoot wrapper.
	if err := unix.PivotRoot(".", "./.old_root"); err != nil {
		return err
	}

	if err := os.Chdir("/"); err != nil {
		return err
	}

	// Lazy detach old root and remove it.
	if err := unix.Unmount("/.old_root", unix.MNT_DETACH); err != nil {
		return err
	}

	return os.Remove("/.old_root")
}

/**
 * Setup root filesystem using overlayfs based on a user-provided directory.
 * @param opts the sandbox options
 * @return error if any
 */
func setupRootfs(opts *FsOpts) error {
	// Ensure the root filesystem is mounted as private, so that changes
	// made within the container do not affect the host filesystem, and
	// changes made outside the container do not affect the container.
	if err := unix.Mount("", "/", "", unix.MS_PRIVATE|unix.MS_REC, ""); err != nil {
		return err
	}

	// Validate lower (read-only) root.
	if !isDir(opts.FS.Path) {
		return fmt.Errorf("rootfs %q not a directory", opts.FS.Path)
	}

	// We first create a `tmpfs` at /box as a writable, ephemeral filesystem.
	tmp := "/box"
	if err := createTmpfs(tmp, opts.Storage); err != nil {
		return err
	}

	overlayMP := "/box/overlay"
	if err := os.MkdirAll(overlayMP, 0o755); err != nil {
		return err
	}

	// We create an `overlayfs` on top of the `tmpfs`.
	ov, err := createOverlay(opts.FS.Path, overlayMP)
	if err != nil {
		return fmt.Errorf("error creating overlayfs: %w", err)
	}

	// Mount `procfs`.
	if err := MountProc(ov.merge); err != nil {
		return fmt.Errorf("error mounting procfs: %w", err)
	}

	// Mount `devfs`.
	if err := MountDev(ov.merge); err != nil {
		return fmt.Errorf("error mounting devfs: %w", err)
	}

	// Mount `/tmp`.
	if err := MountTmp(ov.merge); err != nil {
		return fmt.Errorf("error mounting /tmp: %w", err)
	}

	// Setup /etc configuration files.
	if err := SetupEtc(ov.merge, opts.Nameservers, opts.Hostname); err != nil {
		return fmt.Errorf("error setting up /etc: %w", err)
	}

	// User read-only bind mounts.
	for _, m := range opts.MountRO {
		if err := BindMount(ov.merge, MountSpec{Host: m.Host, Dest: m.Dest, RO: true}); err != nil {
			return err
		}
	}

	// User read-write bind mounts.
	for _, m := range opts.MountRW {
		if err := BindMount(ov.merge, MountSpec{Host: m.Host, Dest: m.Dest, RO: false}); err != nil {
			return err
		}
	}

	// Switch root to merged dir.
	if err := pivotTo(ov.merge); err != nil {
		return err
	}

	// Remount the rootfs as read-only if requested.
	if opts.ReadOnly {
		if err := unix.Mount("", "/", "", unix.MS_REMOUNT|unix.MS_RDONLY, ""); err != nil {
			return err
		}
	}

	return nil
}

/**
 * Setup root filesystem as an empty `tmpfs`.
 * @param opts the sandbox options
 * @return error if any
 */
func setupTmpfsRoot(opts *FsOpts) error {
	base := "/box"

	// Ensure the root filesystem is mounted as private, so that changes
	// made within the container do not affect the host filesystem, and
	// changes made outside the container do not affect the container.
	if err := unix.Mount("", "/", "", unix.MS_PRIVATE|unix.MS_REC, ""); err != nil {
		return err
	}

	// Create root filesystem as `tmpfs`.
	if err := createTmpfs(base, opts.Storage); err != nil {
		return err
	}

	// Mount `procfs`.
	if err := MountProc(base); err != nil {
		return err
	}

	// Mount `devfs`.
	if err := MountDev(base); err != nil {
		return err
	}

	// Mount `/tmp`.
	if err := MountTmp(base); err != nil {
		return fmt.Errorf("error mounting /tmp: %w", err)
	}

	// Setup /etc configuration files.
	if err := SetupEtc(base, opts.Nameservers, opts.Hostname); err != nil {
		return err
	}

	// User read-only bind mounts.
	for _, m := range opts.MountRO {
		if err := BindMount(base, MountSpec{Host: m.Host, Dest: m.Dest, RO: true}); err != nil {
			return err
		}
	}

	// User read-write bind mounts.
	for _, m := range opts.MountRW {
		if err := BindMount(base, MountSpec{Host: m.Host, Dest: m.Dest, RO: false}); err != nil {
			return err
		}
	}

	// Switch root to the new `tmpfs`.
	if err := pivotTo(base); err != nil {
		return err
	}

	// Remount the rootfs as read-only if requested.
	if opts.ReadOnly {
		if err := unix.Mount("", "/", "", unix.MS_REMOUNT|unix.MS_RDONLY, ""); err != nil {
			return err
		}
	}

	return nil
}

/**
 * Setup root filesystem as the host's root.
 * @param opts the sandbox options
 * @return error if any
 */
func setupHostfsRoot(opts *FsOpts) error {
	base := "/box"
	readOnly := false

	// Ensure the root filesystem is mounted as private, so that changes
	// made within the container do not affect the host filesystem, and
	// changes made outside the container do not affect the container.
	if err := unix.Mount("", "/", "", unix.MS_PRIVATE|unix.MS_REC, ""); err != nil {
		return err
	}

	// Create root filesystem as `tmpfs`.
	if err := createTmpfs(base, opts.Storage); err != nil {
		return err
	}

	// Bind-mount host root to /box.
	if opts.ReadOnly {
		readOnly = true
	}
	if err := BindMount(base, MountSpec{Host: "/", Dest: "/", RO: readOnly}); err != nil {
		return err
	}

	return pivotTo(base)
}

// SetupFS chooses the filesystem strategy based on opts.FS.Mode.
//   - FsHost  : do nothing (use host root).
//   - FsTmpfs : empty tmpfs root.
//   - FsRootfs: overlay(lower=opts.FS.Path, upper/work on tmpfs).
func SetupFS(opts *FsOpts) error {
	switch opts.FS.Mode {
	case FsHost:
		return setupHostfsRoot(opts)
	case FsTmpfs:
		return setupTmpfsRoot(opts)
	case FsRootfs:
		return setupRootfs(opts)
	default:
		return unix.EINVAL
	}
}
