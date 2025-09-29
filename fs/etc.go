//go:build linux

package fs

import (
	"fmt"
	"log/slog"
	"os"
	"path"

	"github.com/HQarroum/microbox/logger"
	"golang.org/x/sys/unix"
)

var defaultNameservers = []string{
	"8.8.8.8",
	"8.8.4.4",
}

/**
 * Set DNS resolvers in the sandbox /etc/resolv.conf.
 * @param base the root path of the sandbox filesystem.
 * @param nameservers the list of nameservers to set.
 * @return error if any, nil otherwise.
 */
func SetResolvers(base string, nameservers []string) error {
	var resolvContent string

	if base == "" {
		return unix.EINVAL
	}

	// Ensure /etc exists.
	if err := os.MkdirAll(path.Join(base, "/etc"), 0o755); err != nil {
		return fmt.Errorf("error creating /etc directory: %w", err)
	}

	// Check if /etc/resolv.conf exists and remove it if it is a symlink.
	resolvPath := path.Join(base, "/etc/resolv.conf")
	if info, err := os.Lstat(resolvPath); err == nil {
		if info.Mode()&os.ModeSymlink != 0 {
			if err := os.Remove(resolvPath); err != nil {
				return fmt.Errorf("error removing symlink /etc/resolv.conf: %w", err)
			}
		}
	}

	// Use default nameservers if none provided.
	if len(nameservers) == 0 {
		nameservers = defaultNameservers
	}

	// Write the provided nameservers.
	for _, ns := range nameservers {
		resolvContent += fmt.Sprintf("nameserver %s\n", ns)
	}

	// Here we don't bind-mount from the host, because we want the
	// sandbox to have its own resolv.conf, and its own DNS settings.
	// This avoids issues with the host's resolv.conf being incompatible
	// with the sandbox (e.g. using systemd-resolved's stub resolver, or local
	// DNS servers that are not accessible from within the sandbox).
	if err := os.WriteFile(resolvPath, []byte(resolvContent), 0o644); err != nil {
		return fmt.Errorf("error creating /etc/resolv.conf: %w", err)
	}

	return nil
}

/**
 * Setup essential /etc files in the sandbox rootfs.
 * @param base the root path of the sandbox filesystem.
 * @param opts the sandbox options.
 * @return error if any, nil otherwise.
 */
func SetupEtc(base string, nameservers []string, hostname string) error {
	if base == "" {
		return unix.EINVAL
	}

	// Ensure /etc exists.
	target := path.Join(base, "/etc")
	if err := os.MkdirAll(target, 0o755); err != nil {
		return err
	}

	// Create /etc/resolv.conf.
	if err := SetResolvers(base, nameservers); err != nil {
		logger.Log.Warn("error setting resolvers", slog.Any("err", err))
	}

	// Bind-mount /etc/hosts
	if _, err := os.Stat("/etc/hosts"); err == nil {
		spec := MountSpec{Host: "/etc/hosts", Dest: "/etc/hosts", RO: true}
		if err := BindMount(base, spec); err != nil {
			return fmt.Errorf("error binding /etc/hosts: %w", err)
		}
	}

	// Write the hostname if specified.
	if hostname != "" {
		hostnamePath := path.Join(base, "/etc/hostname")
		if err := os.WriteFile(hostnamePath, []byte(hostname+"\n"), 0o644); err != nil {
			logger.Log.Warn("error writing /etc/hostname", slog.Any("err", err))
		}
	}

	return nil
}
