#include "security_clock.h"
#include "io.h"

/* RTC wall clock is untrusted: never set trusted=1 in this module. */
static u8 rtc_read_register(u8 reg) {
    outb(0x70,(u8)(reg|0x80u)); /* NMI disabled only during CMOS access */
    u8 v=inb(0x71);
    outb(0x70,0x00u);
    return v;
}
static u8 bcd(u8 x) {
    return (u8)((x & 15u)+10u*(x>>4));
}
static int leap(u16 y) {
    return ((y%4u)==0u && (y%100u)!=0u) || (y%400u)==0u;
}
int may_rtc_calendar_valid(const MayRtcObservation *t) {
    if(!t || t->year<2020u || t->year>2099u ||
       t->month<1u || t->month>12u ||
       t->hour>23u || t->minute>59u || t->second>59u)
        return 0;
    static const u8 days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    u8 maximum=days[t->month-1u];
    if(t->month==2u && leap(t->year))maximum=29u;
    return t->day>=1u && t->day<=maximum;
}
static int rtc_snapshot(MayRtcObservation *t) {
    for(u32 i=0;i<10000u;i++)
        if(!(rtc_read_register(0x0au)&0x80u))break;
    if(rtc_read_register(0x0au)&0x80u)return 0;
    u8 second=rtc_read_register(0x00u);
    u8 minute=rtc_read_register(0x02u);
    u8 hour=rtc_read_register(0x04u);
    u8 day=rtc_read_register(0x07u);
    u8 month=rtc_read_register(0x08u);
    u8 year=rtc_read_register(0x09u);
    u8 mode=rtc_read_register(0x0bu);
    u8 pm=hour&0x80u;
    hour&=0x7fu;
    if(!(mode&0x04u)) {
        second=bcd(second); minute=bcd(minute);
        hour=bcd(hour); day=bcd(day);
        month=bcd(month); year=bcd(year);
    }
    if(!(mode&0x02u)) {
        if(hour==12u)hour=0;
        if(pm)hour=(u8)(hour+12u);
    }
    t->year=(u16)(2000u+year);/* no reliable CMOS century register */
    t->month=month;t->day=day;
    t->hour=hour;t->minute=minute;t->second=second;
    t->valid=(u8)may_rtc_calendar_valid(t);
    t->trusted=0;/* a CMOS clock cannot establish trusted TLS time */
    return t->valid;
}
int may_rtc_read_observed(MayRtcObservation *t) {
    if(!t)return 0;
    memset(t,0,sizeof(*t));
    MayRtcObservation a={0},b={0};
    /* Avoid a split timestamp while the RTC increments between reads. */
    for(u32 tries=0;tries<4u;tries++) {
        if(!rtc_snapshot(&a)||!rtc_snapshot(&b))continue;
        if(a.year==b.year && a.month==b.month && a.day==b.day &&
           a.hour==b.hour && a.minute==b.minute &&
           a.second==b.second) {
            *t=b;
            return 1;
        }
    }
    return 0;
}
