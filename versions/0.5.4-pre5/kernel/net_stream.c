#include "net_stream.h"
#include "net.h"
#include "net_ipv4.h"
#include "net_tcp_retry.h"

enum { PHASE_IDLE, PHASE_ARP, PHASE_SYN_SEND, PHASE_SYN_SENT, PHASE_OPEN,
       PHASE_DRAIN, PHASE_CLOSED, PHASE_FAILED };
enum { STREAM_RX_SIZE = 4096, STREAM_TX_SIZE = 512, ECHO_BYTES = 8192 };

static struct {
    int phase, ack_pending, peer_fin, local_fin, fin_acked, close_requested;
    u8 address[4], local[4], next_hop[4], mac[6];
    u16 local_port, port, peer_window;
    u32 started, last_event, last_arp, send_next, receive_next;
    unsigned arp_attempts, rx_head, rx_length;
    u8 rx[STREAM_RX_SIZE], tx[STREAM_TX_SIZE];
    MayTcpRetry retry;
} stream;
static unsigned tuple_serial;
static int echo_active;
static unsigned echo_sent, echo_received;

static u8 echo_byte(unsigned offset) {
    return offset == ECHO_BYTES - 1 ? '\n' : (u8)('a' + offset % 26);
}

static int equal(const u8 *a, const u8 *b, unsigned n) {
    for (unsigned i = 0; i < n; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

static int active(void) {
    return stream.phase >= PHASE_ARP && stream.phase <= PHASE_DRAIN;
}

static void fail(const char *reason) {
    stream.phase = PHASE_FAILED;
    may_tcp_retry_cancel(&stream.retry);
    serial(reason);
}

static int packet(u32 seq, u8 flags, const u8 *data, u16 bytes) {
    return may_stream_send_segment(stream.mac, stream.address, stream.local_port,
        stream.port, seq, stream.receive_next, flags, STREAM_RX_SIZE - stream.rx_length,
        data, bytes);
}

static int tracked(u8 flags, const u8 *data, u16 bytes) {
    if (stream.retry.active || !packet(stream.send_next, flags, data, bytes)) return 0;
    may_tcp_retry_start(&stream.retry, stream.send_next, bytes, flags, ticks);
    stream.send_next = stream.retry.expected_ack;
    return 1;
}

int may_stream_open(const u8 destination[4], u16 port) {
    const MayDhcpClient *lease = may_net_dhcp_info();
    u8 next_hop[4];
    if (active() || !port || lease->state != MAY_DHCP_BOUND ||
        !may_ipv4_next_hop(lease->address, lease->subnet, lease->router,
                           destination, next_hop)) return 0;
    memset(&stream, 0, sizeof(stream));
    memcpy(stream.address, destination, 4);
    memcpy(stream.local, lease->address, 4);
    memcpy(stream.next_hop, next_hop, 4);
    stream.local_port = (u16)(33000u + (++tuple_serial % 11000u));
    stream.port = port;
    stream.send_next = 0x4d41594fu ^ (ticks * 2654435761u) ^ (tuple_serial * 2246822519u);
    stream.started = stream.last_event = ticks;
    stream.phase = PHASE_ARP;
    return 1;
}

int may_stream_status(void) {
    if (stream.phase == PHASE_FAILED) return MAY_STREAM_FAILED;
    if (stream.phase == PHASE_CLOSED) return MAY_STREAM_CLOSED;
    if (stream.peer_fin) return MAY_STREAM_EOF;
    if (stream.phase == PHASE_OPEN) return MAY_STREAM_READY;
    return active() ? MAY_STREAM_CONNECTING : MAY_STREAM_IDLE;
}

int may_stream_read(u8 *data, unsigned capacity) {
    unsigned count = capacity < stream.rx_length ? capacity : stream.rx_length;
    for (unsigned i = 0; i < count; i++)
        data[i] = stream.rx[(stream.rx_head + i) % STREAM_RX_SIZE];
    stream.rx_head = (stream.rx_head + count) % STREAM_RX_SIZE;
    stream.rx_length -= count;
    if (count && stream.phase == PHASE_OPEN) stream.ack_pending = 1;
    return (int)count;
}

int may_stream_write(const u8 *data, unsigned length) {
    if (stream.phase != PHASE_OPEN || stream.close_requested || stream.retry.active ||
        !length || !stream.peer_window) return 0;
    if (length > STREAM_TX_SIZE) length = STREAM_TX_SIZE;
    if (length > stream.peer_window) length = stream.peer_window;
    memcpy(stream.tx, data, length);
    if (!tracked(MAY_TCP_ACK | MAY_TCP_PSH, stream.tx, (u16)length)) return 0;
    return (int)length;
}

void may_stream_close(void) {
    if (stream.phase == PHASE_OPEN) stream.close_requested = 1;
    else if (active() && stream.phase != PHASE_DRAIN) {
        may_tcp_retry_cancel(&stream.retry);
        stream.phase = PHASE_CLOSED;
    }
}

void may_stream_arp(const u8 address[4], const u8 mac[6]) {
    if (stream.phase != PHASE_ARP || !stream.arp_attempts ||
        !equal(address, stream.next_hop, 4)) return;
    memcpy(stream.mac, mac, 6);
    stream.phase = PHASE_SYN_SEND;
}

int may_stream_receive(const MayTcpSegment *segment) {
    if (stream.phase < PHASE_SYN_SENT || stream.phase > PHASE_DRAIN ||
        segment->destination_port != stream.local_port || segment->source_port != stream.port ||
        !equal(segment->source_ip, stream.address, 4) ||
        !equal(segment->destination_ip, stream.local, 4)) return 0;

    if (stream.phase == PHASE_SYN_SENT) {
        if (!(segment->flags & MAY_TCP_ACK) || segment->ack != stream.send_next) return 1;
        if (segment->flags & MAY_TCP_RST) {
            fail("MayOS: native TCP connection refused\n");
            return 1;
        }
        if (!(segment->flags & MAY_TCP_SYN)) return 1;
        may_tcp_retry_ack(&stream.retry, segment->ack);
        stream.receive_next = segment->seq + 1;
        stream.peer_window = segment->window;
        stream.ack_pending = 1;
        stream.phase = PHASE_OPEN;
        stream.last_event = ticks;
        serial("MayOS: native TCP handshake established\n");
        return 1;
    }
    if (segment->flags & MAY_TCP_RST) {
        if (segment->seq == stream.receive_next && stream.phase == PHASE_OPEN)
            fail("MayOS: native TCP connection reset\n");
        return 1;
    }
    if (!(segment->flags & MAY_TCP_ACK)) return 1;
    if (segment->seq != stream.receive_next || (segment->flags & MAY_TCP_SYN)) {
        stream.ack_pending = 1;
        return 1;
    }

    u32 oldest = stream.retry.active ? stream.retry.first_seq : stream.send_next;
    if ((i32)(segment->ack - oldest) < 0 || (i32)(stream.send_next - segment->ack) < 0) {
        stream.ack_pending = 1;
        return 1;
    }
    if (may_tcp_retry_ack(&stream.retry, segment->ack) && stream.local_fin)
        stream.fin_acked = 1;
    stream.peer_window = segment->window;
    if (stream.phase == PHASE_DRAIN || stream.peer_fin) return 1;

    unsigned count = segment->payload_length;
    if (count > STREAM_RX_SIZE - stream.rx_length) {
        stream.ack_pending = 1;
        return 1;
    }
    for (unsigned i = 0; i < count; i++)
        stream.rx[(stream.rx_head + stream.rx_length + i) % STREAM_RX_SIZE] = segment->payload[i];
    stream.rx_length += count;
    stream.receive_next += count;
    if (segment->flags & MAY_TCP_FIN) {
        stream.peer_fin = 1;
        stream.receive_next++;
    }
    if (count || stream.peer_fin) stream.ack_pending = 1;
    stream.last_event = ticks;
    return 1;
}

static void transport_poll(void) {
    if (!active()) return;
    const MayDhcpClient *lease = may_net_dhcp_info();
    if (lease->state != MAY_DHCP_BOUND || !equal(lease->address, stream.local, 4)) {
        fail("MayOS: native TCP lease changed\n");
        return;
    }
    if (stream.phase == PHASE_DRAIN) {
        if (stream.ack_pending && packet(stream.send_next, MAY_TCP_ACK, 0, 0))
            stream.ack_pending = 0;
        if ((u32)(ticks - stream.last_event) >= 200u) {
            stream.phase = PHASE_CLOSED;
            serial("MayOS: native TCP connection closed\n");
        }
        return;
    }
    if (may_tcp_retry_exhausted(&stream.retry, ticks) ||
        (stream.phase < PHASE_OPEN && (u32)(ticks - stream.started) >= 850u) ||
        (stream.phase == PHASE_OPEN && (u32)(ticks - stream.last_event) >= 3000u)) {
        fail("MayOS: native TCP timed out\n");
        return;
    }
    if (stream.phase == PHASE_ARP) {
        if (stream.arp_attempts < 3 && (!stream.arp_attempts ||
            (u32)(ticks - stream.last_arp) >= 75u) && may_stream_send_arp(stream.next_hop)) {
            stream.arp_attempts++;
            stream.last_arp = ticks;
        }
        return;
    }
    if (stream.phase == PHASE_SYN_SEND && tracked(MAY_TCP_SYN, 0, 0))
        stream.phase = PHASE_SYN_SENT;
    if (may_tcp_retry_due(&stream.retry, ticks) &&
        packet(stream.retry.first_seq, stream.retry.flags, stream.tx, stream.retry.payload_length))
        may_tcp_retry_resent(&stream.retry, ticks);
    if (stream.phase != PHASE_OPEN) return;

    if (stream.ack_pending && packet(stream.send_next, MAY_TCP_ACK, 0, 0))
        stream.ack_pending = 0;
    if (stream.close_requested && !stream.local_fin &&
        tracked(MAY_TCP_ACK | MAY_TCP_FIN, 0, 0)) stream.local_fin = 1;
    if (stream.fin_acked && stream.peer_fin && !stream.ack_pending) {
        stream.phase = PHASE_DRAIN;
        stream.last_event = ticks;
    }
}

static int parse_ipv4(const char *text, u8 address[4]) {
    for (unsigned i = 0; i < 4; i++) {
        unsigned value = 0, digits = 0;
        while (*text >= '0' && *text <= '9' && digits < 3) {
            value = value * 10 + (unsigned)(*text++ - '0');
            digits++;
        }
        if (!digits || value > 255 || *text != (i == 3 ? '\0' : '.')) return 0;
        address[i] = (u8)value;
        text++;
    }
    return 1;
}

int may_stream_echo_check(const char *address, const char *port) {
    unsigned value = 0;
    u8 ip[4];
    if (!parse_ipv4(address, ip)) return 0;
    for (; *port; port++) {
        if (*port < '0' || *port > '9') return 0;
        value = value * 10 + (unsigned)(*port - '0');
        if (value > 65535) return 0;
    }
    if (echo_active || !may_stream_open(ip, (u16)value)) return 0;
    echo_active = 1;
    echo_sent = echo_received = 0;
    return 1;
}

void may_stream_poll(void) {
    transport_poll();
    if (!echo_active) return;
    int status = may_stream_status();
    if (status == MAY_STREAM_FAILED || status == MAY_STREAM_CLOSED) {
        echo_active = 0;
        return;
    }
    if (status != MAY_STREAM_READY && status != MAY_STREAM_EOF) return;
    u8 data[STREAM_TX_SIZE];
    if (status == MAY_STREAM_READY && echo_sent < ECHO_BYTES && !stream.retry.active) {
        unsigned bytes = ECHO_BYTES - echo_sent;
        if (bytes > sizeof(data)) bytes = sizeof(data);
        for (unsigned i = 0; i < bytes; i++) data[i] = echo_byte(echo_sent + i);
        echo_sent += (unsigned)may_stream_write(data, bytes);
    }
    int count = may_stream_read(data, sizeof(data));
    for (int i = 0; i < count; i++) {
        if (echo_received >= ECHO_BYTES || echo_byte(echo_received) != data[i]) {
            serial("MayOS: native TCP echo payload mismatch\n");
            echo_active = 0;
            may_stream_close();
            return;
        }
        echo_received++;
    }
    if (echo_received == ECHO_BYTES && echo_sent == ECHO_BYTES) {
        serial("MayOS: native TCP echo verified (8192 bytes)\n");
        echo_active = 0;
        may_stream_close();
    } else if (status == MAY_STREAM_EOF && !count) {
        serial("MayOS: native TCP peer ended the echo early\n");
        echo_active = 0;
        may_stream_close();
    }
}
