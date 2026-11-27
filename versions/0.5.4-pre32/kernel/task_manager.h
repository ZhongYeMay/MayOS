#ifndef MAYOS_TASK_MANAGER_H
#define MAYOS_TASK_MANAGER_H

#include "core.h"

#if defined(MAYOS_RUST_CORE) || defined(MAYOS_LINUX_TASK_MANAGER)
#define MAYOS_TASK_MANAGER 1
int may_task_poll(u32 ticks, u32 visible_windows);
void may_task_draw(int x, int y, int w, int h, int dark);
void may_task_click(int x, int y, int w, int h);
void may_task_key(int key, int height);
#ifdef MAYOS_LINUX
void may_task_cancel(void);
#endif
#endif

#define MAYOS_KEY_TASK_MANAGER 0x107
#endif
