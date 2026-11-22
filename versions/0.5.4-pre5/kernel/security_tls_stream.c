#include "security_tls_stream.h"
#include "net_stream_io.h"

extern volatile unsigned int ticks;

static br_ssl_client_context client;
static br_x509_minimal_context x509;
static unsigned char engine_buffer[BR_SSL_BUFSIZE_BIDI];
static int state, error, close_pending;
static unsigned int started;

static size_t minimum(size_t first, size_t second) {
    return first < second ? first : second;
}

static void finish(int next, int code) {
    state = next;
    error = code;
    may_stream_close();
}

int may_tls_stream_open(const unsigned char address[4], unsigned short port,
                        const char *hostname, const br_x509_trust_anchor *anchors,
                        size_t count, unsigned days, unsigned seconds,
                        const unsigned char *seed, size_t seed_length) {
    if (state == MAY_TLS_STREAM_HANDSHAKE || state == MAY_TLS_STREAM_READY ||
        !hostname || !*hostname || !anchors || !count || !days || seconds >= 86400 ||
        !seed || seed_length < 32) return 0;
    static const uint16_t suites[] = {
        BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
        BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
        BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
        BR_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
    };
    br_ssl_client_init_full(&client, &x509, anchors, count);
    br_ssl_engine_set_suites(&client.eng, suites, sizeof(suites) / sizeof(suites[0]));
    br_ssl_engine_set_versions(&client.eng, BR_TLS12, BR_TLS12);
    br_ssl_engine_add_flags(&client.eng, BR_OPT_NO_RENEGOTIATION);
    br_ssl_engine_set_buffer(&client.eng, engine_buffer, sizeof(engine_buffer), 1);
    br_x509_minimal_set_time(&x509, days, seconds);
    br_ssl_engine_inject_entropy(&client.eng, seed, seed_length);
    if (!br_ssl_client_reset(&client, hostname, 0) || !may_stream_open(address, port)) {
        error = br_ssl_engine_last_error(&client.eng);
        return 0;
    }
    state = MAY_TLS_STREAM_HANDSHAKE;
    started = ticks;
    error = close_pending = 0;
    return 1;
}

int may_tls_stream_state(void) { return state; }
int may_tls_stream_error(void) { return error; }

void may_tls_stream_poll(void) {
    if (state != MAY_TLS_STREAM_HANDSHAKE && state != MAY_TLS_STREAM_READY) return;
    for (int step = 0; step < 8; step++) {
        unsigned flags = br_ssl_engine_current_state(&client.eng);
        if (flags & BR_SSL_CLOSED) {
            int code = br_ssl_engine_last_error(&client.eng);
            finish(code ? MAY_TLS_STREAM_FAILED : MAY_TLS_STREAM_CLOSED, code);
            return;
        }
        if (flags & (BR_SSL_SENDAPP | BR_SSL_RECVAPP)) state = MAY_TLS_STREAM_READY;
        int transport = may_stream_status();
        if (transport == MAY_STREAM_FAILED || transport == MAY_STREAM_CLOSED ||
            (state == MAY_TLS_STREAM_HANDSHAKE && (unsigned)(ticks - started) >= 1500u)) {
            finish(MAY_TLS_STREAM_FAILED, -1);
            return;
        }
        if (transport == MAY_STREAM_CONNECTING) return;

        size_t space;
        int moved = 0;
        if (flags & BR_SSL_SENDREC) {
            const unsigned char *data = br_ssl_engine_sendrec_buf(&client.eng, &space);
            int bytes = may_stream_write(data, (unsigned)minimum(space, 512));
            if (bytes > 0) {
                br_ssl_engine_sendrec_ack(&client.eng, (size_t)bytes);
                moved = 1;
            }
        }
        flags = br_ssl_engine_current_state(&client.eng);
        if (flags & BR_SSL_RECVREC) {
            unsigned char *data = br_ssl_engine_recvrec_buf(&client.eng, &space);
            int bytes = may_stream_read(data, (unsigned)minimum(space, 4096));
            if (bytes > 0) {
                br_ssl_engine_recvrec_ack(&client.eng, (size_t)bytes);
                moved = 1;
            } else if (transport == MAY_STREAM_EOF) {
                finish(MAY_TLS_STREAM_FAILED, -2); /* No authenticated TLS close alert. */
                return;
            }
        }
        if (!moved) return;
    }
}

size_t may_tls_stream_read(unsigned char *output, size_t capacity) {
    if (state != MAY_TLS_STREAM_READY ||
        !(br_ssl_engine_current_state(&client.eng) & BR_SSL_RECVAPP)) return 0;
    size_t available;
    const unsigned char *data = br_ssl_engine_recvapp_buf(&client.eng, &available);
    size_t bytes = minimum(available, capacity);
    for (size_t i = 0; i < bytes; i++) output[i] = data[i];
    if (bytes) br_ssl_engine_recvapp_ack(&client.eng, bytes);
    return bytes;
}

size_t may_tls_stream_write(const unsigned char *data, size_t length) {
    if (state != MAY_TLS_STREAM_READY || close_pending ||
        !(br_ssl_engine_current_state(&client.eng) & BR_SSL_SENDAPP)) return 0;
    size_t available;
    unsigned char *destination = br_ssl_engine_sendapp_buf(&client.eng, &available);
    size_t bytes = minimum(available, length);
    for (size_t i = 0; i < bytes; i++) destination[i] = data[i];
    if (bytes) {
        br_ssl_engine_sendapp_ack(&client.eng, bytes);
        br_ssl_engine_flush(&client.eng, 0);
    }
    return bytes;
}

void may_tls_stream_close(void) {
    if (state != MAY_TLS_STREAM_READY || close_pending) return;
    close_pending = 1;
    br_ssl_engine_close(&client.eng);
}
