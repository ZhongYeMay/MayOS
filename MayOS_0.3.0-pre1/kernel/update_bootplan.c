#include "update_bootplan.h"

static unsigned int checksum(const MayBootRecord *r) {
    /* CRC-32 (IEEE), explicit little-endian encoding, no structure padding.
     * Detects torn writes; not a cryptographic signature. */
    unsigned int crc = 0xffffffffu;
    const unsigned int words[6] = {
        r->magic, r->format, r->generation,
        r->confirmed_slot, r->candidate_slot, r->attempts_remaining
    };
    for (int i=0;i<6;++i) for (int j=0;j<4;++j) {
        unsigned int x = (words[i] >> (8*j)) & 255u;
        crc ^= x;
        for (int bit=0;bit<8;++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return ~crc;
}
static int slot_valid(unsigned int slot) { return slot <= MAY_BOOT_SLOT_B; }
static MayBootRecord seal(MayBootRecord r) { r.crc32 = checksum(&r); return r; }
MayBootRecord may_boot_default(unsigned int slot) {
    MayBootRecord out = {0};
    if (!slot_valid(slot)) return out;
    out.magic=MAY_BOOT_MAGIC;out.format=MAY_BOOT_FORMAT;
    out.generation=1u;out.confirmed_slot=slot;
    out.candidate_slot=MAY_BOOT_NONE;out.attempts_remaining=0;
    return seal(out);
}
int may_boot_valid(const MayBootRecord *r) {
    if (!r || r->magic!=MAY_BOOT_MAGIC || r->format!=MAY_BOOT_FORMAT ||
        r->generation == 0 || !slot_valid(r->confirmed_slot) ||
        r->attempts_remaining > MAY_BOOT_ATTEMPTS)
        return 0;
    if (r->candidate_slot==MAY_BOOT_NONE) {
        if (r->attempts_remaining) return 0;
    } else if (!slot_valid(r->candidate_slot) ||
               r->candidate_slot==r->confirmed_slot) return 0;
    return r->crc32==checksum(r);
}
int may_boot_choose_redundant(const MayBootRecord *a,const MayBootRecord *b,
                              MayBootRecord *out) {
    int av=may_boot_valid(a),bv=may_boot_valid(b);
    if (!out || (!av&&!bv)) return 0;
    *out = av && (!bv || a->generation >= b->generation) ? *a : *b;
    return 1;
}
int may_boot_propose(const MayBootRecord *r,unsigned int target,
                     MayBootRecord *next) {
    if (!next || !may_boot_valid(r) || !slot_valid(target) ||
        target==r->confirmed_slot || r->candidate_slot!=MAY_BOOT_NONE ||
        r->generation==0xffffffffu) return 0;
    MayBootRecord copy=*r;
    copy.generation++;
    copy.candidate_slot=target;
    copy.attempts_remaining=MAY_BOOT_ATTEMPTS;
    *next=seal(copy);
    return 1;
}
int may_boot_select(const MayBootRecord *r,MayBootRecord *next,
                    unsigned int *target) {
    if (!next || !target || !may_boot_valid(r) || r->generation==0xffffffffu)
        return 0;
    MayBootRecord copy=*r;
    copy.generation++;
    if (r->candidate_slot!=MAY_BOOT_NONE && r->attempts_remaining) {
        /* A/B boot candidate; reduce retry counter BEFORE booting it. */
        *target=r->candidate_slot;
        copy.attempts_remaining--;
    } else if (r->candidate_slot!=MAY_BOOT_NONE) {
        /* Candidate boot did not confirm: leave known-good slot active. */
        *target=r->confirmed_slot;
        copy.candidate_slot=MAY_BOOT_NONE;
    } else {
        *target=r->confirmed_slot;
    }
    *next=seal(copy);
    return 1;
}
int may_boot_confirm(const MayBootRecord *r,unsigned int actual,
                     MayBootRecord *next) {
    if (!next || !may_boot_valid(r) || !slot_valid(actual) ||
        r->candidate_slot!=actual || r->generation==0xffffffffu)
        return 0;
    MayBootRecord copy=*r;
    copy.generation++;
    copy.confirmed_slot=actual;
    copy.candidate_slot=MAY_BOOT_NONE;
    copy.attempts_remaining=0;
    *next=seal(copy);
    return 1;
}
