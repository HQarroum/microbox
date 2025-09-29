//go:build linux

package options

import (
	"fmt"

	"github.com/HQarroum/microbox/net"
)

/**
 * Parse network mode from the given string.
 * @param s the string to parse
 * @return the parsed NetworkMode
 */
func parseNetMode(s string) (net.NetworkMode, error) {
	switch s {
	case "none":
		return net.NetNone, nil
	case "host":
		return net.NetHost, nil
	case "bridge":
		return net.NetBridge, nil
	default:
		return net.NetNone, fmt.Errorf("bad --net %q (none|host|bridge)", s)
	}
}
