#include "update.h"
#include "version.h"
#include "sha256.h"
#if defined(MAYOS_RELEASE_SIGNATURE) && defined(MAYOS_STAGE_DISK_DIAGNOSTIC)
#include "update_stage_disk.h"
#endif
#ifdef MAYOS_RELEASE_SIGNATURE
#include "rust_bridge.h"
#include "release_pubkey.h"
#endif
#ifdef MAYOS_RUST_CORE
#include "rust_bridge.h"
#endif

/* This parser deliberately has no network or block-device write dependency.
 * All input is bounded by File.size, not a trusted NUL terminator. */
static MayUpdateInfo update;
static int verify_state;
/* This is an observation only. No updater/install code is enabled. */
static int auth_state;
enum {
    AUTH_NOT_CHECKED=0, AUTH_NO_RELEASE_ROOT, AUTH_INVALID_MANIFEST,
    AUTH_MISSING_SIGNATURE, AUTH_BAD_SIGNATURE_FILE,
    AUTH_SIGNATURE_MISMATCH, AUTH_IMAGE_MISMATCH, AUTH_VALID
};

static int printable_copy(char *dst, u32 cap, const char *value) {
    u32 n = 0;
    if (!value[0]) return 0;
    while (value[n]) {
        u8 c = (u8)value[n];
        if (c < 32 || c > 126 || n + 1 >= cap) return 0;
        dst[n] = (char)c;
        n++;
    }
    dst[n] = 0;
    return 1;
}

static int decimal(const char *s, u32 *out) {
    u32 value = 0;
    if (!*s) return 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return 0;
        u32 digit = (u32)(*s - '0');
        if (value > (0xffffffffu - digit) / 10u) return 0;
        value = value * 10u + digit;
    }
    *out = value;
    return 1;
}

static int date_valid(const char *s) {
    if (strlen(s) != 10 || s[4] != '-' || s[7] != '-') return 0;
    for (int i = 0; i < 10; i++)
        if (i != 4 && i != 7 && (s[i] < '0' || s[i] > '9'))
            return 0;
    int year = (s[0] - '0') * 1000 + (s[1] - '0') * 100 +
               (s[2] - '0') * 10 + (s[3] - '0');
    int month = (s[5] - '0') * 10 + (s[6] - '0');
    int day = (s[8] - '0') * 10 + (s[9] - '0');
    if (year < 2023 || month < 1 || month > 12 || day < 1) return 0;
    static const u8 days_in_month[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    int maximum = days_in_month[month - 1];
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) ||
                       year % 400 == 0)) maximum = 29;
    return day <= maximum;
}

static int hex256_valid(const char *s) {
    if (strlen(s) != 64) return 0;
    for (int i = 0; i < 64; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) return 0;
    }
    return 1;
}

typedef struct {
    u32 major, minor, patch, pre;
    int is_final, is_dev;
} ParsedVersion;

static int next_number(const char **cursor, u32 *out) {
    const char *start = *cursor;
    u32 v = 0;
    while (**cursor >= '0' && **cursor <= '9') {
        u32 d = (u32)(**cursor - '0');
        if (v > (1000000u - d) / 10u) return 0;
        v = v * 10 + d;
        (*cursor)++;
    }
    if (*cursor == start) return 0;
    *out = v;
    return 1;
}

static int version_parse(const char *s, ParsedVersion *v) {
    memset(v, 0, sizeof(*v));
    if (!next_number(&s, &v->major) || *s++ != '.' ||
        !next_number(&s, &v->minor) || *s++ != '.' ||
        !next_number(&s, &v->patch)) return 0;
    if (*s == 0) {
        v->is_final = 1;
        return 1;
    }
    if (s[0] != '-' || s[1] != 'p' || s[2] != 'r' || s[3] != 'e')
        return 0;
    s += 4;
    if (!next_number(&s, &v->pre)) return 0;
    if (*s == 0) return 1;
    if (s[0] == '-' && s[1] == 'd' && s[2] == 'e' &&
        s[3] == 'v' && s[4] == 0) {
        v->is_dev = 1;
        return 1;
    }
    return 0;
}

static int compare_versions(const char *candidate, const char *current, int *result) {
    ParsedVersion a, b;
    if (!version_parse(candidate, &a) || !version_parse(current, &b))
        return 0;
#define CMP(field) do { if (a.field != b.field) { \
    *result = a.field > b.field ? 1 : -1; return 1; } } while (0)
    CMP(major);
    CMP(minor);
    CMP(patch);
    CMP(is_final);
    if (!a.is_final) {
        CMP(pre);
        /* pre2-dev < pre2 < pre3 < final */
        if (a.is_dev != b.is_dev) {
            *result = a.is_dev ? -1 : 1;
            return 1;
        }
    }
#undef CMP
    *result = 0;
    return 1;
}

static int parse_pair(char *line, u32 *seen) {
    char *eq = line;
    while (*eq && *eq != '=') eq++;
    if (!*eq || eq == line || !eq[1]) return 0;
    *eq++ = 0;
    u32 mask = 0;
    int ok = 0;
    if (!strcmp(line, "name")) {
        mask = 1u << 0;
        ok = printable_copy(update.name, sizeof(update.name), eq);
    } else if (!strcmp(line, "version")) {
        mask = 1u << 1;
        ok = printable_copy(update.version, sizeof(update.version), eq);
    } else if (!strcmp(line, "architecture")) {
        mask = 1u << 2;
        ok = printable_copy(update.architecture, sizeof(update.architecture), eq);
    } else if (!strcmp(line, "kernel")) {
        mask = 1u << 3;
        ok = printable_copy(update.kernel, sizeof(update.kernel), eq);
    } else if (!strcmp(line, "release_date")) {
        mask = 1u << 4;
        ok = printable_copy(update.release_date, sizeof(update.release_date), eq);
    } else if (!strcmp(line, "size_bytes")) {
        mask = 1u << 5;
        ok = decimal(eq, &update.size_bytes) && update.size_bytes > 0;
    } else if (!strcmp(line, "sha256")) {
        mask = 1u << 6;
        ok = printable_copy(update.sha256, sizeof(update.sha256), eq);
    } else if (!strcmp(line, "eta_minutes")) {
        mask = 1u << 7;
        ok = decimal(eq, &update.eta_minutes) &&
             update.eta_minutes >= 1 && update.eta_minutes <= 1440;
    } else if (!strcmp(line, "changelog")) {
        mask = 1u << 8;
        ok = printable_copy(update.changelog, sizeof(update.changelog), eq);
    } else {
        return 0;
    }
    if (!ok || (*seen & mask)) return 0;
    *seen |= mask;
    return 1;
}

void may_update_check(void) {
    memset(&update, 0, sizeof(update));
    verify_state=MAY_VERIFY_NOT_CHECKED;
    auth_state=AUTH_NOT_CHECKED;
    int index = fs_find(1, "Update.may");
    if (index < 0) {
        update.state = MAY_UPDATE_MISSING;
        return;
    }
    update.state = MAY_UPDATE_INVALID;
    if (!files[index].used || files[index].dir ||
        files[index].size == 0 || files[index].size > 2048u)
        return;
    const u8 *data = files[index].data;
#ifdef MAYOS_RUST_CORE
    /* Independent no_std Rust safety gate before the legacy C parser.
     * It never grants trust, installation permission, or network access. */
    if (mayrs_abi_version() != 1u ||
        !mayrs_manifest_gate(data, files[index].size)) {
        serial("MayOS: Rust update manifest gate rejected untrusted metadata\n");
        return;
    }
    serial("MayOS: Rust no_std manifest gate passed (UNTRUSTED)\n");
#endif
    u32 seen = 0, offset = 0;
    while (offset < files[index].size) {
        char line[160];
        u32 n = 0;
        while (offset < files[index].size && data[offset] != '\n') {
            u8 ch = data[offset++];
            if (ch == 0 || n + 1 >= sizeof(line)) return;
            line[n++] = (char)ch;
        }
        if (offset < files[index].size) offset++;
        if (n && line[n - 1] == '\r') n--;
        line[n] = 0;
        if (!n || line[0] == '#') continue;
        if (!parse_pair(line, &seen)) return;
    }
    if (seen != 0x1ffu ||
        strcmp(update.name, "MayOS") ||
        strcmp(update.architecture, "x86_64") ||
        strcmp(update.kernel, "MayKernel") ||
        !date_valid(update.release_date) ||
        !hex256_valid(update.sha256)) return;
    int comparison = 0;
    if (!compare_versions(update.version, MAYOS_VERSION, &comparison)) return;
    update.state = comparison > 0 ? MAY_UPDATE_CANDIDATE : MAY_UPDATE_CURRENT;
}

const MayUpdateInfo *may_update_info(void) {
    return &update;
}

const char *may_update_status(void) {
    switch (update.state) {
        case MAY_UPDATE_MISSING: return "No local manifest in Documents/Update.may";
        case MAY_UPDATE_INVALID: return "Manifest invalid or incompatible (no changes made)";
        case MAY_UPDATE_CURRENT: return "No newer version in local manifest";
        case MAY_UPDATE_CANDIDATE: return "Newer version found (UNVERIFIED metadata)";
        default: return "Not checked - local only; GitHub unavailable";
    }
}


int may_update_verify_local(void) {
    verify_state=MAY_VERIFY_NOT_READY;
    if (update.state!=MAY_UPDATE_CANDIDATE &&
        update.state!=MAY_UPDATE_CURRENT) return 0;
    int index=fs_find(1,"Update.bin");
    if (index<0) {
        verify_state=MAY_VERIFY_FILE_MISSING;
        return 0;
    }
    verify_state=MAY_VERIFY_SIZE_MISMATCH;
    if (!files[index].used || files[index].dir ||
        files[index].size!=update.size_bytes ||
        files[index].size>FILE_CAP) return 0;
    MaySha256 sha;
    u8 digest[32];
    char hex[65];
    may_sha256_init(&sha);
    may_sha256_update(&sha,files[index].data,files[index].size);
    may_sha256_final(&sha,digest);
    may_sha256_hex(digest,hex);
    u8 difference=0;
    for(u32 i=0;i<64u;i++) {
        char expected=update.sha256[i];
        if(expected>='A' && expected<='F')expected=(char)(expected-'A'+'a');
        difference|=(u8)(hex[i]^expected);
    }
    verify_state=difference ? MAY_VERIFY_HASH_MISMATCH : MAY_VERIFY_HASH_MATCH;
    return verify_state==MAY_VERIFY_HASH_MATCH;
}
const char *may_update_verify_status(void) {
    switch(verify_state) {
        case MAY_VERIFY_NOT_READY:
            return "Verify: run update check first (valid local manifest)";
        case MAY_VERIFY_FILE_MISSING:
            return "Verify: Documents/Update.bin was not found";
        case MAY_VERIFY_SIZE_MISMATCH:
            return "Verify: local file size differs from untrusted metadata";
        case MAY_VERIFY_HASH_MISMATCH:
            return "Verify: SHA-256 mismatch; file was not accepted";
        case MAY_VERIFY_HASH_MATCH:
            return "Verify: SHA-256 matches UNTRUSTED manifest; NOT signed";
        default:
            return "Verify: no local image has been checked";
    }
}


int may_update_authenticate_local(void) {
#ifndef MAYOS_RELEASE_SIGNATURE
    auth_state=AUTH_NO_RELEASE_ROOT;
    return 0;
#else
    /* Re-parse the exact current MayFS metadata before authenticating it.
     * Otherwise a stale unsigned check could leave a different SHA-256 in
     * the cached C struct than the newly signed manifest (TOCTOU flaw). */
    may_update_check();
    auth_state=AUTH_INVALID_MANIFEST;
    if(update.state!=MAY_UPDATE_CANDIDATE &&
       update.state!=MAY_UPDATE_CURRENT)return 0;
    int manifest=fs_find(1,"Update.may");
    if(manifest<0 || !files[manifest].used || files[manifest].dir ||
       files[manifest].size==0 || files[manifest].size>2048u)
        return 0;
    auth_state=AUTH_MISSING_SIGNATURE;
    int signature=fs_find(1,"Update.sig");
    if(signature<0)return 0;
    auth_state=AUTH_BAD_SIGNATURE_FILE;
    if(!files[signature].used || files[signature].dir ||
       files[signature].size!=64u)return 0;

    auth_state=AUTH_SIGNATURE_MISMATCH;
    /* The key is configured by the OS build owner, never read from MayFS.
     * The whole manifest, including the image hash, is authenticated.
     * The Rust library implements strict Ed25519 verification and is
     * compiled only in an explicitly opted-in signed-test build. */
    if(!mayrs_ed25519_verify(mayos_release_root_key,
                            files[signature].data,
                            files[manifest].data,files[manifest].size))
        return 0;
    auth_state=AUTH_IMAGE_MISMATCH;
    /* Rehash the actual MayFS payload now; signature covers ONLY metadata. */
    if(!may_update_verify_local())return 0;
    auth_state=AUTH_VALID;
    serial("MayOS: local Ed25519 manifest signature and image digest verified; NO INSTALL\n");
    return 1;
#endif
}
const char *may_update_auth_status(void) {
    switch(auth_state) {
        case AUTH_NO_RELEASE_ROOT:
            return "Authenticity: disabled (no owner-pinned release key)";
        case AUTH_INVALID_MANIFEST:
            return "Authenticity: valid update metadata required first";
        case AUTH_MISSING_SIGNATURE:
            return "Authenticity: Documents/Update.sig missing";
        case AUTH_BAD_SIGNATURE_FILE:
            return "Authenticity: detached signature must be exactly 64 bytes";
        case AUTH_SIGNATURE_MISMATCH:
            return "Authenticity: Ed25519 signature REJECTED";
        case AUTH_IMAGE_MISMATCH:
            return "Authenticity: signed metadata OK, image digest/size invalid";
        case AUTH_VALID:
#ifdef MAYOS_RELEASE_TEST_KEY
            return "Authenticity: TEST KEY match ONLY (not official), install DISABLED";
#else
            return "Authenticity: configured key + image verified, install DISABLED";
#endif
        default:
            return "Authenticity: not checked; install DISABLED";
    }
}


/* The signed staged-image verification is ALWAYS read-only and is compiled
 * only for an explicitly marked QEMU test disk. Never use this as installer
 * authorization: TLS, bootloader, disk geometry and owner key rotation remain
 * security gates. The normal MayKernel build returns disabled.
 */
static int staged_auth_state;
enum {
    STAGED_AUTH_NOT_RUN=0,STAGED_AUTH_DISABLED,STAGED_AUTH_METADATA_INVALID,
    STAGED_AUTH_SIG_MISSING,STAGED_AUTH_SIG_INVALID,
    STAGED_AUTH_HASH_MISMATCH,STAGED_AUTH_SUCCESS
};
#if defined(MAYOS_RELEASE_SIGNATURE) && defined(MAYOS_STAGE_DISK_DIAGNOSTIC)
static int nibble(char ch) {
    if(ch>='0'&&ch<='9')return ch-'0';
    if(ch>='a'&&ch<='f')return ch-'a'+10;
    if(ch>='A'&&ch<='F')return ch-'A'+10;
    return -1;
}
#endif
int may_update_authenticate_staged(void) {
#if defined(MAYOS_RELEASE_SIGNATURE) && defined(MAYOS_STAGE_DISK_DIAGNOSTIC)
    /* Never use previously cached unsigned metadata. Always parse the
     * exact current manifest before Ed25519 and full ATA SHA-256 checks. */
    may_update_check();
    staged_auth_state=STAGED_AUTH_METADATA_INVALID;
    if(update.state!=MAY_UPDATE_CANDIDATE &&
       update.state!=MAY_UPDATE_CURRENT)return 0;
    int manifest=fs_find(1,"Update.may");
    if(manifest<0||!files[manifest].used||files[manifest].dir||
       !files[manifest].size||files[manifest].size>2048u)return 0;
    staged_auth_state=STAGED_AUTH_SIG_MISSING;
    int signature=fs_find(1,"Update.sig");
    if(signature<0||!files[signature].used||files[signature].dir||
       files[signature].size!=64u)return 0;
    staged_auth_state=STAGED_AUTH_SIG_INVALID;
    /* Trust root is the build-pinned Ed25519 public key, NOT a key from
     * Update.may, a GitHub redirect or an unsigned TLS response. */
    if(!mayrs_ed25519_verify(mayos_release_root_key,
                            files[signature].data,
                            files[manifest].data,files[manifest].size))
        return 0;
    u8 expected[32];
    for(u32 i=0;i<32u;i++) {
        int high=nibble(update.sha256[2u*i]);
        int low=nibble(update.sha256[2u*i+1u]);
        if(high<0||low<0)return 0;
        expected[i]=(u8)((high<<4)|low);
    }
    staged_auth_state=STAGED_AUTH_HASH_MISMATCH;
    /* Re-read and hash ACTUAL raw ATA bytes now, even if an older staging
     * header said VERIFIED. This protects against post-verify mutations. */
    int ok=may_stage_diag_matches_signed(update.size_bytes,expected);
    memset(expected,0,sizeof(expected));
    if(!ok)return 0;
    staged_auth_state=STAGED_AUTH_SUCCESS;
    serial("MayOS: signed manifest + staged ATA data authenticated; NO INSTALL\n");
    return 1;
#else
    staged_auth_state=STAGED_AUTH_DISABLED;
    return 0;
#endif
}
const char *may_update_auth_staged_status(void) {
    switch(staged_auth_state) {
        case STAGED_AUTH_DISABLED:
            return "Signed staging: DISABLED (diagnostic-only; no installer)";
        case STAGED_AUTH_METADATA_INVALID:
            return "Signed staging: invalid/incompatible manifest";
        case STAGED_AUTH_SIG_MISSING:
            return "Signed staging: detached signature missing/invalid length";
        case STAGED_AUTH_SIG_INVALID:
            return "Signed staging: publisher Ed25519 signature REJECTED";
        case STAGED_AUTH_HASH_MISMATCH:
            return "Signed staging: signed digest differs from ATA staged payload";
        case STAGED_AUTH_SUCCESS:
            return "Signed staging: Ed25519 + full disk image matched; NO INSTALL";
        default:
            return "Signed staging: not checked; NO INSTALL";
    }
}
