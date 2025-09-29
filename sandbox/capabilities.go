//go:build linux

package sandbox

import (
	"fmt"
	"strings"

	"github.com/moby/sys/capability"
)

/**
 * Default capabilities allow-list matching Docker/runc defaults.
 */
var defaultCaps = []string{
	"CAP_CHOWN", "CAP_DAC_OVERRIDE", "CAP_FSETID", "CAP_FOWNER",
	"CAP_MKNOD", "CAP_NET_RAW", "CAP_SETGID", "CAP_SETUID",
	"CAP_SETFCAP", "CAP_SETPCAP", "CAP_NET_BIND_SERVICE",
	"CAP_SYS_CHROOT", "CAP_KILL", "CAP_AUDIT_READ", "CAP_AUDIT_WRITE",
}

/**
 * A tiny set type for capabilities.
 */
type CapSet map[capability.Cap]struct{}

/**
 * Capability options for a sandbox.
 */
type CapabilityOpts struct {
	// List of capabilities to add.
	Add CapSet `json:"add"`

	// List of capabilities to drop.
	Drop CapSet `json:"drop"`
}

/**
 * Create a new capability set initialized with the given capabilities.
 * @param ids the capability IDs to include
 * @return the new capability set
 */
func NewCapSet(ids ...capability.Cap) CapSet {
	cs := make(CapSet, len(ids))
	cs.Add(ids...)
	return cs
}

/**
 * Add capabilities to the set.
 * @param ids the capability IDs to add
 */
func (cs CapSet) Add(ids ...capability.Cap) {
	for _, id := range ids {
		cs[id] = struct{}{}
	}
}

/**
 * Remove capabilities from the set.
 * @param ids the capability IDs to remove
 */
func (cs CapSet) Remove(ids ...capability.Cap) {
	for _, id := range ids {
		delete(cs, id)
	}
}

/**
 * Copy the capability set.
 * @return the slice of capability IDs
 */
func (cs CapSet) Slice() []capability.Cap {
	out := make([]capability.Cap, 0, len(cs))
	for id := range cs {
		out = append(out, id)
	}
	return out
}

/**
 * Converts a capability name to a lowercase and without "CAP_" prefix.
 * @param cap the capability name to normalize
 * @return the normalized capability name
 */
func NormalizeCap(cap string) string {
	s := strings.TrimSpace(strings.ToLower(cap))
	s = strings.TrimPrefix(s, "cap_")
	return s
}

/**
 * Map of capability names to their IDs.
 */
var capNameToID = func() map[string]capability.Cap {
	m := make(map[string]capability.Cap)
	for _, c := range capability.ListKnown() {
		m[c.String()] = c
	}
	return m
}()

/**
 * Convert a capability name to its ID.
 * @param cap the capability name
 * @return the capability ID, or an error if the name is unknown
 */
func FromCapability(cap string) (capability.Cap, error) {
	cap = NormalizeCap(cap)
	if id, ok := capNameToID[cap]; ok {
		return id, nil
	} else {
		return 0, fmt.Errorf("unknown capability: %q", cap)
	}
}

/**
 * Convert a list of capability names to their IDs.
 * @param caps the list of capability names
 * @return the list of capability IDs, or an error if any name is unknown
 */
func FromCapabilities(caps []string) ([]capability.Cap, error) {
	var out []capability.Cap
	for _, cap := range caps {
		id, err := FromCapability(cap)
		if err != nil {
			return nil, err
		}
		out = append(out, id)
	}
	return out, nil
}

/**
 * BuildCapSets computes the effective capability sets given the defaults
 * plus user additions/drops.
 * @return a map of capability sets by type, or an error if any capability is unknown
 */
func (o *CapabilityOpts) BuildCapSets() (map[capability.CapType][]capability.Cap, error) {
	defCaps, err := FromCapabilities(defaultCaps)
	if err != nil {
		return nil, err
	}
	capSet := NewCapSet(defCaps...)

	// Apply drops.
	if len(o.Drop) > 0 {
		capSet.Remove(o.Drop.Slice()...)
	}

	// Apply adds.
	if len(o.Add) > 0 {
		capSet.Add(o.Add.Slice()...)
	}

	final := capSet.Slice()
	return map[capability.CapType][]capability.Cap{
		capability.BOUNDING:    final,
		capability.PERMITTED:   final,
		capability.EFFECTIVE:   final,
		capability.INHERITABLE: final,
	}, nil
}

/**
 * Apply applies the computed capability sets to the current process.
 * It clears existing caps and sets only those returned by BuildCapSets.
 */
func (o *CapabilityOpts) Apply() error {
	capsByType, err := o.BuildCapSets()
	if err != nil {
		return err
	}

	// Get a capability handler for the current process (pid=0).
	caps, err := capability.NewPid2(0)
	if err != nil {
		return fmt.Errorf("error getting process capabilities: %w", err)
	}

	// Apply bounding set first.
	caps.Clear(capability.BOUNDS)
	caps.Set(capability.BOUNDING, capsByType[capability.BOUNDING]...)

	// Replace all other sets.
	caps.Clear(capability.CAPS)
	caps.Set(capability.PERMITTED, capsByType[capability.PERMITTED]...)
	caps.Set(capability.EFFECTIVE, capsByType[capability.EFFECTIVE]...)
	caps.Set(capability.INHERITABLE, capsByType[capability.INHERITABLE]...)

	// Drop ambient capabilities.
	caps.Clear(capability.AMBIENT)

	if err := caps.Apply(capability.CAPS | capability.BOUNDS | capability.AMBIENT); err != nil {
		return fmt.Errorf("set capabilities: %w", err)
	}

	return nil
}
