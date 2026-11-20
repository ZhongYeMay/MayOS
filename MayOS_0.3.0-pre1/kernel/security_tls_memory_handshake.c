/*
 * TEST-ONLY BearSSL TLS 1.2 client/server memory-record handshake.
 *
 * Both endpoints execute inside the native MayKernel/QEMU process; records
 * are passed in memory via BearSSL's real sendrec/recvrec engines. Ephemeral
 * RSA test-server key and CA are generated at CI build time and embedded
 * ONLY in this isolated diagnostic binary. Test seeds/time are NOT trusted
 * or suitable for a production HTTPS update client.
 *
 * NO TCP, Internet, GitHub, bootloader or installer functionality is enabled.
 *
 * Avoid including core.h: its legacy size_t-like libc prototypes are not
 * compatible with BearSSL's standard-C ABI.
 */
#ifdef MAYOS_TLS_HANDSHAKE_DIAGNOSTIC
#include "bearssl.h"
#include "tls_x509_fixture.h"
#include "tls_handshake_fixture.h"

extern void serial(const char *message);

static br_ssl_client_context client;
static br_ssl_server_context server;
static br_x509_minimal_context x509;
static br_x509_trust_anchor trust_anchor;
static br_x509_certificate chain[1];
static br_rsa_private_key key;
static unsigned char client_in[BR_SSL_BUFSIZE_INPUT];
static unsigned char client_out[BR_SSL_BUFSIZE_OUTPUT];
static unsigned char server_in[BR_SSL_BUFSIZE_INPUT];
static unsigned char server_out[BR_SSL_BUFSIZE_OUTPUT];

static void prepare_material(void) {
    trust_anchor.dn.data=mayos_test_ca_dn;
    trust_anchor.dn.len=sizeof(mayos_test_ca_dn);
    trust_anchor.flags=BR_X509_TA_CA;
    trust_anchor.pkey.key_type=BR_KEYTYPE_RSA;
    trust_anchor.pkey.key.rsa.n=mayos_test_ca_rsa_modulus;
    trust_anchor.pkey.key.rsa.nlen=sizeof(mayos_test_ca_rsa_modulus);
    trust_anchor.pkey.key.rsa.e=mayos_test_ca_rsa_exponent;
    trust_anchor.pkey.key.rsa.elen=sizeof(mayos_test_ca_rsa_exponent);

    chain[0].data=mayos_test_leaf_der;
    chain[0].data_len=sizeof(mayos_test_leaf_der);

    key.n_bitlen=MAYOS_TEST_TLS_RSA_BITS;
    key.p=mayos_test_tls_p;key.plen=sizeof(mayos_test_tls_p);
    key.q=mayos_test_tls_q;key.qlen=sizeof(mayos_test_tls_q);
    key.dp=mayos_test_tls_dp;key.dplen=sizeof(mayos_test_tls_dp);
    key.dq=mayos_test_tls_dq;key.dqlen=sizeof(mayos_test_tls_dq);
    key.iq=mayos_test_tls_iq;key.iqlen=sizeof(mayos_test_tls_iq);
}
/* Moves only genuine TLS *record* bytes, never pretend to succeed with
 * plaintext. The destination controls its own input-buffer capacity. */
static int record_forward(br_ssl_engine_context *source,
                          br_ssl_engine_context *destination) {
    if(!(br_ssl_engine_current_state(source)&BR_SSL_SENDREC)||
       !(br_ssl_engine_current_state(destination)&BR_SSL_RECVREC))
        return 0;
    size_t count=0,space=0;
    unsigned char *src=br_ssl_engine_sendrec_buf(source,&count);
    unsigned char *dst=br_ssl_engine_recvrec_buf(destination,&space);
    if(!src||!dst||!count||!space)return 0;
    size_t n=count<space?count:space;
    if(n>1024)n=1024; /* deliberately fragmented transfer */
    for(size_t i=0;i<n;i++)dst[i]=src[i];
    br_ssl_engine_recvrec_ack(destination,n);
    br_ssl_engine_sendrec_ack(source,n);
    return 1;
}
static int begin_peers(const char *hostname,unsigned advance_days,int known_ca) {
    br_ssl_client_init_full(&client,&x509,
        known_ca?&trust_anchor:0,known_ca?1:0);
    br_ssl_server_init_full_rsa(&server,chain,1,&key);
    br_ssl_engine_set_versions(&client.eng,BR_TLS12,BR_TLS12);
    br_ssl_engine_set_versions(&server.eng,BR_TLS12,BR_TLS12);

    br_ssl_engine_set_buffers_bidi(&client.eng,client_in,sizeof(client_in),
                                  client_out,sizeof(client_out));
    br_ssl_engine_set_buffers_bidi(&server.eng,server_in,sizeof(server_in),
                                  server_out,sizeof(server_out));
    br_x509_minimal_set_time(&x509,
        MAYOS_TLS_FIXTURE_DAYS+advance_days,MAYOS_TLS_FIXTURE_SECONDS);

    /* The seed is ephemeral test data embedded in a test-only guest. It
     * is NOT a real system DRBG or entropy assurance. */
    br_ssl_engine_inject_entropy(&client.eng,mayos_test_tls_client_seed,
                                 sizeof(mayos_test_tls_client_seed));
    br_ssl_engine_inject_entropy(&server.eng,mayos_test_tls_server_seed,
                                 sizeof(mayos_test_tls_server_seed));
    if(!br_ssl_client_reset(&client,hostname,0)||
       !br_ssl_server_reset(&server))return 0;
    return 1;
}
static int run_handshake(const char *hostname,unsigned time_advance,int known_ca,
                         int require_application_record) {
    if(!begin_peers(hostname,time_advance,known_ca))return 0;
    int handshaken=0;
    for(unsigned attempts=0;attempts<100000u;attempts++) {
        unsigned cs=br_ssl_engine_current_state(&client.eng);
        unsigned ss=br_ssl_engine_current_state(&server.eng);
        if((cs&BR_SSL_CLOSED)||(ss&BR_SSL_CLOSED))return 0;
        if((cs&BR_SSL_SENDAPP)&&(ss&BR_SSL_SENDAPP)) {
            handshaken=1;
            break;
        }
        int moved=record_forward(&client.eng,&server.eng);
        moved|=record_forward(&server.eng,&client.eng);
        if(!moved)return 0;
    }
    if(!handshaken)return 0;
    if(!require_application_record)return 1;

    static const unsigned char msg[]="MAYOS-TLS-RECORD-VERIFIED";
    size_t available=0;
    unsigned char *send=br_ssl_engine_sendapp_buf(&client.eng,&available);
    if(!send||available<sizeof(msg)-1u)return 0;
    for(size_t i=0;i<sizeof(msg)-1u;i++)send[i]=msg[i];
    br_ssl_engine_sendapp_ack(&client.eng,sizeof(msg)-1u);
    br_ssl_engine_flush(&client.eng,0);

    for(unsigned attempts=0;attempts<100000u;attempts++) {
        unsigned cs=br_ssl_engine_current_state(&client.eng);
        unsigned ss=br_ssl_engine_current_state(&server.eng);
        if((cs&BR_SSL_CLOSED)||(ss&BR_SSL_CLOSED))return 0;
        if(ss&BR_SSL_RECVAPP) {
            size_t n=0;
            unsigned char *got=br_ssl_engine_recvapp_buf(&server.eng,&n);
            if(!got||n!=sizeof(msg)-1u)return 0;
            for(size_t i=0;i<n;i++)
                if(got[i]!=msg[i])return 0;
            br_ssl_engine_recvapp_ack(&server.eng,n);
            return 1;
        }
        int moved=record_forward(&client.eng,&server.eng);
        moved|=record_forward(&server.eng,&client.eng);
        if(!moved)return 0;
    }
    return 0;
}
int may_tls_memory_handshake_selftest(void) {
    prepare_material();
    if(!run_handshake("test.mayos.local",0,1,1)) {
        serial("MayOS: REAL BearSSL test TLS 1.2 handshake FAILED (NO INTERNET)\n");
        return 0;
    }
    serial("MayOS: BearSSL TLS 1.2 verified test-CA handshake + encrypted record PASSED (MEMORY ONLY)\n");

    if(run_handshake("wrong.mayos.local",0,1,0)||
       run_handshake("test.mayos.local",40,1,0)||
       run_handshake("test.mayos.local",0,0,0)) {
        serial("MayOS: BearSSL memory TLS negative-certificate test FAILED\n");
        return 0;
    }
    serial("MayOS: BearSSL TLS bad hostname, expired cert and unknown CA all REJECTED (MEMORY ONLY)\n");
    return 1;
}
#endif
