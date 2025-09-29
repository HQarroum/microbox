//go:build linux

package sandbox

import (
	"bytes"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"syscall"
	"time"
)

const (
	cgRoot   = "/sys/fs/cgroup"
	cgParent = "/sys/fs/cgroup/microbox"
)

/**
 * Enables controllers for children of parentPath.
 * parentPath is usually the cgroup you’re *currently in* (under systemd with Delegate).
 */
func enableControllers(parentPath string, ctrls ...string) error {
	f, err := os.OpenFile(
		filepath.Join(parentPath, "cgroup.subtree_control"),
		os.O_WRONLY|syscall.O_CLOEXEC,
		0,
	)
	if err != nil {
		return err
	}
	defer f.Close()

	for _, c := range ctrls {
		if _, err := f.WriteString("+" + c); err != nil && !errors.Is(err, syscall.EBUSY) {
			return err
		}
	}

	return nil
}

/**
 * Ensures the cgroup parent exists and has controllers enabled.
 */
func EnsureCgroupParent() error {
	if err := os.Mkdir(cgParent, 0o755); err != nil && !errors.Is(err, os.ErrExist) {
		return fmt.Errorf("mkdir %s: %w", cgParent, err)
	}

	// Parent’s parent must have controllers enabled for *it* to delegate.
	if err := enableControllers(cgRoot, "cpu", "memory"); err != nil {
		return fmt.Errorf("enable controllers on %s: %w", cgRoot, err)
	}

	// Enable on our parent so children can set limits.
	if err := enableControllers(cgParent, "cpu", "memory"); err != nil {
		return fmt.Errorf("enable controllers on %s: %w", cgParent, err)
	}

	return nil
}

// SetupCgroupLimits creates a cgroup and applies cpu/memory limits, then moves pid into it.
// cpus: 0 => unlimited. memory: 0 => unlimited.
func SetupCgroupLimits(pid int, cpus float64, memory uint64) (string, error) {
	if err := EnsureCgroupParent(); err != nil {
		return "", err
	}

	// Use a stable, unique name (pid + timestamp to avoid reuse races).
	name := fmt.Sprintf("%d-%d", pid, time.Now().UnixNano())
	cgPath := filepath.Join(cgParent, name)
	if err := os.Mkdir(cgPath, 0o755); err != nil && !errors.Is(err, os.ErrExist) {
		return "", fmt.Errorf("mkdir %s: %w", cgPath, err)
	}

	// CPU limits.
	if cpus <= 0 {
		if err := os.WriteFile(filepath.Join(cgPath, "cpu.max"), []byte("max 100000"), 0o644); err != nil {
			return "", fmt.Errorf("write cpu.max: %w", err)
		}
	} else {
		const period = 100000 // 100ms
		quota := uint64(cpus * period)
		line := strconv.FormatUint(quota, 10) + " " + strconv.Itoa(period)
		if err := os.WriteFile(filepath.Join(cgPath, "cpu.max"), []byte(line), 0o644); err != nil {
			return "", fmt.Errorf("write cpu.max: %w", err)
		}
	}

	// Memory limits.
	if memory == 0 {
		if err := os.WriteFile(filepath.Join(cgPath, "memory.max"), []byte("max"), 0o644); err != nil {
			return "", fmt.Errorf("write memory.max: %w", err)
		}
	} else {
		if err := os.WriteFile(filepath.Join(cgPath, "memory.max"), []byte(strconv.FormatUint(memory, 10)), 0o644); err != nil {
			return "", fmt.Errorf("write memory.max: %w", err)
		}
		_ = os.WriteFile(filepath.Join(cgPath, "memory.swap.max"), []byte("0"), 0o644) // best-effort
	}

	// Move the task into the cgroup *after* limits are set.
	if err := os.WriteFile(filepath.Join(cgPath, "cgroup.procs"), []byte(strconv.Itoa(pid)), 0o644); err != nil {
		return "", fmt.Errorf("attach pid to cgroup: %w", err)
	}

	return cgPath, nil
}

/**
 * Cleans up a cgroup created by SetupCgroupLimits.
 */
func CleanupCgroup(cgPath string) error {
	if cgPath == "" {
		return nil
	}
	if err := os.WriteFile(filepath.Join(cgPath, "cgroup.kill"), []byte("1"), 0o644); err != nil && !errors.Is(err, os.ErrNotExist) {
		// Fallback: try to send signals to procs.
		_ = func() error {
			b, err := os.ReadFile(filepath.Join(cgPath, "cgroup.procs"))
			if err != nil {
				return err
			}
			for _, f := range bytes.Fields(b) {
				if pid, err := strconv.Atoi(string(f)); err == nil {
					_ = syscall.Kill(pid, syscall.SIGTERM)
					_ = syscall.Kill(pid, syscall.SIGKILL)
				}
			}
			return nil
		}()
	}

	// Try to remove; if it fails due to busy, caller can retry later.
	if err := os.Remove(cgPath); err != nil && !errors.Is(err, os.ErrNotExist) {
		return err
	}

	return nil
}
