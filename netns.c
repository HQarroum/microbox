#include "netns.h"
#include "netlink.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/rtnetlink.h>
#include <linux/if.h>
#include <linux/veth.h>

/**
 * Generate unique network interface names
 * @param config Network configuration to populate
 * @param container_id Unique container identifier
 * @return 0 on success, -1 on failure
 */
int generate_interface_names(netns_config_t *config, pid_t container_id) {
    int interface_id = container_id % 254;

    snprintf(config->bridge_name, sizeof(config->bridge_name), "microbox0");
    snprintf(config->veth_host, sizeof(config->veth_host), "mbx%dh", interface_id);
    snprintf(config->veth_container, sizeof(config->veth_container), "mbx%dc", interface_id);

    // Simple IP allocation: 172.20.0.0/16 subnet
    // Bridge IP: 172.20.0.1
    config->bridge_ip = htonl(MICROBOX_DEFAULT_BRIDGE_IP_SUBNET + 1);
    config->container_ip = htonl(MICROBOX_DEFAULT_BRIDGE_IP_SUBNET + interface_id + 2);
    config->prefix_len = 16;

    return 0;
}

/**
 * Clean up existing NAT rules for bridge
 * @param config Network configuration
 * @param use_nft Whether to use nftables (1) or iptables (0)
 */
static void cleanup_nat_rules(const netns_config_t *config, int use_nft) {
    char cmd[512];
    char bridge_subnet[32];

    // Calculate subnet for cleanup
    uint32_t subnet = ntohl(config->bridge_ip) & (0xFFFFFFFF << (32 - config->prefix_len));
    struct in_addr subnet_addr = { .s_addr = htonl(subnet) };
    snprintf(bridge_subnet, sizeof(bridge_subnet), "%s/%d", inet_ntoa(subnet_addr), config->prefix_len);

    if (use_nft) {
        // Clean nftables rules (flush chains)
        system("nft flush chain nat postrouting 2>/dev/null || true");
        system("nft flush chain filter forward 2>/dev/null || true");
    } else {
        // Clean iptables rules - remove our specific rules
        snprintf(cmd, sizeof(cmd), "iptables -t nat -D POSTROUTING -s %s ! -d %s -j MASQUERADE 2>/dev/null || true", bridge_subnet, bridge_subnet);
        system(cmd);

        // Remove forward rules for this bridge (may exist multiple times)
        snprintf(cmd, sizeof(cmd), "while iptables -D FORWARD -i %s -j ACCEPT 2>/dev/null; do :; done", config->bridge_name);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "while iptables -D FORWARD -o %s -j ACCEPT 2>/dev/null; do :; done", config->bridge_name);
        system(cmd);
    }
}

/**
 * Setup NAT/masquerading for bridge network
 * @param config Network configuration
 * @return 0 on success, -1 on failure
 */
static int setup_nat(const netns_config_t *config) {
    char cmd[512];
    char bridge_subnet[32];
    char default_iface[16] = {0};

    // Calculate subnet (e.g., "172.20.0.0/16")
    uint32_t subnet = ntohl(config->bridge_ip) & (0xFFFFFFFF << (32 - config->prefix_len));
    struct in_addr subnet_addr = { .s_addr = htonl(subnet) };
    snprintf(bridge_subnet, sizeof(bridge_subnet), "%s/%d", inet_ntoa(subnet_addr), config->prefix_len);

    // Detect default route interface using netlink (no system calls)
    netlink_handle_t route_handle;
    if (netlink_open(&route_handle) == 0) {
        if (netlink_get_default_interface(&route_handle, default_iface, sizeof(default_iface)) != 0) {
            fprintf(stderr, "Warning: Could not detect default interface via netlink, using eth0\n");
            strcpy(default_iface, "eth0");
        }
        netlink_close(&route_handle);
    } else {
        fprintf(stderr, "Warning: Could not open netlink for route detection, using eth0\n");
        strcpy(default_iface, "eth0");
    }

    // NAT setup runs on host side, should have proper privileges

    // Enable IP forwarding - try multiple methods for portability
    int forwarding_enabled = 0;

    // Direct file I/O (no system calls)
    FILE *fp = fopen("/proc/sys/net/ipv4/ip_forward", "w");
    if (fp) {
        if (fwrite("1", 1, 1, fp) == 1) {
            forwarding_enabled = 1;
        }
        fclose(fp);
    }

    if (!forwarding_enabled) {
        fprintf(stderr, "Warning: Could not enable IP forwarding - NAT may not work\n");
    }

    // Detect available firewall system using file access (no system calls)
    int use_nft = 0;
    int has_iptables = (access("/usr/sbin/iptables", X_OK) == 0 || access("/sbin/iptables", X_OK) == 0);
    int has_nft = (access("/usr/sbin/nft", X_OK) == 0 || access("/sbin/nft", X_OK) == 0);

    if (has_iptables) {
        use_nft = 0;
    } else if (has_nft) {
        use_nft = 1;
    } else {
        fprintf(stderr, "Error: Neither iptables nor nftables found\n");
        fprintf(stderr, "Install either: sudo apt install iptables OR sudo apt install nftables\n");
        return -1;
    }

    // Clean up any existing rules before adding new ones
    cleanup_nat_rules(config, use_nft);

    if (use_nft) {
        // Use nftables commands

        // Create table and chains if they don't exist
        system("nft add table nat 2>/dev/null || true");
        system("nft add chain nat postrouting { type nat hook postrouting priority 100\\; } 2>/dev/null || true");
        system("nft add table filter 2>/dev/null || true");
        system("nft add chain filter forward { type filter hook forward priority 0\\; } 2>/dev/null || true");

        // Add NAT rule
        snprintf(cmd, sizeof(cmd), "nft add rule nat postrouting ip saddr %s masquerade", bridge_subnet);
        if (system(cmd) != 0) {
            fprintf(stderr, "Warning: Failed to add nft NAT rule\n");
        }

        // Add forwarding rules
        snprintf(cmd, sizeof(cmd), "nft add rule filter forward iif %s accept", config->bridge_name);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "nft add rule filter forward oif %s accept", config->bridge_name);
        system(cmd);

    } else {
        // Use iptables commands
        snprintf(cmd, sizeof(cmd), "iptables -t nat -A POSTROUTING -s %s ! -d %s -j MASQUERADE", bridge_subnet, bridge_subnet);
        if (system(cmd) != 0) {
            fprintf(stderr, "Warning: Failed to add iptables NAT rule\n");
        }

        // Allow forwarding - insert at TOP to bypass Docker rules
        // Insert at position 1 (before Docker rules) - allow outbound from bridge
        snprintf(cmd, sizeof(cmd), "iptables -I FORWARD 1 -i %s -o %s -j ACCEPT", config->bridge_name, default_iface);
        system(cmd);

        // Insert at position 1 - allow return traffic to bridge
        snprintf(cmd, sizeof(cmd), "iptables -I FORWARD 1 -i %s -o %s -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT", default_iface, config->bridge_name);
        system(cmd);
    }

    return 0;
}

/**
 * Create bridge interface if it doesn't exist
 * @param config Network configuration
 * @return 0 on success, -1 on failure
 */
static int ensure_bridge_exists(const netns_config_t *config) {
    netlink_handle_t nl_handle;

    // Check if bridge already exists using netlink
    netlink_handle_t check_handle;
    if (netlink_open(&check_handle) == 0) {
        if (netlink_interface_exists(&check_handle, config->bridge_name) == 1) {
            printf("[DEBUG] ensure_bridge_exists: Bridge %s already exists, but setting up NAT anyway\n", config->bridge_name);
            fflush(stdout);
            netlink_close(&check_handle);
            // Bridge exists, but we still need to setup NAT rules
            goto setup_nat;
        }
        netlink_close(&check_handle);
    }

    // Initialize netlink handle for bridge creation
    if (netlink_open(&nl_handle) != 0) {
        fprintf(stderr, "Failed to open netlink socket: %s\n", strerror(errno));
        return -1;
    }

    // Create bridge using netlink
    if (netlink_create_bridge(&nl_handle, config->bridge_name) != 0) {
        fprintf(stderr, "Failed to create bridge %s: %s\n", config->bridge_name, strerror(errno));
        netlink_close(&nl_handle);
        return -1;
    }

    // Set bridge IP using netlink
    if (netlink_add_ip_address(&nl_handle, config->bridge_name, config->bridge_ip, config->prefix_len) != 0) {
        fprintf(stderr, "Failed to set bridge IP: %s\n", strerror(errno));
        netlink_close(&nl_handle);
        return -1;
    }

    // Bring bridge up using netlink
    if (netlink_set_interface_up(&nl_handle, config->bridge_name) != 0) {
        fprintf(stderr, "Failed to bring bridge up: %s\n", strerror(errno));
        netlink_close(&nl_handle);
        return -1;
    }

    netlink_close(&nl_handle);

setup_nat:
    // Setup NAT for internet access
    if (setup_nat(config) != 0) {
        fprintf(stderr, "ERROR: Failed to setup NAT rules\n");
        return -1; // Fail if NAT setup fails
    }

    return 0;
}

/**
 * Setup veth pair and connect to bridge
 * @param config Network configuration
 * @return 0 on success, -1 on failure
 */
static int setup_veth_pair(const netns_config_t *config) {
    netlink_handle_t nl_handle;

    // Initialize netlink handle
    if (netlink_open(&nl_handle) != 0) {
        fprintf(stderr, "Failed to open netlink socket: %s\n", strerror(errno));
        return -1;
    }

    // Create veth pair using netlink (with NLM_F_ACK fix)
    if (netlink_create_veth_pair(&nl_handle, config->veth_host, config->veth_container) != 0) {
        fprintf(stderr, "Failed to create veth pair: %s\n", strerror(errno));
        netlink_close(&nl_handle);
        return -1;
    }

    // Add host veth to bridge using netlink (with fixed interface lookup)
    if (netlink_set_interface_master(&nl_handle, config->veth_host, config->bridge_name) != 0) {
        fprintf(stderr, "Failed to set veth master: %s\n", strerror(errno));
        netlink_close(&nl_handle);
        return -1;
    }

    // Bring up host veth using netlink
    if (netlink_set_interface_up(&nl_handle, config->veth_host) != 0) {
        fprintf(stderr, "Failed to bring veth up: %s\n", strerror(errno));
        netlink_close(&nl_handle);
        return -1;
    }

    netlink_close(&nl_handle);
    return 0;
}

/**
 * Clean up network interfaces for a container
 * @param container_id Container identifier used to generate interface names
 * @return 0 on success, -1 on failure
 */
int microbox_cleanup_network_interfaces(pid_t container_id) {
    netlink_handle_t nl_handle;
    netns_config_t config;
    int ret = 0;

    // Generate the interface names using the same logic as setup
    if (generate_interface_names(&config, container_id) != 0) {
        fprintf(stderr, "Failed to generate interface names for cleanup\n");
        return -1;
    }

    // Open netlink socket
    if (netlink_open(&nl_handle) != 0) {
        fprintf(stderr, "Failed to open netlink socket for cleanup: %s\n", strerror(errno));
        return -1;
    }

    // Delete the host-side veth interface (this will automatically remove both ends of the pair)
    if (netlink_delete_interface(&nl_handle, config.veth_host) != 0) {
        fprintf(stderr, "Warning: Failed to delete veth interface %s: %s\n",
                config.veth_host, strerror(errno));
        ret = -1; // Mark as failed but continue cleanup
    }

    netlink_close(&nl_handle);
    return ret;
}

/**
 * Move container veth to container network namespace
 * @param config Network configuration
 * @param container_pid Container process ID
 * @return 0 on success, -1 on failure
 */
int move_veth_to_container(const netns_config_t *config, pid_t container_pid) {
    netlink_handle_t nl_handle;

    // Initialize netlink handle
    if (netlink_open(&nl_handle) != 0) {
        fprintf(stderr, "Failed to open netlink socket: %s\n", strerror(errno));
        return -1;
    }

    // Move container veth to container netns using netlink
    if (netlink_move_interface_to_netns(&nl_handle, config->veth_container, container_pid) != 0) {
        fprintf(stderr, "Failed to move veth to container: %s\n", strerror(errno));
        netlink_close(&nl_handle);
        return -1;
    }

    netlink_close(&nl_handle);
    return 0;
}

/**
 * Setup bridged networking for container
 * @param config Network configuration
 * @return 0 on success, -1 on failure
 */
int microbox_setup_bridge_network(netns_config_t *config) {
    if (ensure_bridge_exists(config) != 0) {
        printf("[DEBUG] ensure_bridge_exists() FAILED\n");
        fflush(stdout);
        return -1;
    }

    if (setup_veth_pair(config) != 0) {
        printf("[DEBUG] setup_veth_pair() FAILED\n");
        fflush(stdout);
        return -1;
    }

    return 0;
}

/**
 * Configure container network interface (called from inside container)
 * @param config Network configuration
 * @return 0 on success, -1 on failure
 */
int microbox_configure_container_network(const netns_config_t *config) {
    char actual_interface[32] = {0};
    char cmd[256];

    // Wait for interface to appear
    usleep(200000); // 200ms

    // Find the actual veth interface (any mbx*c interface)
    snprintf(cmd, sizeof(cmd), "ip link show | grep 'mbx.*c@' | awk '{print $2}' | cut -d: -f1 | cut -d@ -f1 | head -1");

    FILE *fp = popen(cmd, "r");
    if (fp && fgets(actual_interface, sizeof(actual_interface), fp)) {
        pclose(fp);
        // Remove newline
        actual_interface[strcspn(actual_interface, "\n")] = 0;
    } else {
        if (fp) pclose(fp);
        return -1;
    }

    // Initialize netlink handle for container IP configuration
    netlink_handle_t nl_handle;
    if (netlink_open(&nl_handle) != 0) {
        fprintf(stderr, "Failed to open netlink socket: %s\n", strerror(errno));
        return -1;
    }

    // Configure container IP using netlink (now working!)
    if (netlink_add_ip_address(&nl_handle, actual_interface, config->container_ip, config->prefix_len) != 0) {
        fprintf(stderr, "Failed to configure container IP: %s\n", strerror(errno));
        netlink_close(&nl_handle);
        return -1;
    }

    // Bring up container interface using netlink (now working!)
    if (netlink_set_interface_up(&nl_handle, actual_interface) != 0) {
        fprintf(stderr, "Failed to bring container interface up: %s\n", strerror(errno));
        netlink_close(&nl_handle);
        return -1;
    }

    // Bring up loopback using netlink
    if (netlink_set_interface_up(&nl_handle, "lo") != 0) {
        fprintf(stderr, "Failed to bring loopback up: %s\n", strerror(errno));
        netlink_close(&nl_handle);
        return -1;
    }

    // Add default route using netlink
    if (netlink_add_default_route(&nl_handle, config->bridge_ip, NULL) != 0) {
        fprintf(stderr, "Failed to add default route: %s\n", strerror(errno));
        netlink_close(&nl_handle);
        return -1;
    }

    netlink_close(&nl_handle);
    return 0;
}
