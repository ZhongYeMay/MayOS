#include "core.h"
#include "net.h"
#include "security_entropy_seed.h"
#include "security_trusted_time.h"

extern void mayrs_roughtime_request(const u8 *nonce, const u8 *key, u8 *request);
extern u64 mayrs_roughtime_verify(const u8 *data, u32 length, const u8 *nonce,
                                  const u8 *key, u32 *radius);

#ifdef MAYOS_ROUGHTIME_TEST_PORT
#include "roughtime_fixture_key.h"
enum { PORT = MAYOS_ROUGHTIME_TEST_PORT, REFRESH_TICKS = 600, RETRY_TICKS = 200 };
#else
static const char service[] = "roughtime.cloudflare.com";
enum { PORT = 2003, REFRESH_TICKS = 360000, RETRY_TICKS = 3000 };
/* Cloudflare's published Ed25519 Roughtime root, not an X.509 certificate. */
static const u8 server_key[] = {
    0xd0, 0x60, 0xfb, 0x73, 0x7c, 0x8f, 0xf3, 0x11,
    0x1c, 0xe1, 0x99, 0x76, 0xcd, 0xeb, 0x8d, 0xd9,
    0x29, 0x4b, 0xbc, 0x35, 0x55, 0xa1, 0xc8, 0xec,
    0x3d, 0x22, 0xfc, 0xfd, 0x19, 0x7f, 0xef, 0x38,
};
#endif

enum Phase { WAIT, DNS, REPLY };
static enum Phase phase;
static u8 nonce[32], request[1024], reply[MAY_UDP_EXCHANGE_MAX];
static u32 next, started;

static void retry(void) {
    next = ticks + RETRY_TICKS;
    phase = WAIT;
}

static void request_time(const u8 address[4]) {
    u8 samples[MAY_ENTROPY_SEED_BYTES];
    if (!may_entropy_seed(samples)) {
        retry();
        return;
    }
    memcpy(nonce, samples, sizeof nonce);
    volatile u8 *wipe = samples;
    for (unsigned i = 0; i < sizeof samples; i++) wipe[i] = 0;
    mayrs_roughtime_request(nonce, server_key, request);
    if (!may_net_udp_start(address, PORT, request, sizeof request)) {
        retry();
        return;
    }
    started = ticks;
    phase = REPLY;
}

void may_roughtime_poll(void) {
    switch (phase) {
    case WAIT:
        if ((i32)(ticks - next) < 0 || may_net_dhcp_info()->state != MAY_DHCP_BOUND) return;
#ifdef MAYOS_ROUGHTIME_TEST_PORT
        static const u8 loopback[] = {10, 0, 2, 2};
        request_time(loopback);
#else
        if (may_net_dns_lookup(service)) {
            phase = DNS;
            started = ticks;
        } else {
            next = ticks + 100;
        }
#endif
        return;
    case DNS:
#ifndef MAYOS_ROUGHTIME_TEST_PORT
        {
            u8 address[4];
            int status = may_net_dns_result(service, address);
            if (status > 0) request_time(address);
            else if (status < 0 || ticks - started >= 550u) retry();
        }
#endif
        return;
    case REPLY: {
        int size = may_net_udp_read(reply, sizeof reply);
        if (!size) return;
        u32 duration = ticks - started;
        u32 radius;
        u64 midpoint = size > 0 && duration <= 350u
            ? mayrs_roughtime_verify(reply, (u32)size, nonce, server_key, &radius) : 0;
        if (!midpoint || !may_time_accept_network(midpoint, radius + (duration + 99) / 100)) {
            if (size > 0) serial("MayOS: Roughtime reply rejected\n");
            retry();
            return;
        }
        serial("MayOS: signed UTC synchronized over Roughtime\n");
        next = ticks + REFRESH_TICKS;
        phase = WAIT;
        return;
    }
    }
}
