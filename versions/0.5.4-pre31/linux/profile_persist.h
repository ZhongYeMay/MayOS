#ifndef MAYOS_LINUX_PROFILE_PERSIST_H
#define MAYOS_LINUX_PROFILE_PERSIST_H
#include "../kernel/core.h"
/* Explicit, opt-in Linux profile. NO raw disk access and NO autoformat. */
int mayos_linux_profile_load(File records[FS_MAX], u8 prefs[32], int *prefs_valid);
int mayos_linux_profile_save(const File records[FS_MAX], const u8 prefs[32], int prefs_valid);
int mayos_linux_profile_enabled(void);
#endif
