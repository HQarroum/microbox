//go:build linux

package options

import (
	"fmt"
	"os"

	"github.com/HQarroum/microbox/fs"
)

/**
 * Parse filesystem mode from a string.
 * @param s the string to parse
 * @return the parsed FsMount and error if any
 */
func parseFsMode(s string) (fs.FsMount, error) {
	switch s {
	case "host":
		return fs.FsMount{Mode: fs.FsHost}, nil
	case "tmpfs":
		return fs.FsMount{Mode: fs.FsTmpfs}, nil
	default:
		fi, err := os.Lstat(s)

		// Check that the path exists.
		if err != nil {
			return fs.FsMount{}, fmt.Errorf("bad --fs %q: %v", s, err)
		}

		// Check if the path is a directory.
		if !fi.IsDir() {
			return fs.FsMount{}, fmt.Errorf("bad --fs %q: not a directory", s)
		}
		return fs.FsMount{Mode: fs.FsRootfs, Path: s}, nil
	}
}
