package fs

import (
	"os"
	"path"
)

// MountTmp ensures that the /tmp directory exists within the given base path
// and sets its permissions to be world-writable and sticky (mode 1777).
// This is important for many applications that expect /tmp to have these properties.
// If the base path is empty, the function does nothing and returns nil.
func MountTmp(base string) error {
	if base == "" {
		return nil
	}

	// Ensure /tmp exists.
	tmp := path.Join(base, "/tmp")
	if err := os.MkdirAll(tmp, 0o1777); err != nil {
		return err
	}

	// Make /tmp world-writable and sticky.
	return os.Chmod(tmp, 0o1777)
}
