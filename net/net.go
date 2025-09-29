//go:build linux

package net

import (
	"fmt"
	stdnet "net"
	"os"
	"syscall"
	"time"

	"github.com/coreos/go-iptables/iptables"
	"github.com/vishvananda/netlink"
	"github.com/vishvananda/netns"
	"golang.org/x/sys/unix"
)

var (
	subnetCIDR  = "10.44.0.0/24"
	bridgeIp    = "10.44.0.1/24"
	reservedIPs = []stdnet.IP{{10, 44, 0, 1}}
)

/**
 * Network mode.
 */
type NetworkMode int

/**
 * @return a string representation of network mode.
 */
func (m NetworkMode) String() string {
	switch m {
	case NetNone:
		return "none"
	case NetHost:
		return "host"
	case NetBridge:
		return "bridge"
	default:
		return "unknown"
	}
}

/**
 * Network modes.
 */
const (
	NetNone NetworkMode = iota
	NetHost
	NetBridge
)

/**
 * Network configuration for setting up container networking.
 */
type NetworkConfig struct {
	ChildPID int
	Mode     NetworkMode
}

/**
 * The network stack setup result.
 */
type NetworkResult struct {
	IPAM *IpamAllocator
	// A function to cleanup networking resources.
	Cleanup func() error
}

/**
 * Enable IPv4 forwarding on the host.
 * @note must be run as root.
 * @return error if any, nil otherwise.
 */
func EnableIPv4Forwarding() error {
	const p = "/proc/sys/net/ipv4/ip_forward"

	if err := os.WriteFile(p, []byte("1\n"), 0o644); err != nil {
		return fmt.Errorf("enable ipv4 forwarding: %w", err)
	}
	return nil
}

/**
 * Sets up container networking according to the specified mode.
 * @param childPID the PID of the sandboxed process.
 * @param mode the network mode.
 * @return error if any, nil otherwise.
 */
func SetupContainerNetworking(cfg NetworkConfig) (*NetworkResult, error) {
	switch cfg.Mode {

	// Bridge networking with a veth pair.
	case NetBridge:
		ipam, err := AllocateIP(IpamOptions{
			SubnetCIDR: subnetCIDR,
			Reserved:   reservedIPs,
		})
		if err != nil {
			return nil, err
		}

		// Create veth pair, bridge, assign IP, setup iptables rules.
		cleanup, err := SetupVethNetworking(cfg.ChildPID, VethConfig{
			SubnetCIDR:  subnetCIDR,
			BridgeIP:    bridgeIp,
			ContainerIP: ipam.IP(),
			EnableNAT:   true,
		})
		if err != nil {
			return nil, err
		}

		return &NetworkResult{
			IPAM: ipam,
			Cleanup: func() error {
				// Release allocated IP.
				ipam.Release()
				// Cleanup veth and bridge.
				return cleanup()
			},
		}, nil

	// Unsupported.
	default:
		return nil, fmt.Errorf("unsupported network mode: %d", cfg.Mode)
	}
}

/**
 * Assigns the given CIDR address to the specified link.
 * @param link the network link
 * @param cidr the CIDR address
 */
func AssignAddr(link netlink.Link, cidr string) error {
	ip, ipnet, err := stdnet.ParseCIDR(cidr)
	if err != nil {
		return err
	}

	addr := &netlink.Addr{
		IPNet: &stdnet.IPNet{
			IP:   ip,
			Mask: ipnet.Mask,
		},
	}

	// Check if address is already assigned.
	addrs, _ := netlink.AddrList(link, unix.AF_INET)
	for _, a := range addrs {
		if a.IPNet.String() == addr.IPNet.String() {
			return nil
		}
	}

	// Assign address to link.
	if err := netlink.AddrAdd(link, addr); err != nil && err != syscall.EEXIST {
		return fmt.Errorf("addr add %s: %w", addr.IPNet, err)
	}
	return nil
}

/**
 * Helper to find the default network interface on the host.
 * @return the name of the default network interface on the host.
 */
func DefaultInterface() (string, error) {
	// Try route lookup to a well-known external IP.
	routes, err := netlink.RouteGet(stdnet.ParseIP("8.8.8.8"))
	if err == nil {
		for _, r := range routes {
			if r.LinkIndex != 0 {
				if l, err := netlink.LinkByIndex(r.LinkIndex); err == nil {
					return l.Attrs().Name, nil
				}
			}
		}
	}

	// Fallback: scan main routing table for default entry.
	filter := &netlink.Route{Table: unix.RT_TABLE_MAIN}
	all, err2 := netlink.RouteListFiltered(unix.AF_INET, filter, netlink.RT_FILTER_TABLE)
	if err2 != nil {
		return "", fmt.Errorf("route list: %w", err2)
	}
	for _, r := range all {
		if r.Dst == nil && r.LinkIndex != 0 {
			if l, err := netlink.LinkByIndex(r.LinkIndex); err == nil {
				return l.Attrs().Name, nil
			}
		}
	}
	return "", fmt.Errorf("default route interface not found")
}

/**
 * Configures the container interface inside the child network namespace by
 * setting its name, bringing it up, assigning IP, adding default route.
 *
 * @param childPID the PID of the containerized process (child).
 * @param tempName the temporary name of the interface inside the container (e.g. "cVETH1234")
 * @param finalName the final name of the interface inside the container (e.g. "eth0")
 * @param addrCIDR the IP address to assign to the interface (e.g. "10.44.0.2/24")
 * @param gwCIDR the gateway IP address (e.g. "10.44.0.1/24")
 */
func configureContainerInterface(childPID int, tempName, finalName, addrCIDR, gwCIDR string) error {
	// Save current netns.
	hostNS, err := netns.Get()
	if err != nil {
		return err
	}
	defer hostNS.Close()

	// Get child netns.
	targetNS, err := netns.GetFromPid(childPID)
	if err != nil {
		return err
	}
	defer targetNS.Close()

	// Switch to child netns.
	if err := netns.Set(targetNS); err != nil {
		return err
	}
	defer netns.Set(hostNS)

	// Wait for the device to appear to avoid ENODEV during configuration.
	link, err := waitLinkByName(tempName, 5000*time.Millisecond)
	if err != nil {
		return fmt.Errorf("wait veth %s in ns: %w", tempName, err)
	}

	if finalName != tempName {
		if err := netlink.LinkSetName(link, finalName); err != nil {
			return fmt.Errorf("rename %s->%s: %w", tempName, finalName, err)
		}
		link, err = waitLinkByName(finalName, 5000*time.Millisecond)
		if err != nil {
			return err
		}
	}

	// Bring lo up early.
	if lo, _ := netlink.LinkByName("lo"); lo != nil {
		_ = netlink.LinkSetUp(lo)
	}

	// Bring iface up **before** assigning address (avoids ENODEV on some drivers).
	if err := netlink.LinkSetUp(link); err != nil && err != syscall.EEXIST {
		return fmt.Errorf("link up: %w", err)
	}

	// Assign IP.
	if addrCIDR != "" {
		if err := AssignAddr(link, addrCIDR); err != nil {
			// Retry once after a short delay in case of transient race.
			time.Sleep(100 * time.Millisecond)
			if err2 := AssignAddr(link, addrCIDR); err2 != nil {
				return err
			}
		}
	}

	// Default route via bridge IP (0.0.0.0/0).
	if gwCIDR != "" {
		gwIP, _, err := stdnet.ParseCIDR(gwCIDR)
		if err != nil {
			return fmt.Errorf("parse gw %q: %w", gwCIDR, err)
		}
		route := &netlink.Route{
			LinkIndex: link.Attrs().Index,
			Scope:     netlink.SCOPE_UNIVERSE,
			Gw:        gwIP,
			Dst: &stdnet.IPNet{
				IP:   stdnet.IPv4zero,
				Mask: stdnet.IPv4Mask(0, 0, 0, 0),
			},
		}
		if err := netlink.RouteReplace(route); err != nil && err != syscall.EEXIST {
			return fmt.Errorf("default route via %s: %w", gwIP, err)
		}
	}

	return nil
}

/**
 * Add iptables rules for forwarding and NAT on the given interface.
 * @param iface the bridge interface name (e.g. "mbx0")
 * @param subnetCIDR the subnet CIDR (e.g. "10.0.0.0/24")
 */
func AddForwardingRules(iface, subnetCIDR string) error {
	ipt, err := iptables.New()
	if err != nil {
		return err
	}

	// Detect default host egress interface.
	defaultIf, err := DefaultInterface()
	if err != nil {
		return err
	}

	// Allow outbound forwarding from bridge -> default interface
	outRule := []string{"-i", iface, "-o", defaultIf, "-j", "ACCEPT"}
	if err := ensureIptRule(ipt, "filter", "FORWARD", outRule); err != nil {
		return err
	}

	// Allow return traffic back to the bridge.
	inRule := []string{"-i", defaultIf, "-o", iface, "-m", "conntrack", "--ctstate", "RELATED,ESTABLISHED", "-j", "ACCEPT"}
	if err := ensureIptRule(ipt, "filter", "FORWARD", inRule); err != nil {
		return err
	}

	// Optional: intra-bridge forwarding
	if subnetCIDR != "" {
		localRule := []string{"-i", iface, "-o", iface, "-s", subnetCIDR, "-d", subnetCIDR, "-j", "ACCEPT"}
		_ = ensureIptRule(ipt, "filter", "FORWARD", localRule)
	}

	return nil
}

/**
 * Add iptables MASQUERADE rule for outbound traffic from the given subnet.
 * @param iface the bridge interface name (e.g. "mbx0")
 * @param subnetCIDR the subnet CIDR (e.g. "10.0.0.0/24")
 */
func AddMasqueradeRule(iface, subnetCIDR string) error {
	if subnetCIDR == "" {
		return nil
	}
	ipt, err := iptables.New()
	if err != nil {
		return err
	}

	// MASQUERADE: -t nat -I POSTROUTING 1 -s <subnet> ! -o <bridge> -j MASQUERADE
	return ensureIptRule(ipt, "nat", "POSTROUTING", []string{
		"-s",
		subnetCIDR,
		"!",
		"-o",
		iface,
		"-j",
		"MASQUERADE"},
	)
}

func ensureIptRule(ipt *iptables.IPTables, table, chain string, rule []string) error {
	exists, err := ipt.Exists(table, chain, rule...)
	if err != nil {
		return fmt.Errorf("iptables exists %s/%s: %w", table, chain, err)
	}
	if exists {
		return nil
	}
	// Insert at the top to take precedence over Docker or other rules.
	if err := ipt.Insert(table, chain, 1, rule...); err != nil {
		return fmt.Errorf("iptables insert %s/%s %v: %w", table, chain, rule, err)
	}
	return nil
}

/**
 * Waits up to 'timeout' for a link by name to appear in the current netns.
 * @param name the interface name to wait for.
 * @param timeout the maximum wait time.
 * @return error if not found in time, nil otherwise.
 */
func waitLinkByName(name string, timeout time.Duration) (netlink.Link, error) {
	deadline := time.Now().Add(timeout)
	for {
		if link, err := netlink.LinkByName(name); err == nil {
			return link, nil
		}
		if time.Now().After(deadline) {
			break
		}
		time.Sleep(50 * time.Millisecond)
	}
	return nil, fmt.Errorf("link %q not found", name)
}
