#include "netlink.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/if.h>
#include <linux/veth.h>
#include <arpa/inet.h>

/**
 * Open a netlink socket for communication
 * @param handle Netlink handle to initialize
 * @return 0 on success, -1 on failure
 */
int netlink_open(netlink_handle_t *handle) {
    if (!handle) {
        errno = EINVAL;
        return -1;
    }

    // Create netlink socket
    handle->fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (handle->fd < 0) {
        perror("socket(AF_NETLINK)");
        return -1;
    }

    // Initialize local address
    memset(&handle->addr, 0, sizeof(handle->addr));
    handle->addr.nl_family = AF_NETLINK;
    handle->addr.nl_pid = getpid();
    handle->addr.nl_groups = 0;

    // Bind socket
    if (bind(handle->fd, (struct sockaddr *)&handle->addr, sizeof(handle->addr)) < 0) {
        perror("bind(netlink)");
        close(handle->fd);
        return -1;
    }

    // Initialize sequence number
    handle->seq = 1;

    return 0;
}

/**
 * Close netlink socket
 * @param handle Netlink handle to close
 */
void netlink_close(netlink_handle_t *handle) {
    if (handle && handle->fd >= 0) {
        close(handle->fd);
        handle->fd = -1;
    }
}

/**
 * Send netlink request
 * @param handle Netlink handle
 * @param req Request message
 * @param len Request length
 * @return 0 on success, -1 on failure
 */
int netlink_send_request(netlink_handle_t *handle, struct nlmsghdr *req, size_t len) {
    struct sockaddr_nl dest = {0};
    struct iovec iov = {0};
    struct msghdr msg = {0};

    if (!handle || !req) {
        errno = EINVAL;
        return -1;
    }

    // Set up destination (kernel)
    dest.nl_family = AF_NETLINK;
    dest.nl_pid = 0; // Kernel
    dest.nl_groups = 0;

    // Set up message
    iov.iov_base = req;
    iov.iov_len = len;
    msg.msg_name = &dest;
    msg.msg_namelen = sizeof(dest);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    // Send message
    if (sendmsg(handle->fd, &msg, 0) < 0) {
        perror("sendmsg(netlink)");
        return -1;
    }

    return 0;
}

/**
 * Receive netlink response
 * @param handle Netlink handle
 * @param buffer Buffer for response
 * @param buffer_size Buffer size
 * @return bytes received on success, -1 on failure
 */
int netlink_recv_response(netlink_handle_t *handle, char *buffer, size_t buffer_size) {
    struct sockaddr_nl addr;
    struct iovec iov = {0};
    struct msghdr msg = {0};
    socklen_t addr_len = sizeof(addr);

    if (!handle || !buffer) {
        errno = EINVAL;
        return -1;
    }

    // Set up message
    iov.iov_base = buffer;
    iov.iov_len = buffer_size;
    msg.msg_name = &addr;
    msg.msg_namelen = addr_len;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    // Receive message
    int bytes = recvmsg(handle->fd, &msg, 0);
    if (bytes < 0) {
        perror("recvmsg(netlink)");
        return -1;
    }

    // Check if message was truncated
    if (msg.msg_flags & MSG_TRUNC) {
        errno = EMSGSIZE;
        return -1;
    }

    return bytes;
}

/**
 * Get interface index by name using simple approach
 * @param handle Netlink handle
 * @param name Interface name
 * @return interface index on success, -1 on failure
 */
int netlink_get_interface_index(netlink_handle_t *handle, const char *name) {
    if (!handle || !name) {
        errno = EINVAL;
        return -1;
    }

    // Use if_nametoindex() as a simple, reliable fallback
    // This is a standard POSIX function that works reliably
    int index = if_nametoindex(name);
    if (index == 0) {
        // Interface doesn't exist
        errno = ENODEV;
        return -1;
    }

    return index;
}

/**
 * Check if interface exists
 * @param handle Netlink handle
 * @param name Interface name
 * @return 1 if exists, 0 if not, -1 on error
 */
int netlink_interface_exists(netlink_handle_t *handle, const char *name) {
    int index = netlink_get_interface_index(handle, name);
    if (index > 0) {
        return 1;
    } else if (errno == ENODEV) {
        return 0;
    }
    return -1;
}

/**
 * Create a bridge interface
 * @param handle Netlink handle
 * @param name Bridge name
 * @return 0 on success, -1 on failure
 */
int netlink_create_bridge(netlink_handle_t *handle, const char *name) {
    struct {
        struct nlmsghdr nh;
        struct ifinfomsg ifi;
        char data[256];
    } req = {0};

    struct rtattr *linkinfo;
    int len = 0;

    if (!handle || !name) {
        errno = EINVAL;
        return -1;
    }

    // Check if bridge already exists
    if (netlink_interface_exists(handle, name) == 1) {
        return 0; // Already exists, success
    }

    // Prepare request - FIXED: Added NLM_F_ACK flag
    req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(req.ifi));
    req.nh.nlmsg_type = RTM_NEWLINK;
    req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
    req.nh.nlmsg_seq = handle->seq++;
    req.nh.nlmsg_pid = getpid();
    req.ifi.ifi_family = AF_UNSPEC;

    // Add interface name attribute
    struct rtattr *rta = (struct rtattr *)(req.data + len);
    rta->rta_type = IFLA_IFNAME;
    rta->rta_len = RTA_LENGTH(strlen(name) + 1);
    strcpy(RTA_DATA(rta), name);
    len += RTA_ALIGN(rta->rta_len);

    // Add link info for bridge
    linkinfo = (struct rtattr *)(req.data + len);
    linkinfo->rta_type = IFLA_LINKINFO;
    len += RTA_LENGTH(0);

    // Add info kind (bridge)
    struct rtattr *infokind = (struct rtattr *)((char *)linkinfo + RTA_LENGTH(0));
    infokind->rta_type = IFLA_INFO_KIND;
    infokind->rta_len = RTA_LENGTH(strlen("bridge") + 1);
    strcpy(RTA_DATA(infokind), "bridge");

    linkinfo->rta_len = RTA_LENGTH(RTA_ALIGN(infokind->rta_len));
    len += RTA_ALIGN(infokind->rta_len);

    req.nh.nlmsg_len += len;

    // Send request
    if (netlink_send_request(handle, &req.nh, req.nh.nlmsg_len) < 0) {
        return -1;
    }

    // Receive response
    char buffer[NETLINK_BUFFER_SIZE];
    int bytes = netlink_recv_response(handle, buffer, sizeof(buffer));
    if (bytes < 0) {
        return -1;
    }

    // Check for errors
    struct nlmsghdr *resp = (struct nlmsghdr *)buffer;
    if (resp->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(resp);
        if (err->error != 0) {
            errno = -err->error;
            return -1;
        }
    }

    return 0;
}

/**
 * Create a veth pair
 * @param handle Netlink handle
 * @param name1 First interface name
 * @param name2 Peer interface name
 * @return 0 on success, -1 on failure
 */
int netlink_create_veth_pair(netlink_handle_t *handle, const char *name1, const char *name2) {
    struct {
        struct nlmsghdr nh;
        struct ifinfomsg ifi;
        char data[512];
    } req = {0};

    int len = 0;

    if (!handle || !name1 || !name2) {
        errno = EINVAL;
        return -1;
    }

    // Prepare request - FIXED: Added NLM_F_ACK flag
    req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(req.ifi));
    req.nh.nlmsg_type = RTM_NEWLINK;
    req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
    req.nh.nlmsg_seq = handle->seq++;
    req.nh.nlmsg_pid = getpid();
    req.ifi.ifi_family = AF_UNSPEC;

    // Add interface name
    struct rtattr *rta = (struct rtattr *)(req.data + len);
    rta->rta_type = IFLA_IFNAME;
    rta->rta_len = RTA_LENGTH(strlen(name1) + 1);
    strcpy(RTA_DATA(rta), name1);
    len += RTA_ALIGN(rta->rta_len);

    // Add link info for veth
    struct rtattr *linkinfo = (struct rtattr *)(req.data + len);
    linkinfo->rta_type = IFLA_LINKINFO;
    int linkinfo_len = RTA_LENGTH(0);

    // Add info kind (veth)
    struct rtattr *infokind = (struct rtattr *)((char *)linkinfo + RTA_LENGTH(0));
    infokind->rta_type = IFLA_INFO_KIND;
    infokind->rta_len = RTA_LENGTH(strlen("veth") + 1);
    strcpy(RTA_DATA(infokind), "veth");
    linkinfo_len += RTA_ALIGN(infokind->rta_len);

    // Add info data (peer information)
    struct rtattr *infodata = (struct rtattr *)((char *)linkinfo + linkinfo_len);
    infodata->rta_type = IFLA_INFO_DATA;
    int infodata_len = RTA_LENGTH(0);

    // Add peer info
    struct rtattr *peer_info = (struct rtattr *)((char *)infodata + RTA_LENGTH(0));
    peer_info->rta_type = VETH_INFO_PEER;
    int peer_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));

    // Set up peer ifinfomsg
    struct ifinfomsg *peer_ifi = (struct ifinfomsg *)((char *)peer_info + RTA_LENGTH(0));
    memset(peer_ifi, 0, sizeof(*peer_ifi));
    peer_ifi->ifi_family = AF_UNSPEC;

    // Add peer name
    struct rtattr *peer_name = (struct rtattr *)((char *)peer_ifi + sizeof(struct ifinfomsg));
    peer_name->rta_type = IFLA_IFNAME;
    peer_name->rta_len = RTA_LENGTH(strlen(name2) + 1);
    strcpy(RTA_DATA(peer_name), name2);
    peer_len += RTA_ALIGN(peer_name->rta_len);

    peer_info->rta_len = RTA_LENGTH(peer_len);
    infodata_len += RTA_ALIGN(peer_info->rta_len);
    infodata->rta_len = RTA_LENGTH(infodata_len);
    linkinfo_len += RTA_ALIGN(infodata->rta_len);

    linkinfo->rta_len = RTA_LENGTH(linkinfo_len);
    len += RTA_ALIGN(linkinfo->rta_len);

    req.nh.nlmsg_len += len;

    // Send request
    if (netlink_send_request(handle, &req.nh, req.nh.nlmsg_len) < 0) {
        return -1;
    }

    // Receive response
    char buffer[NETLINK_BUFFER_SIZE];
    int bytes = netlink_recv_response(handle, buffer, sizeof(buffer));
    if (bytes < 0) {
        return -1;
    }

    // Check for errors
    struct nlmsghdr *resp = (struct nlmsghdr *)buffer;
    if (resp->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(resp);
        if (err->error != 0) {
            errno = -err->error;
            return -1;
        }
    }

    return 0;
}

/**
 * Set interface up or down
 * @param handle Netlink handle
 * @param name Interface name
 * @param up 1 for up, 0 for down
 * @return 0 on success, -1 on failure
 */
static int netlink_set_interface_state(netlink_handle_t *handle, const char *name, int up) {
    struct {
        struct nlmsghdr nh;
        struct ifinfomsg ifi;
    } req = {0};

    if (!handle || !name) {
        errno = EINVAL;
        return -1;
    }

    int ifindex = netlink_get_interface_index(handle, name);
    if (ifindex < 0) {
        return -1;
    }

    // Prepare request - FIXED: Use RTM_SETLINK for modifying existing interfaces
    req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(req.ifi));
    req.nh.nlmsg_type = RTM_SETLINK;  // Changed from RTM_NEWLINK
    req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;  // FIXED: Added NLM_F_ACK
    req.nh.nlmsg_seq = handle->seq++;
    req.nh.nlmsg_pid = getpid();
    req.ifi.ifi_family = AF_UNSPEC;
    req.ifi.ifi_index = ifindex;
    req.ifi.ifi_flags = up ? IFF_UP : 0;
    req.ifi.ifi_change = IFF_UP;

    // Send request
    if (netlink_send_request(handle, &req.nh, req.nh.nlmsg_len) < 0) {
        return -1;
    }

    // Receive response
    char buffer[NETLINK_BUFFER_SIZE];
    int bytes = netlink_recv_response(handle, buffer, sizeof(buffer));
    if (bytes < 0) {
        return -1;
    }

    // Check for errors - FIXED: Proper ACK response handling
    struct nlmsghdr *resp = (struct nlmsghdr *)buffer;
    if (resp->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(resp);
        if (err->error != 0) {
            errno = -err->error;
            return -1;
        }
        // err->error == 0 means success ACK
    }

    return 0;
}

/**
 * Set interface up
 * @param handle Netlink handle
 * @param name Interface name
 * @return 0 on success, -1 on failure
 */
int netlink_set_interface_up(netlink_handle_t *handle, const char *name) {
    return netlink_set_interface_state(handle, name, 1);
}

/**
 * Set interface down
 * @param handle Netlink handle
 * @param name Interface name
 * @return 0 on success, -1 on failure
 */
int netlink_set_interface_down(netlink_handle_t *handle, const char *name) {
    return netlink_set_interface_state(handle, name, 0);
}

/**
 * Delete interface
 * @param handle Netlink handle
 * @param name Interface name
 * @return 0 on success, -1 on failure
 */
int netlink_delete_interface(netlink_handle_t *handle, const char *name) {
    struct {
        struct nlmsghdr nh;
        struct ifinfomsg ifi;
    } req = {0};

    if (!handle || !name) {
        errno = EINVAL;
        return -1;
    }

    int ifindex = netlink_get_interface_index(handle, name);
    if (ifindex < 0) {
        return 0; // Interface doesn't exist, consider success
    }

    // Prepare request
    req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(req.ifi));
    req.nh.nlmsg_type = RTM_DELLINK;
    req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    req.nh.nlmsg_seq = handle->seq++;
    req.nh.nlmsg_pid = getpid();
    req.ifi.ifi_family = AF_UNSPEC;
    req.ifi.ifi_index = ifindex;

    // Send request
    if (netlink_send_request(handle, &req.nh, req.nh.nlmsg_len) < 0) {
        return -1;
    }

    // Receive response
    char buffer[NETLINK_BUFFER_SIZE];
    int bytes = netlink_recv_response(handle, buffer, sizeof(buffer));
    if (bytes < 0) {
        return -1;
    }

    // Check for errors
    struct nlmsghdr *resp = (struct nlmsghdr *)buffer;
    if (resp->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(resp);
        if (err->error != 0) {
            errno = -err->error;
            return -1;
        }
        // err->error == 0 means success ACK
    }

    return 0;
}

/**
 * Set interface master (add to bridge)
 * @param handle Netlink handle
 * @param iface Interface name
 * @param master Master interface (bridge) name
 * @return 0 on success, -1 on failure
 */
int netlink_set_interface_master(netlink_handle_t *handle, const char *iface, const char *master) {
    struct {
        struct nlmsghdr nh;
        struct ifinfomsg ifi;
        char data[256];
    } req = {0};

    int len = 0;

    if (!handle || !iface || !master) {
        errno = EINVAL;
        return -1;
    }

    int ifindex = netlink_get_interface_index(handle, iface);
    if (ifindex < 0) {
        return -1;
    }

    int master_index = netlink_get_interface_index(handle, master);
    if (master_index < 0) {
        return -1;
    }

    // Prepare request - FIXED: Added NLM_F_ACK flag and use RTM_SETLINK
    req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(req.ifi));
    req.nh.nlmsg_type = RTM_SETLINK;  // Use SETLINK for modifying existing interface
    req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    req.nh.nlmsg_seq = handle->seq++;
    req.nh.nlmsg_pid = getpid();
    req.ifi.ifi_family = AF_UNSPEC;
    req.ifi.ifi_index = ifindex;

    // Add master attribute
    struct rtattr *rta = (struct rtattr *)(req.data + len);
    rta->rta_type = IFLA_MASTER;
    rta->rta_len = RTA_LENGTH(4);
    memcpy(RTA_DATA(rta), &master_index, 4);
    len += RTA_ALIGN(rta->rta_len);
    req.nh.nlmsg_len += len;

    // Send request
    if (netlink_send_request(handle, &req.nh, req.nh.nlmsg_len) < 0) {
        return -1;
    }

    // Receive response
    char buffer[NETLINK_BUFFER_SIZE];
    int bytes = netlink_recv_response(handle, buffer, sizeof(buffer));
    if (bytes < 0) {
        return -1;
    }

    // Check for errors
    struct nlmsghdr *resp = (struct nlmsghdr *)buffer;
    if (resp->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(resp);
        if (err->error != 0) {
            errno = -err->error;
            return -1;
        }
    }

    return 0;
}

/**
 * Add IP address to interface
 * @param handle Netlink handle
 * @param iface Interface name
 * @param addr IP address (network byte order)
 * @param prefixlen Prefix length
 * @return 0 on success, -1 on failure
 */
int netlink_add_ip_address(netlink_handle_t *handle, const char *iface, uint32_t addr, int prefixlen) {
    struct {
        struct nlmsghdr nh;
        struct ifaddrmsg ifa;
        char data[256];
    } req = {0};

    int len = 0;

    if (!handle || !iface) {
        errno = EINVAL;
        return -1;
    }

    int ifindex = netlink_get_interface_index(handle, iface);
    if (ifindex < 0) {
        return -1;
    }

    // Prepare request - FIXED: Added NLM_F_ACK flag
    req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(req.ifa));
    req.nh.nlmsg_type = RTM_NEWADDR;
    req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
    req.nh.nlmsg_seq = handle->seq++;
    req.nh.nlmsg_pid = getpid();

    req.ifa.ifa_family = AF_INET;
    req.ifa.ifa_prefixlen = prefixlen;
    req.ifa.ifa_flags = IFA_F_PERMANENT;
    req.ifa.ifa_scope = RT_SCOPE_UNIVERSE;
    req.ifa.ifa_index = ifindex;

    // Add local address attribute
    struct rtattr *rta = (struct rtattr *)(req.data + len);
    rta->rta_type = IFA_LOCAL;
    rta->rta_len = RTA_LENGTH(4);
    memcpy(RTA_DATA(rta), &addr, 4);
    len += RTA_ALIGN(rta->rta_len);

    // Add address attribute (same as local for most cases)
    rta = (struct rtattr *)(req.data + len);
    rta->rta_type = IFA_ADDRESS;
    rta->rta_len = RTA_LENGTH(4);
    memcpy(RTA_DATA(rta), &addr, 4);
    len += RTA_ALIGN(rta->rta_len);

    req.nh.nlmsg_len += len;

    // Send request
    if (netlink_send_request(handle, &req.nh, req.nh.nlmsg_len) < 0) {
        return -1;
    }

    // Receive response
    char buffer[NETLINK_BUFFER_SIZE];
    int bytes = netlink_recv_response(handle, buffer, sizeof(buffer));
    if (bytes < 0) {
        return -1;
    }

    // Check for errors
    struct nlmsghdr *resp = (struct nlmsghdr *)buffer;
    if (resp->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(resp);
        if (err->error != 0) {
            errno = -err->error;
            return -1;
        }
    }

    return 0;
}

/**
 * Add default route
 * @param handle Netlink handle
 * @param gateway Gateway IP address (network byte order)
 * @param iface Interface name (optional, can be NULL)
 * @return 0 on success, -1 on failure
 */
int netlink_add_default_route(netlink_handle_t *handle, uint32_t gateway, const char *iface) {
    struct {
        struct nlmsghdr nh;
        struct rtmsg rt;
        char data[256];
    } req = {0};

    int len = 0;

    if (!handle) {
        errno = EINVAL;
        return -1;
    }

    // Prepare request
    req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(req.rt));
    req.nh.nlmsg_type = RTM_NEWROUTE;
    req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
    req.nh.nlmsg_seq = handle->seq++;
    req.nh.nlmsg_pid = getpid();

    req.rt.rtm_family = AF_INET;
    req.rt.rtm_table = RT_TABLE_MAIN;
    req.rt.rtm_protocol = RTPROT_STATIC;
    req.rt.rtm_scope = RT_SCOPE_UNIVERSE;
    req.rt.rtm_type = RTN_UNICAST;
    req.rt.rtm_dst_len = 0;  // Default route (0.0.0.0/0)

    // Add gateway attribute
    struct rtattr *rta = (struct rtattr *)(req.data + len);
    rta->rta_type = RTA_GATEWAY;
    rta->rta_len = RTA_LENGTH(4);
    memcpy(RTA_DATA(rta), &gateway, 4);
    len += RTA_ALIGN(rta->rta_len);

    // Add interface if specified
    if (iface) {
        int ifindex = netlink_get_interface_index(handle, iface);
        if (ifindex > 0) {
            rta = (struct rtattr *)(req.data + len);
            rta->rta_type = RTA_OIF;  // Output interface
            rta->rta_len = RTA_LENGTH(4);
            memcpy(RTA_DATA(rta), &ifindex, 4);
            len += RTA_ALIGN(rta->rta_len);
        }
    }

    req.nh.nlmsg_len += len;

    // Send request
    if (netlink_send_request(handle, &req.nh, req.nh.nlmsg_len) < 0) {
        return -1;
    }

    // Receive response
    char buffer[NETLINK_BUFFER_SIZE];
    int bytes = netlink_recv_response(handle, buffer, sizeof(buffer));
    if (bytes < 0) {
        return -1;
    }

    // Check for errors
    struct nlmsghdr *resp = (struct nlmsghdr *)buffer;
    if (resp->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(resp);
        if (err->error != 0) {
            errno = -err->error;
            return -1;
        }
        // err->error == 0 means success ACK
    }

    return 0;
}

/**
 * Move interface to network namespace
 * @param handle Netlink handle
 * @param iface Interface name
 * @param pid Process ID of target namespace
 * @return 0 on success, -1 on failure
 */
int netlink_move_interface_to_netns(netlink_handle_t *handle, const char *iface, pid_t pid) {
    struct {
        struct nlmsghdr nh;
        struct ifinfomsg ifi;
        char data[256];
    } req = {0};

    int len = 0;

    if (!handle || !iface) {
        errno = EINVAL;
        return -1;
    }

    int ifindex = netlink_get_interface_index(handle, iface);
    if (ifindex < 0) {
        return -1;
    }

    // Prepare request
    req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(req.ifi));
    req.nh.nlmsg_type = RTM_SETLINK;
    req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    req.nh.nlmsg_seq = handle->seq++;
    req.nh.nlmsg_pid = getpid();
    req.ifi.ifi_family = AF_UNSPEC;
    req.ifi.ifi_index = ifindex;

    // Add network namespace PID attribute
    struct rtattr *rta = (struct rtattr *)(req.data + len);
    rta->rta_type = IFLA_NET_NS_PID;
    rta->rta_len = RTA_LENGTH(4);
    memcpy(RTA_DATA(rta), &pid, 4);
    len += RTA_ALIGN(rta->rta_len);

    req.nh.nlmsg_len += len;

    // Send request
    if (netlink_send_request(handle, &req.nh, req.nh.nlmsg_len) < 0) {
        return -1;
    }

    // Receive response
    char buffer[NETLINK_BUFFER_SIZE];
    int bytes = netlink_recv_response(handle, buffer, sizeof(buffer));
    if (bytes < 0) {
        return -1;
    }

    // Check for errors
    struct nlmsghdr *resp = (struct nlmsghdr *)buffer;
    if (resp->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(resp);
        if (err->error != 0) {
            errno = -err->error;
            return -1;
        }
        // err->error == 0 means success ACK
    }

    return 0;
}

/**
 * Get default route interface name
 * @param handle Netlink handle
 * @param iface Buffer to store interface name
 * @param iface_size Size of interface buffer
 * @return 0 on success, -1 on failure
 */
int netlink_get_default_interface(netlink_handle_t *handle, char *iface, size_t iface_size) {
    (void)iface_size;  // Suppress unused parameter warning
    struct {
        struct nlmsghdr nh;
        struct rtmsg rt;
    } req = {0};

    char buffer[NETLINK_BUFFER_SIZE];
    struct nlmsghdr *resp;
    struct rtmsg *rt;
    struct rtattr *rta;
    int len, found = 0;

    if (!handle || !iface) {
        errno = EINVAL;
        return -1;
    }

    // Prepare request to get routing table
    req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(req.rt));
    req.nh.nlmsg_type = RTM_GETROUTE;
    req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.nh.nlmsg_seq = handle->seq++;
    req.nh.nlmsg_pid = getpid();
    req.rt.rtm_family = AF_INET;
    req.rt.rtm_table = RT_TABLE_MAIN;

    // Send request
    if (netlink_send_request(handle, &req.nh, req.nh.nlmsg_len) < 0) {
        return -1;
    }

    // Receive and parse responses
    while (1) {
        int bytes = netlink_recv_response(handle, buffer, sizeof(buffer));
        if (bytes < 0) {
            return -1;
        }

        for (resp = (struct nlmsghdr *)buffer; NLMSG_OK(resp, bytes); resp = NLMSG_NEXT(resp, bytes)) {
            if (resp->nlmsg_type == NLMSG_DONE) {
                goto done;
            }

            if (resp->nlmsg_type == NLMSG_ERROR) {
                struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(resp);
                errno = -err->error;
                return -1;
            }

            if (resp->nlmsg_type != RTM_NEWROUTE) {
                continue;
            }

            rt = (struct rtmsg *)NLMSG_DATA(resp);

            // Look for default route (dst_len = 0)
            if (rt->rtm_dst_len == 0 && rt->rtm_family == AF_INET) {
                len = resp->nlmsg_len - NLMSG_LENGTH(sizeof(*rt));

                // Parse attributes to find output interface
                for (rta = RTM_RTA(rt); RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
                    if (rta->rta_type == RTA_OIF) {
                        int ifindex = *(int *)RTA_DATA(rta);
                        // Convert interface index to name
                        if (if_indextoname(ifindex, iface) != NULL) {
                            found = 1;
                            goto done;
                        }
                    }
                }
            }
        }
    }

done:
    if (!found) {
        errno = ENOENT;
        return -1;
    }

    return 0;
}

/**
 * Get netlink error string
 * @param error Error code (negative errno)
 * @return Error string
 */
const char *netlink_strerror(int error) {
    return strerror(-error);
}
