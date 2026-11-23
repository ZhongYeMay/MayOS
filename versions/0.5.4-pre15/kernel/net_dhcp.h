#ifndef MAYOS_NET_DHCP_H
#define MAYOS_NET_DHCP_H
#include "core.h"

/* Native DHCPv4 BOOTP client. After expiry the caller starts a fresh
 * discovery; no directed renew/rebind/release or persistent configuration. */
enum {
    MAY_DHCP_IDLE = 0, MAY_DHCP_DISCOVER, MAY_DHCP_OFFER,
    MAY_DHCP_REQUEST, MAY_DHCP_BOUND, MAY_DHCP_NAK
};
typedef struct {
    u8 mac[6], offered_ip[4], address[4], subnet[4];
    u8 router[4], dns[4], server[4];
    u32 xid, lease_seconds;
    u8 state;
} MayDhcpClient;

/* Returns message byte size, 0 on invalid state/capacity.
 * Packets are padded to BOOTP's 300-byte minimum for compatibility. */
void may_dhcp_begin(MayDhcpClient *client, const u8 mac[6], u32 xid);
int may_dhcp_discover(const MayDhcpClient *client, u8 *output, u32 capacity);
int may_dhcp_request(MayDhcpClient *client, u8 *output, u32 capacity);
/* 1=valid OFFER, 2=valid ACK / bound, -1=valid NAK; 0=ignored.
 * Source IP is the UDP/IP sender. Bound only after matching transaction,
 * client MAC, server ID, offered address and well-formed DHCP options. */
int may_dhcp_receive(MayDhcpClient *client, const u8 *packet, u32 size,
                     const u8 source_ip[4]);

#endif
