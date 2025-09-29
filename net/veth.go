//go:build linux

package net

import (
	"fmt"
	"os"
	"syscall"

	"github.com/vishvananda/netlink"
)

const (
	vethDefaultBridgeName  = "mbx0"
	vethDefaultContainerIf = "eth0"
	vethDefaultMTU         = 1500
)

/**
 * Network configuration for bridged networking.
 */
type VethConfig struct {
	// Bridge interface name (e.g. "mbx0").
	BridgeName string

	// Subnet CIDR (e.g. "10.44.0.0/24")
	SubnetCIDR string

	// Bridge IP address (e.g. "10.44.0.1/24")
	BridgeIP string

	// Container IP address (e.g. "10.44.0.10/24")
	ContainerIP string

	// Name of the interface inside the container (e.g. "eth0")
	ContainerIf string

	// Maximum Transmission Unit (e.g. 1500)
	MTU int

	// Whether to enable NAT for outbound traffic
	EnableNAT bool
}

/**
 * Setup container networking with a bridge and veth pair.
 * @note must be run as root (CAP_NET_ADMIN).
 * @param childPID the PID of the containerized process (child).
 * @param cfg the network configuration.
 * @return error if any, nil otherwise.
 */
func SetupVethNetworking(childPID int, cfg VethConfig) (func() error, error) {
	if cfg.BridgeName == "" {
		cfg.BridgeName = vethDefaultBridgeName
	}
	if cfg.ContainerIf == "" {
		cfg.ContainerIf = vethDefaultContainerIf
	}
	if cfg.MTU == 0 {
		cfg.MTU = vethDefaultMTU
	}

	// Create the bridge interface on the host if it doesn't exist.
	bridge, err := CreateBridge(cfg.BridgeName, cfg.BridgeIP, cfg.MTU)
	if err != nil {
		return nil, fmt.Errorf("create bridge: %w", err)
	}

	// Create veth pair, and move one end to the sandbox namespace.
	hostIf, contIfTemp, err := CreateVethPair(bridge, cfg, childPID)
	if err != nil {
		return nil, fmt.Errorf("veth setup: %w", err)
	}

	// Configure container interface.
	if err := configureContainerInterface(childPID, contIfTemp, cfg.ContainerIf, cfg.ContainerIP, cfg.BridgeIP); err != nil {
		return nil, fmt.Errorf("configure container iface: %w", err)
	}

	// Set host veth UP.
	if err := netlink.LinkSetUp(hostIf); err != nil {
		return nil, fmt.Errorf("host veth up: %w", err)
	}

	// Host forwarding + iptables NAT/FORWARD rules
	if cfg.EnableNAT {
		if err := EnableIPv4Forwarding(); err != nil {
			return nil, err
		}
		if err := AddForwardingRules(cfg.BridgeName, cfg.SubnetCIDR); err != nil {
			return nil, err
		}
		if err := AddMasqueradeRule(cfg.BridgeName, cfg.SubnetCIDR); err != nil {
			return nil, err
		}
	}

	// Cleanup function to remove the veth pair and associated resources.
	cleanup := func() error {
		if err := netlink.LinkDel(hostIf); err != nil && !os.IsNotExist(err) {
			return fmt.Errorf("delete host veth: %w", err)
		}

		return nil
	}

	return cleanup, nil
}

/**
 * Creates a new Linux bridge interface with the given name and CIDR
 * if it does not already exist.
 * @param name the bridge interface name
 * @param cidr the bridge CIDR address
 * @param mtu the bridge MTU
 * @return the bridge link, or error if any.
 */
func CreateBridge(name, cidr string, mtu int) (netlink.Link, error) {
	if l, err := netlink.LinkByName(name); err == nil {
		if err := netlink.LinkSetUp(l); err != nil {
			return nil, err
		}
		if cidr != "" {
			if err := AssignAddr(l, cidr); err != nil {
				return nil, err
			}
		}
		return l, nil
	}

	// Bridge attributes.
	bridge := &netlink.Bridge{
		LinkAttrs: netlink.LinkAttrs{
			Name: name,
			MTU:  mtu,
		},
	}

	// Create a new bridge interface if it doesn't exist.
	if err := netlink.LinkAdd(bridge); err != nil && !os.IsExist(err) {
		return nil, err
	}

	// Turn the bridge interface up.
	if err := netlink.LinkSetUp(bridge); err != nil {
		return nil, err
	}

	// Assign the CIDR address if given.
	if cidr != "" {
		if err := AssignAddr(bridge, cidr); err != nil {
			return nil, err
		}
	}

	return bridge, nil
}

/**
 * Creates a veth pair, with one end attached to the given bridge,
 * and the other end moved to the network namespace of the given PID.
 * @param bridge the bridge link to attach the host end to.
 * @param cfg the network configuration.
 * @param childPID the PID of the containerized process.
 * @return the host-side link, the name of the peer in the child netns, or error if any.
 */
func CreateVethPair(bridge netlink.Link, cfg VethConfig, childPID int) (netlink.Link, string, error) {
	hostName := fmt.Sprintf("vmbx%d", childPID)
	peerName := fmt.Sprintf("c%s", hostName)

	v := &netlink.Veth{
		LinkAttrs: netlink.LinkAttrs{
			Name:        hostName,
			MTU:         cfg.MTU,
			MasterIndex: bridge.Attrs().Index,
		},
		PeerName: peerName,
	}

	// Create the veth pair.
	if err := netlink.LinkAdd(v); err != nil && err != syscall.EEXIST {
		return nil, "", err
	}

	// Lookup the host interface.
	hostIf, err := netlink.LinkByName(hostName)
	if err != nil {
		return nil, "", err
	}

	// Lookup the peer interface.
	peerIf, err := netlink.LinkByName(peerName)
	if err != nil {
		return nil, "", err
	}

	// Ensure host veth is enslaved to bridge and UP.
	if hostIf.Attrs().MasterIndex != bridge.Attrs().Index {
		if err := netlink.LinkSetMaster(hostIf, bridge); err != nil && err != syscall.EEXIST {
			return nil, "", fmt.Errorf("attach host veth to bridge: %w", err)
		}
	}

	// Bring up the host side.
	if err := netlink.LinkSetUp(hostIf); err != nil && err != syscall.EEXIST {
		return nil, "", err
	}

	// Move peer to the sandbox namespace.
	if err := netlink.LinkSetNsPid(peerIf, childPID); err != nil {
		return nil, "", err
	}

	return hostIf, peerName, nil
}
