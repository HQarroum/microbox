package sandbox

import (
	"bufio"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"os/user"
	"path/filepath"
	"strconv"
	"strings"
)

/**
 * User namespace modes.
 */
type UserNamespaceMode int

const (
	UserNamespaceError UserNamespaceMode = iota
	UserNamespaceIsolated
	UserNamespaceHost
)

/**
 * SetupIDMappings configures /proc/<pid>/{setgroups,uid_map,gid_map} for a child
 * created in a new user namespace. When running as root, we write a simple
 * 0 -> host UID/GID identity mapping. When rootless, we try to use newuidmap /
 * newgidmap with a range from /etc/subuid and /etc/subgid. This matches what
 * runc/podman do in practice.
 */
func SetupIdMappings(childPID int) error {
	if childPID <= 0 {
		return fmt.Errorf("invalid child pid: %d", childPID)
	}

	euid := os.Geteuid()
	egid := os.Getegid()

	// Paths to the procfs files for the child process.
	setgroupsPath := fmt.Sprintf("/proc/%d/setgroups", childPID)
	uidMapPath := fmt.Sprintf("/proc/%d/uid_map", childPID)
	gidMapPath := fmt.Sprintf("/proc/%d/gid_map", childPID)

	// Always try to write "deny" to setgroups before writing gid_map (required
	// on modern kernels for unprivileged writers).
	_ = os.WriteFile(setgroupsPath, []byte("deny"), 0o644)

	if euid == 0 {
		// Privileged path: simple one-to-one mapping (container root -> host uid 0).
		if err := writeMap(uidMapPath, 0, 0, 1); err != nil {
			return fmt.Errorf("write uid_map: %w", err)
		}
		if err := writeMap(gidMapPath, 0, 0, 1); err != nil {
			return fmt.Errorf("write gid_map: %w", err)
		}
		return nil
	}

	// Rootless path: prefer newuidmap/newgidmap with subid ranges.
	newUIDMap, errUID := exec.LookPath("newuidmap")
	newGIDMap, errGID := exec.LookPath("newgidmap")
	if errUID == nil && errGID == nil {
		usr, err := user.Current()
		if err != nil {
			return fmt.Errorf("user.Current: %w", err)
		}

		subUIDStart, subUIDLen, err := firstSubidRange("/etc/subuid", usr.Username)
		if err != nil {
			return fmt.Errorf("configure /etc/subuid (e.g. 'USERNAME:100000:65536') or run as root: %w", err)
		}
		subGIDStart, subGIDLen, err := firstSubidRange("/etc/subgid", usr.Username)
		if err != nil {
			return fmt.Errorf("configure /etc/subgid (e.g. 'USERNAME:100000:65536') or run as root: %w", err)
		}

		// Typical mapping used by rootless runtimes:
		// - Map container root (0) to the start of the user's subuid/subgid range,
		//   with the full length of that range.
		// - Also map the caller's real uid/gid to itself (length 1) so files owned
		//   by the user remain accessible.
		uidArgs := []string{
			strconv.Itoa(childPID),
			"0", strconv.Itoa(subUIDStart), strconv.Itoa(subUIDLen),
			strconv.Itoa(euid), strconv.Itoa(euid), "1",
		}
		gidArgs := []string{
			strconv.Itoa(childPID),
			"0", strconv.Itoa(subGIDStart), strconv.Itoa(subGIDLen),
			strconv.Itoa(egid), strconv.Itoa(egid), "1",
		}

		// Setgroups must be "deny" before writing gid_map as an unprivileged user.
		_ = os.WriteFile(setgroupsPath, []byte("deny"), 0o644)

		if out, err := exec.Command(newUIDMap, uidArgs...).CombinedOutput(); err != nil {
			return fmt.Errorf("newuidmap failed: %v\n%s", err, out)
		}
		if out, err := exec.Command(newGIDMap, gidArgs...).CombinedOutput(); err != nil {
			return fmt.Errorf("newgidmap failed: %v\n%s", err, out)
		}
		return nil
	}

	// Fallback (unprivileged and helpers missing): we cannot safely give root
	// inside the child. The kernel only allows an unprivileged writer to set
	// a *single* mapping where insideID == outsideID == caller's euid/egid.
	// This means there is no 0-mapping, so the child won't have root-like caps.
	// Return a helpful error instead of silently creating a crippled container.
	return errors.New(
		"rootless ID mapping requires newuidmap/newgidmap (shadow-utils). " +
			"Install them or run as root")
}

/**
 * Write a single ID mapping to the specified procfs map file.
 * @param path the path to the uid_map or gid_map file
 * @param inside the starting ID inside the namespace
 * @param outside the starting ID outside the namespace
 * @param length the number of IDs to map
 * @return error if any, nil on success
 */
func writeMap(path string, inside, outside, length int) error {
	line := fmt.Sprintf("%d %d %d\n", inside, outside, length)

	if _, err := os.Stat(filepath.Dir(path)); err != nil {
		return err
	}
	return os.WriteFile(path, []byte(line), 0o644)
}

/**
 * Find the first subid range for a given username in /etc/subuid or /etc/subgid.
 * @param file the path to the subid file (/etc/subuid or /etc/subgid)
 * @param username the username to look for
 * @return start, length, error if any
 */
func firstSubidRange(file, username string) (start, length int, err error) {
	f, err := os.Open(file)
	if err != nil {
		return 0, 0, fmt.Errorf("open %s: %w", file, err)
	}
	defer func() {
		_ = f.Close()
	}()

	sc := bufio.NewScanner(f)
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		parts := strings.Split(line, ":")
		if len(parts) != 3 {
			continue
		}
		if parts[0] != username {
			continue
		}
		start64, err1 := strconv.ParseInt(parts[1], 10, 64)
		len64, err2 := strconv.ParseInt(parts[2], 10, 64)
		if err1 != nil || err2 != nil || start64 < 0 || len64 <= 0 {
			continue
		}
		return int(start64), int(len64), nil
	}
	if err := sc.Err(); err != nil {
		return 0, 0, fmt.Errorf("scan %s: %w", file, err)
	}
	return 0, 0, fmt.Errorf("no %s entry for user %q", filepath.Base(file), username)
}
