#ifndef MAYOS_NET_H
#define MAYOS_NET_H
#include "core.h"
#include "net_dhcp.h"

/* MayKernel-only PCI RTL8139 Ethernet bring-up, polled (no IRQ routing).
 * No general-purpose IP configuration or Internet connectivity yet. */
typedef struct {
    u8 mac[6];
    u16 io_base;
    u32 packets_rx;
    u32 packets_tx;
    u32 rx_errors;
    int present;
    int gateway_arp_reply;
    int ipv4_udp_reply;
    int dns_fixture_reply;
    int tcp_fixture_connected;
    int tcp_fixture_reply;
    int tcp_fixture_closed;
    int tcp_fixture_timeout;
    u32 tcp_fixture_retries;
    u32 tcp_fixture_bytes;
    u8 dns_fixture_address[4];
} MayNetInfo;

void may_net_init(void);
void may_net_poll(void);
const MayNetInfo *may_net_info(void);
/* QEMU SLIRP diagnostic ONLY: static 10.0.2.15 -> 10.0.2.2 ARP probe.
 * Never infer public Internet, TCP, DHCP, or HTTPS from this result. */
int may_net_qemu_arp_probe(void);
/* QEMU-only deterministic transport checks. Require prior gateway ARP.
 * UDP fixture: host port 10053. DNS fixture: same host, synthetic A record.
 * These prove packet handling, not public DNS connectivity or DHCP. */
int may_net_qemu_udp_probe(void);
int may_net_qemu_dns_fixture(void);
/* Start a DHCP lease exchange over the native RTL8139 driver. Initial
 * negotiation only: no renewal/rebinding, do not persist lease across boots. */
int may_net_qemu_tcp_http_probe(void);
/* Diagnostic only: deliberately drops initial guest SYN, then retransmits. */
int may_net_qemu_tcp_loss_probe(void);
int may_net_dhcp_start(void);
const MayDhcpClient *may_net_dhcp_info(void);
const char *may_net_format_ipv4(const u8 ip[4]);

#endif
