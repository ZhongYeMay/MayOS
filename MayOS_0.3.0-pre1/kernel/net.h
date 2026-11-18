#ifndef MAYOS_NET_H
#define MAYOS_NET_H
#include "core.h"

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
} MayNetInfo;

void may_net_init(void);
void may_net_poll(void);
const MayNetInfo *may_net_info(void);
/* QEMU SLIRP diagnostic ONLY: static 10.0.2.15 -> 10.0.2.2 ARP probe.
 * Never infer public Internet, TCP, DHCP, or HTTPS from this result. */
int may_net_qemu_arp_probe(void);

#endif
