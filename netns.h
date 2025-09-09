#ifndef MICROBOX_NETNS_H
#define MICROBOX_NETNS_H

#include <sys/types.h>
#include <stdint.h>

/**
 * The default bridge IP address net (172.20.0.x).
 */
#define MICROBOX_DEFAULT_BRIDGE_IP_SUBNET 0xAC140000

/**
 * Network configuration structure
 */
typedef struct {
    char bridge_name[16];       // Bridge interface name (e.g., "microbox0")
    char veth_host[16];         // Host veth name (e.g., "mbx0h")
    char veth_container[16];    // Container veth name (e.g., "mbx0c")
    uint32_t container_ip;      // Container IP (network byte order)
    uint32_t bridge_ip;         // Bridge IP (network byte order)
    uint8_t prefix_len;         // Subnet prefix length (e.g., 24)
    int netns_fd;               // Network namespace file descriptor
} netns_config_t;

/**
 * Setup bridged networking for container
 * @param config Network configuration
 * @return 0 on success, -1 on failure
 */
int microbox_setup_bridge_network(netns_config_t *config);

/**
 * Configure container network interface
 * @param config Network configuration
 * @return 0 on success, -1 on failure
 */
int microbox_configure_container_network(const netns_config_t *config);

/**
 * Generate unique network interface names and IP addresses
 * @param config Network configuration to populate
 * @param container_id Unique container identifier
 * @return 0 on success, -1 on failure
 */
int generate_interface_names(netns_config_t *config, pid_t container_id);

/**
 * Move container veth to container network namespace
 * @param config Network configuration
 * @param container_pid Container process ID
 * @return 0 on success, -1 on failure
 */
int move_veth_to_container(const netns_config_t *config, pid_t container_pid);

/**
 * Clean up network interfaces for a container
 * @param container_id Container identifier used to generate interface names
 * @return 0 on success, -1 on failure
 */
int microbox_cleanup_network_interfaces(pid_t container_id);

#endif /* MICROBOX_NETNS_H */
