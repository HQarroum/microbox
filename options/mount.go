//go:build linux

package options

import (
	"fmt"
	"path/filepath"
	"strings"

	"github.com/HQarroum/microbox/fs"
)

/**
 * Parse a mount specification string.
 * @param spec the mount specification (HOST:DEST)
 * @param ro whether the mount is read-only
 * @return the parsed MountSpec and error if any
 */
func parseMount(spec string, ro bool) (fs.MountSpec, error) {
	parts := strings.SplitN(spec, ":", 2)

	if len(parts) != 2 || parts[0] == "" || parts[1] == "" {
		return fs.MountSpec{}, fmt.Errorf("bad mount %q (HOST:DEST)", spec)
	}
	if !filepath.IsAbs(parts[1]) {
		return fs.MountSpec{}, fmt.Errorf("DEST must be absolute: %q", spec)
	}
	return fs.MountSpec{
		Host: parts[0],
		Dest: parts[1],
		RO:   ro,
	}, nil
}
