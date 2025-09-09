#ifndef NET_H
#define NET_H

/**
 * The network mode (none, host, private, bridge).
 */
typedef enum {
  NET_NONE,
  NET_HOST,
  NET_PRIVATE,
  NET_BRIDGE,
  NET_INVALID
} net_mode_t;

#endif // NET_H
