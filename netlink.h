#ifndef MICROBOX_NETLINK_H
#define MICROBOX_NETLINK_H

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <stdint.h>

#define NETLINK_BUFFER_SIZE 8192
#define MAX_INTERFACE_NAME 16

/**
 * Netlink socket handle
 */
typedef struct {
    int fd;                 // Socket file descriptor
    uint32_t seq;          // Sequence number for requests
    struct sockaddr_nl addr; // Local address
} netlink_handle_t;

/**
 * Network interface information
 */
typedef struct {
    int index;                           // Interface index
    char name[MAX_INTERFACE_NAME];       // Interface name
    unsigned int flags;                  // Interface flags (IFF_UP, etc.)
    unsigned int mtu;                    // Maximum transmission unit
    unsigned char addr[6];               // Hardware address (MAC)
} netlink_interface_t;

/**
 * IP address information
 */
typedef struct {
    int family;         // AF_INET or AF_INET6
    int prefixlen;      // Prefix length
    uint32_t addr;      // IP address (IPv4)
    int ifindex;        // Interface index
} netlink_addr_t;

/**
 * Route information
 */
typedef struct {
    int family;         // Address family
    int dst_len;        // Destination prefix length
    uint32_t dst;       // Destination address
    uint32_t gateway;   // Gateway address
    int ifindex;        // Output interface index
} netlink_route_t;

/* Core netlink functions */
int netlink_open(netlink_handle_t *handle);
void netlink_close(netlink_handle_t *handle);
int netlink_send_request(netlink_handle_t *handle, struct nlmsghdr *req, size_t len);
int netlink_recv_response(netlink_handle_t *handle, char *buffer, size_t buffer_size);

/* RTNetlink interface management */
int netlink_create_bridge(netlink_handle_t *handle, const char *name);
int netlink_create_veth_pair(netlink_handle_t *handle, const char *name1, const char *name2);
int netlink_delete_interface(netlink_handle_t *handle, const char *name);
int netlink_set_interface_up(netlink_handle_t *handle, const char *name);
int netlink_set_interface_down(netlink_handle_t *handle, const char *name);
int netlink_set_interface_master(netlink_handle_t *handle, const char *iface, const char *master);
int netlink_move_interface_to_netns(netlink_handle_t *handle, const char *name, pid_t pid);

/* IP address management */
int netlink_add_ip_address(netlink_handle_t *handle, const char *iface, uint32_t addr, int prefixlen);
int netlink_delete_ip_address(netlink_handle_t *handle, const char *iface, uint32_t addr, int prefixlen);

/* Route management */
int netlink_add_default_route(netlink_handle_t *handle, uint32_t gateway, const char *iface);
int netlink_delete_route(netlink_handle_t *handle, uint32_t dst, int dst_len, uint32_t gateway);

/* Utility functions */
int netlink_get_interface_index(netlink_handle_t *handle, const char *name);
int netlink_get_interface_info(netlink_handle_t *handle, const char *name, netlink_interface_t *info);
int netlink_interface_exists(netlink_handle_t *handle, const char *name);
int netlink_get_default_interface(netlink_handle_t *handle, char *iface, size_t iface_size);

/* Error handling */
const char *netlink_strerror(int error);

#endif /* MICROBOX_NETLINK_H */