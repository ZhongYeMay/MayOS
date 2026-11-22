#include "security_trusted_time.h"
#include "core.h"

#ifdef MAYOS_TIME_AUTH
#include "security_entropy_seed.h"
#include "time_pubkey.h"

extern int mayrs_ed25519_verify(const u8 *key, const u8 *signature,
                                const u8 *payload, u32 length);

enum { CHALLENGE_TICKS = 12000u, VALID_TICKS = 2160000u };
static u8 nonce[32], signature[64];
static unsigned signature_size;
static u32 challenge_tick, clock_tick;
static u64 clock_epoch, candidate;
static int challenge_active, clock_active;

void may_time_poll(void) {
    if (clock_active && (u32)(ticks - clock_tick) >= VALID_TICKS) {
        clock_epoch += VALID_TICKS / 100u; /* Retain a rollback floor for this boot. */
        clock_active = 0;
    }
    if (challenge_active && (u32)(ticks - challenge_tick) >= CHALLENGE_TICKS)
        challenge_active = 0;
}

int may_time_available(void) { return 1; }

int may_time_read(unsigned *days, unsigned *seconds) {
    may_time_poll();
    if (!clock_active) return 0;
    u64 utc = clock_epoch + (u32)(ticks - clock_tick) / 100u;
    *days = (unsigned)(utc / 86400u) + 719528u;
    *seconds = (unsigned)(utc % 86400u);
    return 1;
}

int may_time_challenge(char hex[65]) {
    u8 samples[MAY_ENTROPY_SEED_BYTES];
    if (!may_entropy_seed(samples)) return 0;
    memcpy(nonce, samples, sizeof(nonce));
    volatile u8 *discard = samples;
    for (unsigned i = 0; i < sizeof(samples); i++) discard[i] = 0;
    static const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < sizeof(nonce); i++) {
        hex[2 * i] = digits[nonce[i] >> 4];
        hex[2 * i + 1] = digits[nonce[i] & 15u];
    }
    hex[64] = 0;
    challenge_tick = ticks;
    challenge_active = 1;
    signature_size = 0;
    candidate = 0;
    return 1;
}

int may_time_begin(const char *epoch) {
    may_time_poll();
    if (!challenge_active) return 0;
    u64 value = 0;
    for (; *epoch; epoch++) {
        if (*epoch < '0' || *epoch > '9') return 0;
        value = value * 10u + (unsigned)(*epoch - '0');
        if (value >= 4102444800ULL) return 0; /* before 2100 */
    }
    if (value < 1577836800ULL) return 0; /* at least 2020 */
    candidate = value;
    signature_size = 0;
    return 1;
}

static int digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

int may_time_part(const char *hex) {
    may_time_poll();
    if (!challenge_active || !candidate) return 0;
    unsigned length = strlen(hex);
    if (!length || length % 2 || length / 2 > sizeof(signature) - signature_size) return 0;
    for (unsigned i = 0; i < length; i++)
        if (digit(hex[i]) < 0) return 0;
    for (unsigned i = 0; i < length; i += 2)
        signature[signature_size++] = (u8)((digit(hex[i]) << 4) | digit(hex[i + 1]));
    return 1;
}

int may_time_apply(void) {
    may_time_poll();
    if (!challenge_active || signature_size != sizeof(signature)) return 0;
    challenge_active = 0;
    static const char domain[] = "MAYOS-UTC-V1";
    u8 message[sizeof(domain) - 1 + sizeof(nonce) + 8];
    memcpy(message, domain, sizeof(domain) - 1);
    memcpy(message + sizeof(domain) - 1, nonce, sizeof(nonce));
    for (unsigned i = 0; i < 8; i++)
        message[sizeof(message) - 1 - i] = (u8)(candidate >> (i * 8));
    u64 floor = clock_epoch;
    if (clock_active) floor += (u32)(ticks - clock_tick) / 100u;
    if (candidate < floor || !mayrs_ed25519_verify(mayos_time_key, signature, message, sizeof(message)))
        return 0;
    clock_epoch = candidate;
    clock_tick = ticks;
    clock_active = 1;
    return 1;
}
#else
int may_time_read(unsigned *days, unsigned *seconds) { (void)days; (void)seconds; return 0; }
int may_time_available(void) { return 0; }
void may_time_poll(void) {}
int may_time_challenge(char hex[65]) { (void)hex; return 0; }
int may_time_begin(const char *epoch) { (void)epoch; return 0; }
int may_time_part(const char *hex) { (void)hex; return 0; }
int may_time_apply(void) { return 0; }
#endif
