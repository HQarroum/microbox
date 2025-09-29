//go:build linux

package options

import (
	"fmt"
	"sort"
	"strings"

	"github.com/HQarroum/microbox/sandbox"
)

/**
 * Default environment variables to use as a baseline
 * for sandboxed processes.
 */
var defaultEnvironment = map[string]string{
	"PATH": "/usr/bin:/bin:/usr/sbin:/sbin:/usr/local/bin",
	"HOME": "/root",
	"TERM": "xterm",
	"LANG": "C.UTF-8",
}

/**
 * Merge default environment variables with user provided ones.
 * @param defaults the default environment variables
 * @param user the user provided environment variables
 * @return the merged environment variables
 */
func MergeEnv(defaults map[string]string, user []sandbox.EnvVar) []sandbox.EnvVar {
	merged := make(map[string]string, len(defaults)+len(user))

	// Start with default environment variables.
	for k, v := range defaults {
		merged[k] = v
	}

	// Apply user overrides.
	for _, e := range user {
		merged[e.Key] = e.Val
	}

	// Build a stable []EnvVar:
	out := make([]sandbox.EnvVar, 0, len(merged))

	// keep default key order first (but with possibly overridden values)…
	seen := make(map[string]struct{}, len(merged))
	for _, k := range []string{"PATH", "HOME", "TERM", "LANG"} {
		if v, ok := merged[k]; ok {
			out = append(out, sandbox.EnvVar{Key: k, Val: v})
			seen[k] = struct{}{}
		}
	}
	// …then append any extra user keys deterministically
	extras := make([]string, 0, len(merged))
	for k := range merged {
		if _, ok := seen[k]; !ok {
			extras = append(extras, k)
		}
	}
	sort.Strings(extras)
	for _, k := range extras {
		out = append(out, sandbox.EnvVar{Key: k, Val: merged[k]})
	}
	return out
}

/**
 * Parse an environment variable specification string.
 * @param kv the environment variable specification (KEY=VALUE)
 * @return the parsed EnvVar and error if any
 */
func ParseEnv(kv string) (sandbox.EnvVar, error) {
	k, v, ok := strings.Cut(kv, "=")

	if !ok || k == "" {
		return sandbox.EnvVar{}, fmt.Errorf("bad --env %q (KEY=VALUE)", kv)
	}
	return sandbox.EnvVar{Key: k, Val: v}, nil
}
