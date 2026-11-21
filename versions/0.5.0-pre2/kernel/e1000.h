#ifndef MAYOS_E1000_H
#define MAYOS_E1000_H
#include "core.h"
int e1000_init(void);
int e1000_link_up(void);
const u8 *e1000_mac_address(void);
int e1000_send(const void *frame, u32 length);
int e1000_receive(void *frame, u32 capacity);
#endif
