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
