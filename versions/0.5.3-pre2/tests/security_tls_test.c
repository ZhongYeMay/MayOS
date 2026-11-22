#include "security_tls.h"
#include "security_clock.h"
#include "security_entropy.h"

/* The test intentionally doesn't invoke privileged CMOS I/O or hardware RNG:
 * CPU/RTC observations are tested by real MayKernel QEMU smoke separately. */
void *memset(void *p,int v,u32 n) {
    u8 *x=p;for(u32 i=0;i<n;i++)x[i]=(u8)v;
    return p;
}
u32 strlen(const char *s) {
    u32 n=0;while(s[n])n++;return n;
}
int strcmp(const char *a,const char *b) {
    while(*a&&*a==*b){a++;b++;}
    return (u8)*a-(u8)*b;
}
void serial(const char *s){(void)s;}

int main(void) {
    MayRtcObservation d={0};
    d.year=2028;d.month=2;d.day=29;d.hour=23;d.minute=59;d.second=59;
    if(!may_rtc_calendar_valid(&d))return 1;
    d.year=2026;
    if(may_rtc_calendar_valid(&d))return 2;
    d.month=11;d.day=18;
    if(!may_rtc_calendar_valid(&d))return 3;
    d.month=4;d.day=31;
    if(may_rtc_calendar_valid(&d))return 4;
    d.month=12;d.day=31;d.second=60;
    if(may_rtc_calendar_valid(&d))return 5;
    d.second=1;d.year=2099;
    if(!may_rtc_calendar_valid(&d))return 6;
    d.year=2100;
    if(may_rtc_calendar_valid(&d))return 7;
    u64 test_words[8]={0};
    if(may_entropy_healthcheck(test_words))return 8;
    for(int i=0;i<8;i++)test_words[i]=~(u64)0;
    if(may_entropy_healthcheck(test_words))return 9;
    for(int i=0;i<8;i++)test_words[i]=0x0123456789abcdefULL;
    if(may_entropy_healthcheck(test_words))return 10;
    for(int i=0;i<8;i++)test_words[i]=0x0123456789abcdefULL+(u64)i*17u;
    if(!may_entropy_healthcheck(test_words))return 11;
    if(!may_tls_update_host_allowed("api.github.com") ||
       !may_tls_update_host_allowed("github.com"))return 12;
    const char *bad[]={
        "", "API.GITHUB.COM","api.github.com.attacker.test",
        "https://api.github.com","api.github.com:443",
        "api.github.com/","api.github.com.","github.com.evil",
        "api.github.com@attacker.test",0
    };
    for(int i=0;bad[i];i++)
        if(may_tls_update_host_allowed(bad[i]))return 13;
    if(may_tls_update_preflight("github.com")!=
       MAY_TLS_BLOCK_ENTROPY)return 14;
    if(may_tls_update_preflight("github.com.evil")!=
       MAY_TLS_BLOCK_HOST)return 15;
    if(may_tls_open_verified_update("api.github.com"))return 16;
    if(may_tls_open_verified_update("github.com.evil"))return 17;
    const MayTlsSecurityReport *report=may_tls_security_report();
    if(report->x509_engine_ready||report->authenticated_handshake||
       report->trusted_time_ready||report->cryptographic_drbg_ready)
       return 18;
    return 0;
}
