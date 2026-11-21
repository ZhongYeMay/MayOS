#ifndef MAYOS_SECURITY_CLOCK_H
#define MAYOS_SECURITY_CLOCK_H
#include "core.h"
/*
 * CMOS RTC is only an observation of local firmware time. It is NOT trusted
 * wall time: firmware can be wrong, CMOS settings can be user-modified and
 * the timezone is unspecified. Never use this alone for TLS certificate dates.
 */
typedef struct {
    u16 year;
    u8 month,day,hour,minute,second;
    u8 valid, trusted;
} MayRtcObservation;
int may_rtc_calendar_valid(const MayRtcObservation *t);
int may_rtc_read_observed(MayRtcObservation *t);
#endif
