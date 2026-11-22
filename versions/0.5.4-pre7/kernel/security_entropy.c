#include "security_entropy.h"
/* CPUID detection prevents illegal-instruction faults on older CPUs. */
int may_entropy_healthcheck(const u64 words[8]) {
    if(!words)return 0;
    u64 all_or=0,all_and=~(u64)0;
    int different=0;
    for(int i=0;i<8;i++) {
        all_or|=words[i];
        all_and&=words[i];
        if(i>0 && words[i]!=words[0])different=1;
    }
    return different && all_or!=0 && all_and!=~(u64)0;
}
static int rdrand64(u64 *output) {
    for(int attempt=0;attempt<10;attempt++) {
        u8 ok;
        u64 value;
        __asm__ volatile("rdrand %0; setc %1"
            : "=r"(value),"=qm"(ok)::"cc");
        if(ok) {
            *output=value;
            return 1;
        }
    }
    return 0;
}
int may_entropy_probe_hardware(void) {
    u32 eax=1,ebx=0,ecx=0,edx=0;
    __asm__ volatile("cpuid": "+a"(eax),"=b"(ebx),"=c"(ecx),"=d"(edx));
    if(!(ecx&(1u<<30)))return MAY_ENTROPY_CPU_ABSENT;
    u64 words[8];
    for(int i=0;i<8;i++) {
        if(!rdrand64(&words[i])) {
            memset(words,0,sizeof(words));
            return MAY_ENTROPY_CPU_FAILED;
        }
    }
    int ok=may_entropy_healthcheck(words);
    memset(words,0,sizeof(words));/* do not leak entropy in logs */
    return ok ? MAY_ENTROPY_HW_SAMPLED_UNTRUSTED : MAY_ENTROPY_CPU_FAILED;
}

/* RDSEED is an entropy source for independently seeding a DRBG. RDRAND is
 * still probed separately for diagnostics and cannot satisfy this contract. */
static int rdseed_supported(void) {
    u32 eax = 0, ebx, ecx = 0, edx;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    if (eax < 7) return 0;
    eax = 7;
    ecx = 0;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    return (ebx & (1u << 18)) != 0;
}

static int rdseed_word(u64 *output) {
    /* Carry clear is a transient hardware failure. Bound retries per word. */
    for (unsigned attempt = 0; attempt < 64; attempt++) {
        u64 word;
        u8 ok;
        __asm__ volatile("rdseed %0; setc %1" : "=r"(word), "=qm"(ok) :: "cc");
        if (ok) {
            *output = word;
            return 1;
        }
        __asm__ volatile("pause");
    }
    return 0;
}

static void erase(void *data, unsigned length) {
    volatile u8 *bytes = data;
    while (length--) *bytes++ = 0;
}

int may_entropy_seed(u8 output[MAY_ENTROPY_SEED_BYTES]) {
    if (!output) return 0;
    erase(output, MAY_ENTROPY_SEED_BYTES);
    if (!rdseed_supported()) return 0;

    u64 sample[MAY_ENTROPY_SEED_BYTES / sizeof(u64)] = {0};
    int valid = 1;
    for (unsigned i = 0; i < sizeof(sample) / sizeof(sample[0]); i++) {
        if (!rdseed_word(&sample[i])) {
            valid = 0;
            break;
        }
        for (unsigned j = 0; j < i; j++) {
            if (sample[i] == sample[j]) {
                valid = 0;
                break;
            }
        }
        if (!valid) break;
    }
    if (valid && may_entropy_healthcheck(sample)) {
        const u8 *bytes = (const u8 *)sample;
        for (unsigned i = 0; i < MAY_ENTROPY_SEED_BYTES; i++) output[i] = bytes[i];
    } else valid = 0;
    erase(sample, sizeof(sample));
    return valid;
}
