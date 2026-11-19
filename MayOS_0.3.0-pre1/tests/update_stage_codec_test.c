#include "update_stage_codec.h"
#include <stdio.h>
#include <assert.h>

static MayStageRecord initial(u32 size) {
    MayStageRecord s={0};
    s.generation=1;
    s.total_bytes=size;
    s.stored_bytes=0;
    s.phase=MAY_STAGE_DOWNLOADING;
    for(u32 i=0;i<32u;i++)s.digest[i]=(u8)(i*11u+1u);
    return s;
}
static void test_header_and_mirrors(void) {
    MayStageRecord first=initial(65536u),selected;
    u8 a[512],b[512];
    u32 lba=0;
    may_stage_encode(a,&first);
    assert(may_stage_decode(a,&selected));
    assert(selected.generation==1u && selected.total_bytes==65536u);
    assert(!may_stage_choose(a,(const u8[512]){0},&selected,&lba)==0);
    /* First is valid; second blank means the first is selected. */
    assert(may_stage_choose(a,(const u8[512]){0},&selected,&lba));
    assert(lba==MAY_STAGE_HEADER_A);
    MayStageRecord second=first;
    second.generation=2;second.stored_bytes=8192u;
    may_stage_encode(b,&second);
    assert(may_stage_choose(a,b,&selected,&lba));
    assert(lba==MAY_STAGE_HEADER_B && selected.stored_bytes==8192u);
    b[56]^=0x80;
    assert(!may_stage_decode(b,&selected));
    assert(may_stage_choose(a,b,&selected,&lba));
    assert(lba==MAY_STAGE_HEADER_A && selected.stored_bytes==0);
    may_stage_encode(b,&second);
    second.generation=1; /* contradictory mirrors at equal generation */
    may_stage_encode(b,&second);
    assert(!may_stage_choose(a,b,&selected,&lba));
    second.generation=3; second.phase=MAY_STAGE_HASH_VALID;
    second.stored_bytes=second.total_bytes;
    may_stage_encode(b,&second);
    assert(may_stage_choose(a,b,&selected,&lba));
    assert(selected.phase==MAY_STAGE_HASH_VALID);
}
static void test_append_constraints(void) {
    MayStageRecord s=initial(65537u);
    assert(!may_stage_plan_append(&s,0));
    assert(!may_stage_plan_append(&s,4097));
    assert(!may_stage_plan_append(&s,123)); /* only final may be partial */
    assert(may_stage_plan_append(&s,4096u));
    s.stored_bytes=65536u;
    assert(may_stage_plan_append(&s,1u));
    assert(!may_stage_plan_append(&s,2u));
    s.stored_bytes=65537u;
    assert(!may_stage_plan_append(&s,1u));
    s=initial(8192u);
    s.phase=MAY_STAGE_HASH_VALID;
    assert(!may_stage_plan_append(&s,512u));
    s.phase=MAY_STAGE_DOWNLOADING;
    s.stored_bytes=1;
    assert(!may_stage_plan_append(&s,512u));
    s=initial(MAY_STAGE_CAPACITY+1);
    u8 invalid[512],decoded_dummy[512];
    may_stage_encode(invalid,&s);
    assert(!may_stage_decode(invalid,&s));
    (void)decoded_dummy;
}
static void test_corruption_and_overflow(void) {
    MayStageRecord j=initial(MAY_STAGE_CAPACITY),out;
    u8 a[512];
    may_stage_encode(a,&j);
    a[60]=1;assert(!may_stage_decode(a,&out));
    may_stage_encode(a,&j);a[20]=9;assert(!may_stage_decode(a,&out));
    may_stage_encode(a,&j);a[0]='X';assert(!may_stage_decode(a,&out));
    j.generation=0xffffffffu;
    assert(!may_stage_plan_append(&j,512u));
    j.generation=1u;j.stored_bytes=512u;
    assert(may_stage_plan_append(&j,512u));
    j.stored_bytes=j.total_bytes;
    j.phase=MAY_STAGE_HASH_VALID;
    may_stage_encode(a,&j);
    assert(may_stage_decode(a,&out));
}
int main(void) {
    test_header_and_mirrors();
    test_append_constraints();
    test_corruption_and_overflow();
    puts("PASS: bounded large-image header codec, mirrored recovery, torn sectors and append arithmetic");
    return 0;
}
