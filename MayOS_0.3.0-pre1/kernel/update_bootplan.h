#ifndef MAYOS_UPDATE_BOOTPLAN_H
#define MAYOS_UPDATE_BOOTPLAN_H
/* Pure A/B boot selection model for MayKernel future transactional installer.
 * NO disk writes, partition selection, bootloader modification or reboot.
 * A real installer must validate staged SHA-256, slot boundaries and durable
 * mirrored metadata BEFORE using any boot decision from this model. */
#define MAY_BOOT_SLOT_A 0u
#define MAY_BOOT_SLOT_B 1u
#define MAY_BOOT_NONE 0xffffffffu
#define MAY_BOOT_MAGIC 0x4d415942u
#define MAY_BOOT_FORMAT 1u
#define MAY_BOOT_ATTEMPTS 2u

typedef struct {
    unsigned int magic;
    unsigned int format;
    unsigned int generation;
    unsigned int confirmed_slot;
    unsigned int candidate_slot;
    unsigned int attempts_remaining;
    unsigned int crc32;
} MayBootRecord;

MayBootRecord may_boot_default(unsigned int boot_slot);
int may_boot_valid(const MayBootRecord *record);
int may_boot_choose_redundant(const MayBootRecord *a, const MayBootRecord *b,
                              MayBootRecord *out);
/* Both operations return 0 on invalid metadata without modifying input. */
int may_boot_propose(const MayBootRecord *current, unsigned int inactive_slot,
                     MayBootRecord *next);
/* The *next record must be durably saved before the bootloader tries target. */
int may_boot_select(const MayBootRecord *current, MayBootRecord *next,
                    unsigned int *boot_target);
int may_boot_confirm(const MayBootRecord *current, unsigned int actual_boot_slot,
                     MayBootRecord *next);
#endif
