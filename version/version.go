package version

import (
	"fmt"
)

const (
	majorVersion = "0"
	minorVersion = "2"
	patchVersion = "0"
)

/**
 * Returns the version of the microbox-go package.
 */
func Version() string {
	return fmt.Sprintf("%s.%s.%s", majorVersion, minorVersion, patchVersion)
}

/**
 * Returns the version details (major, minor, patch)
 */
func VersionDetails() (string, string, string) {
	return majorVersion, minorVersion, patchVersion
}
