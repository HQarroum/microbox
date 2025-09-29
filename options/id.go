package options

import (
	"fmt"

	"github.com/HQarroum/microbox/sandbox"
)

/**
 * Parse the user namespace mode from a string.
 * @param s the string to parse
 * @return the parsed user namespace mode and error if any
 */
func ParseUserNamespace(s string) (sandbox.UserNamespaceMode, error) {
	switch s {
	case "isolated":
		return sandbox.UserNamespaceIsolated, nil
	case "host":
		return sandbox.UserNamespaceHost, nil
	default:
		return sandbox.UserNamespaceError, fmt.Errorf("unknown user namespace mode: %q", s)
	}
}
