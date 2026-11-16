#include <assert.h>
#include <stdio.h>
#include "update_bootplan.h"

static void reject_invalid(void) {
    MayBootRecord bad = {0};
    assert(!may_boot_valid(&bad));
    assert(!may_boot_valid(0));
    MayBootRecord a=may_boot_default(MAY_BOOT_SLOT_A);
    assert(may_boot_valid(&a));
    assert(!may_boot_valid(&(MayBootRecord){0}));
    bad=a; bad.attempts_remaining=5;
    assert(!may_boot_valid(&bad));
    bad=a; bad.crc32^=1u;
    assert(!may_boot_valid(&bad));
    bad=a; bad.confirmed_slot=2;
    assert(!may_boot_valid(&bad));
    assert(!may_boot_valid(&(MayBootRecord){.magic=MAY_BOOT_MAGIC,
        .format=MAY_BOOT_FORMAT,.confirmed_slot=0,.generation=0}));

}

static void boot_and_rollback(void) {
    MayBootRecord a=may_boot_default(MAY_BOOT_SLOT_A),next={0},working={0};
    unsigned int target=99;
    assert(may_boot_valid(&a));
    assert(!may_boot_propose(&a,MAY_BOOT_SLOT_A,&next)); /* no overwrite of live slot */
    assert(may_boot_select(&a,&next,&target) && target==MAY_BOOT_SLOT_A);
    assert(may_boot_valid(&next));
    assert(may_boot_propose(&a,MAY_BOOT_SLOT_B,&working));
    assert(may_boot_valid(&working) && working.confirmed_slot==MAY_BOOT_SLOT_A);
    assert(working.candidate_slot==MAY_BOOT_SLOT_B && working.attempts_remaining==2);
    assert(!may_boot_confirm(&working,MAY_BOOT_SLOT_A,&next));
    assert(may_boot_select(&working,&next,&target));
    assert(target==MAY_BOOT_SLOT_B && next.attempts_remaining==1);
    assert(may_boot_valid(&next));
    working=next;
    assert(may_boot_select(&working,&next,&target));
    assert(target==MAY_BOOT_SLOT_B && next.attempts_remaining==0);
    working=next;
    assert(may_boot_select(&working,&next,&target));
    assert(target==MAY_BOOT_SLOT_A && next.candidate_slot==MAY_BOOT_NONE);
    assert(may_boot_valid(&next) && next.confirmed_slot==MAY_BOOT_SLOT_A);
    assert(!may_boot_confirm(&next,MAY_BOOT_SLOT_B,&working));
    /* Successfully booted slot B is confirmed only after a health check. */
    assert(may_boot_propose(&next,MAY_BOOT_SLOT_B,&working));
    assert(may_boot_select(&working,&next,&target) && target==MAY_BOOT_SLOT_B);
    working=next;
    assert(may_boot_confirm(&working,MAY_BOOT_SLOT_B,&next));
    assert(may_boot_valid(&next) && next.confirmed_slot==MAY_BOOT_SLOT_B);
    assert(next.candidate_slot==MAY_BOOT_NONE && next.attempts_remaining==0);
}
static void mirrored_metadata_recovery(void) {
    MayBootRecord a=may_boot_default(MAY_BOOT_SLOT_A),b={0},newest={0};
    assert(may_boot_propose(&a,MAY_BOOT_SLOT_B,&b));
    assert(may_boot_choose_redundant(&a,&b,&newest));
    assert(newest.generation==b.generation);
    MayBootRecord torn=b;
    torn.crc32 ^= 0x100u; /* one interrupted record copy */
    assert(may_boot_choose_redundant(&a,&torn,&newest));
    assert(newest.generation==a.generation && newest.confirmed_slot==MAY_BOOT_SLOT_A);
    MayBootRecord bad=a;
    bad.crc32^=1u;
    assert(!may_boot_choose_redundant(&bad,&torn,&newest));
    assert(!may_boot_propose(&bad,MAY_BOOT_SLOT_B,&newest));
}
int main(void) {
    reject_invalid();
    boot_and_rollback();
    mirrored_metadata_recovery();
    puts("PASS: A/B boot model, two-attempt rollback and torn-record recovery (in-memory only)");
    return 0;
}
