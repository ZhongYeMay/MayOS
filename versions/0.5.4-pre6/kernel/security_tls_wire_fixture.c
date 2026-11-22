/* Test-only peer, CA, time and host-generated fresh seed material. */
#include "security_tls_stream.h"
#include "tls_x509_fixture.h"
#include "tls_wire_seed.h"

extern void serial(const char *message);
extern void number(char *text, unsigned value);

static br_x509_trust_anchor anchor;
static unsigned char foreign_dn[sizeof(mayos_test_ca_dn)];
static unsigned seed_index;
static int test, request_sent, reply_read, closing;
static const char test_hostname[] = "test.mayos.local";
static const unsigned char request[] = "GET /health HTTP/1.0\r\nHost: test.mayos.local\r\n\r\n";
static const unsigned char reply[] = "HTTP/1.0 200 OK\r\nContent-Length: 8\r\n\r\nMAYOS-OK";

int may_tls_wire_test(const char *scenario, const char *port_text) {
    static const unsigned char host[4] = {172, 31, 44, 9};
    unsigned port = 0;
    for (; *port_text; port_text++) {
        if (*port_text < '0' || *port_text > '9') return 0;
        port = port * 10 + (unsigned)(*port_text - '0');
        if (port > 65535) return 0;
    }
    if (!port || test) return 0;
    int variant = !strcmp(scenario, "ok") ? 1 : !strcmp(scenario, "host") ? 2 :
                  !strcmp(scenario, "expired") ? 3 : !strcmp(scenario, "ca") ? 4 :
                  !strcmp(scenario, "eof") ? 5 : !strcmp(scenario, "cpu") ? 6 : 0;
    if (!variant) return 0;
    (void)sizeof(mayos_test_leaf_der);
    anchor.dn.data = mayos_test_ca_dn;
    anchor.dn.len = sizeof(mayos_test_ca_dn);
    anchor.flags = BR_X509_TA_CA;
    anchor.pkey.key_type = BR_KEYTYPE_RSA;
    anchor.pkey.key.rsa.n = mayos_test_ca_rsa_modulus;
    anchor.pkey.key.rsa.nlen = sizeof(mayos_test_ca_rsa_modulus);
    anchor.pkey.key.rsa.e = mayos_test_ca_rsa_exponent;
    anchor.pkey.key.rsa.elen = sizeof(mayos_test_ca_rsa_exponent);
    if (variant == 4) {
        for (size_t i = 0; i < sizeof(foreign_dn); i++) foreign_dn[i] = mayos_test_ca_dn[i];
        foreign_dn[sizeof(foreign_dn) - 1] ^= 1;
        anchor.dn.data = foreign_dn;
    }
    const unsigned char *seed = 0;
    if (variant != 6) {
        if (seed_index >= sizeof(mayos_wire_seed) / 32) return 0;
        seed = &mayos_wire_seed[seed_index++ * 32];
    }
    if (!may_tls_stream_open(host, (unsigned short)port,
        variant == 2 ? "wrong.mayos.local" : test_hostname, &anchor, 1,
        MAYOS_TLS_FIXTURE_DAYS + (variant == 3 ? 40 : 0),
        MAYOS_TLS_FIXTURE_SECONDS, seed, seed ? 32 : 0)) return 0;
    test = variant;
    request_sent = reply_read = closing = 0;
    return 1;
}

void may_tls_wire_poll(void) {
    may_tls_stream_poll();
    if (!test) return;
    int state = may_tls_stream_state();
    if (state == MAY_TLS_STREAM_FAILED) {
        int error = may_tls_stream_error();
        if (test == 2 && error == BR_ERR_X509_BAD_SERVER_NAME)
            serial("MayOS: TLS wire rejected wrong hostname\n");
        else if (test == 3 && error == BR_ERR_X509_EXPIRED)
            serial("MayOS: TLS wire rejected expired certificate\n");
        else if (test == 4 && error == BR_ERR_X509_NOT_TRUSTED)
            serial("MayOS: TLS wire rejected untrusted CA\n");
        else if (test == 5 && error == -2)
            serial("MayOS: TLS wire rejected truncated transport\n");
        else {
            char code[16];
            number(code, error < 0 ? (unsigned)-error : (unsigned)error);
            serial("MayOS: TLS wire unexpected failure code ");
            serial(code);
            serial("\n");
        }
        test = 0;
        return;
    }
    if (state == MAY_TLS_STREAM_CLOSED) {
        if (test == 6 && closing)
            serial("MayOS: CPU-seeded TLS wire verified HTTP response and TLS close\n");
        else if (test == 1 && closing)
            serial("MayOS: TLS wire verified HTTP response and TLS close\n");
        else serial("MayOS: TLS wire closed before response verification\n");
        test = 0;
        return;
    }
    if (state != MAY_TLS_STREAM_READY) return;
    if (test >= 2 && test <= 4) {
        serial("MayOS: TLS wire incorrectly accepted invalid certificate\n");
        may_tls_stream_close();
        test = 0;
        return;
    }
    const int request_length = sizeof(request) - 1;
    const int reply_length = sizeof(reply) - 1;
    if (request_sent < request_length)
        request_sent += (int)may_tls_stream_write(request + request_sent, request_length - request_sent);
    unsigned char received[64];
    size_t count = may_tls_stream_read(received, sizeof(received));
    for (size_t i = 0; i < count; i++) {
        if (reply_read >= reply_length || received[i] != reply[reply_read++]) {
            serial("MayOS: TLS wire response mismatch\n");
            may_tls_stream_close();
            test = 0;
            return;
        }
    }
    if (reply_read == reply_length && !closing) {
        closing = 1;
        may_tls_stream_close();
    }
}
