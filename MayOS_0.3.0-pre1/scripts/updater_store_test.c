#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "update_store.h"

typedef struct {
    unsigned char block[2][MAY_UPDATE_STORE_BYTES];
    int faulty_write, faulty_flush, faulty_read;
    int writes;
} FakeStorage;
static int read_slot(void *user,unsigned int slot,unsigned char *out){
    FakeStorage *f=user;
    if(slot>1u||f->faulty_read)return 0;
    memcpy(out,f->block[slot],MAY_UPDATE_STORE_BYTES);return 1;
}
static int write_slot(void *user,unsigned int slot,const unsigned char *input){
    FakeStorage *f=user;
    if(slot>1u)return 0;
    f->writes++;
    if(f->faulty_write==1){ /* interrupted 13-byte sector write */
        memcpy(f->block[slot],input,13);return 0;
    }
    memcpy(f->block[slot],input,MAY_UPDATE_STORE_BYTES);
    if(f->faulty_write==2)f->block[slot][9]^=0x80; /* silent torn content */
    return 1;
}
static int flush_slot(void *user){
    FakeStorage *f=user;return !f->faulty_flush;
}
static void factory_record(FakeStorage *f,const MayBootRecord *r,unsigned int slot){
    const unsigned int words[7]={r->magic,r->format,r->generation,
        r->confirmed_slot,r->candidate_slot,r->attempts_remaining,r->crc32};
    assert(slot<=1u);
    for(int i=0;i<7;++i)
        for(int k=0;k<4;++k)
            f->block[slot][4*i+k]=(unsigned char)(words[i]>>(8*k));
}
static void tests(void){
    FakeStorage f={0};
    MayUpdateStoreIO io={read_slot,write_slot,flush_slot,&f};
    MayBootRecord current={0},next={0},again={0};
    assert(!may_update_store_load(&io,&current)); /* no fabricated valid boot */
    MayBootRecord baseline=may_boot_default(MAY_BOOT_SLOT_A);
    assert(may_boot_valid(&baseline));
    factory_record(&f,&baseline,0); /* explicit trusted factory provision */
    assert(may_update_store_load(&io,&current));
    assert(current.confirmed_slot==MAY_BOOT_SLOT_A);
    assert(may_boot_propose(&current,MAY_BOOT_SLOT_B,&next));

    /* Unexpected copy corruption and a failed sync never overwrite copy 0. */
    f.faulty_write=1;
    assert(!may_update_store_commit(&io,&next));
    assert(may_update_store_load(&io,&again) &&
           again.generation==baseline.generation &&
           again.confirmed_slot==MAY_BOOT_SLOT_A);
    f.faulty_write=2;
    assert(!may_update_store_commit(&io,&next));
    assert(may_update_store_load(&io,&again) &&
           again.generation==baseline.generation);
    f.faulty_write=0;
    assert(may_update_store_commit(&io,&next));
    assert(may_update_store_load(&io,&again) && again.generation==2u &&
           again.candidate_slot==MAY_BOOT_SLOT_B && f.writes==3);
    /* Replaying a stale generation cannot advance metadata. */
    assert(!may_update_store_commit(&io,&next));

    unsigned int target=99;
    current=again;
    assert(may_boot_select(&current,&next,&target));
    assert(target==MAY_BOOT_SLOT_B && next.attempts_remaining==1u);
    assert(may_update_store_commit(&io,&next));
    assert(may_update_store_load(&io,&again) && again.generation==3u);
    /* Corrupt the latest metadata copy: still recover generation 2. */
    f.block[0][12]^=1u;
    assert(may_update_store_load(&io,&again) && again.generation==2u);
    /* Neither copy is usable: refuse to select an arbitrary boot slot. */
    f.block[1][24]^=2u;
    assert(!may_update_store_load(&io,&again));

    /* Equal-generation disagreement must fail closed, never pick randomly. */
    FakeStorage conflict={0};
    MayUpdateStoreIO other={read_slot,write_slot,flush_slot,&conflict};
    MayBootRecord slotb=may_boot_default(MAY_BOOT_SLOT_B);
    factory_record(&conflict,&baseline,0);
    factory_record(&conflict,&slotb,1);
    assert(!may_update_store_load(&other,&again));
    printf("PASS: mirrored boot metadata, CRC integrity, monotonic updates, "
           "torn writes and power-loss recovery model (isolated test storage)\n");
}
int main(void){tests();return 0;}
