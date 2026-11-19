#ifndef MAYOS_UPDATE_STAGE_DISK_H
#define MAYOS_UPDATE_STAGE_DISK_H
#include "core.h"
/* ALL symbols in this interface are non-release, marker-guarded QEMU tests.
 * No network, no signature, no installation, no bootloader writes. */
#ifdef MAYOS_STAGE_DISK_DIAGNOSTIC
int may_stage_diag_fixture_begin(void);
int may_stage_diag_fixture_append(u32 maximum_chunks);
int may_stage_diag_status(void);
int may_stage_diag_verify(void);
#endif
#endif
