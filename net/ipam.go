//go:build linux

package net

import (
	"bytes"
	"fmt"
	"net"
	"os"
	"path/filepath"
	"time"

	"github.com/apparentlymart/go-cidr/cidr"
	bolt "go.etcd.io/bbolt"
)

const (
	ipamDefaultDBPath = "/var/run/microbox/ipam.db"
)

/**
 * IpamOptions configures the IP allocator.
 */
type IpamOptions struct {
	SubnetCIDR string
	DBPath     string
	Reserved   []net.IP
}

/**
 * IpamAllocator represents a single allocated IP within a subnet.
 */
type IpamAllocator struct {
	// BoltDB file path.
	dbPath string

	// Bucket name for this subnet.
	bucket []byte

	// Networking subnet.
	subnet *net.IPNet

	// Prefix length.
	prefix int

	// Allocated IP address.
	ip net.IP

	// List of reserved IPs that should not be allocated.
	reserved map[string]struct{}
}

/**
 * AllocateIP returns a new allocator for the next free IP inside the given
 * subnet. The IP is reserved until Release() is called.
 * @param opts configuration
 * @return *IpamAllocator or error.
 */
func AllocateIP(opts IpamOptions) (*IpamAllocator, error) {
	if opts.SubnetCIDR == "" {
		return nil, fmt.Errorf("SubnetCIDR must be provided")
	}

	// Choose DB path.
	dbPath := opts.DBPath
	if dbPath == "" {
		dbPath = ipamDefaultDBPath
	}

	// Parse subnet.
	_, ipNet, err := net.ParseCIDR(opts.SubnetCIDR)
	if err != nil {
		return nil, fmt.Errorf("invalid subnet CIDR: %w", err)
	}
	if ipNet.IP.To4() == nil {
		return nil, fmt.Errorf("only IPv4 subnets supported")
	}
	prefixLen, _ := ipNet.Mask.Size()

	// Address range and reserved set (network, broadcast, plus user-specified).
	first, last := cidr.AddressRange(ipNet)
	reserved := map[string]struct{}{
		first.String(): {}, // network
		last.String():  {}, // broadcast
	}
	for _, r := range opts.Reserved {
		if r4 := r.To4(); r4 != nil {
			reserved[r4.String()] = struct{}{}
		}
	}

	// Open DB (short-lived), reserve first free address atomically, then close.
	if err := os.MkdirAll(filepath.Dir(dbPath), 0o755); err != nil {
		return nil, fmt.Errorf("ipam: mkdir: %w", err)
	}

	var picked net.IP
	if err := withDB(dbPath, func(db *bolt.DB) error {
		bucket := []byte(opts.SubnetCIDR)

		return db.Update(func(tx *bolt.Tx) error {
			bkt, err := tx.CreateBucketIfNotExists(bucket)
			if err != nil {
				return err
			}

			for cur := cidr.Inc(first); bytes.Compare(cur, last) < 0; cur = cidr.Inc(cur) {
				s := cur.String()
				if _, skip := reserved[s]; skip {
					// IP address is reserved.
					continue
				}
				if v := bkt.Get([]byte(s)); v != nil {
					// IP address is already allocated.
					continue
				}
				// Allocate this IP.
				if err := bkt.Put([]byte(s), []byte{1}); err != nil {
					return fmt.Errorf("reserve %s: %w", s, err)
				}
				picked = append(net.IP(nil), cur...) // copy
				return nil
			}
			return fmt.Errorf("no free IPs in %s", opts.SubnetCIDR)
		})
	}); err != nil {
		return nil, fmt.Errorf("ipam: open DB: %w", err)
	}

	return &IpamAllocator{
		dbPath:   dbPath,
		bucket:   []byte(opts.SubnetCIDR),
		subnet:   ipNet,
		prefix:   prefixLen,
		ip:       picked,
		reserved: reserved,
	}, nil
}

/**
 * @return the allocated IP in CIDR notation.
 */
func (ia *IpamAllocator) IP() string {
	return fmt.Sprintf("%s/%d", ia.ip.String(), ia.prefix)
}

/**
 * Release frees the allocated IP.
 * After release, the IpamAllocator should not be used.
 * It is safe to call Release multiple times.
 */
func (ia *IpamAllocator) Release() error {
	return withDB(ia.dbPath, func(db *bolt.DB) error {
		return db.Update(func(tx *bolt.Tx) error {
			bkt := tx.Bucket(ia.bucket)
			if bkt == nil {
				return nil
			}
			return bkt.Delete([]byte(ia.ip.String()))
		})
	})
}

/**
 * Helper to open BoltDB with a short timeout, run f, and close it.
 * This avoids holding an exclusive RW lock for the lifetime of the sandbox.
 */
func withDB(path string, f func(*bolt.DB) error) error {
	db, err := bolt.Open(path, 0o600, &bolt.Options{Timeout: 2 * time.Second})
	if err != nil {
		return err
	}
	defer func() {
		_ = db.Close()
	}()
	return f(db)
}
