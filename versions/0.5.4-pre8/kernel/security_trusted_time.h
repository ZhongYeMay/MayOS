#ifndef MAYOS_SECURITY_TRUSTED_TIME_H
#define MAYOS_SECURITY_TRUSTED_TIME_H

int may_time_read(unsigned *days, unsigned *seconds);
int may_time_available(void);
void may_time_poll(void);
int may_time_challenge(char hex[65]);
int may_time_begin(const char *epoch);
int may_time_part(const char *hex);
int may_time_apply(void);
#endif
