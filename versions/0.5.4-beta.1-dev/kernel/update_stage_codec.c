#include "update_stage_codec.h"

/* Explicit on-disk LE encoding; independent of compiler struct padding.
 * FNV is a torn-write detector, NOT an authenticity/integrity proof. */
static u32 le32(const u8 *p) {
    return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);
}
static void put32(u8 *p,u32 value) {
    p[0]=(u8)value;p[1]=(u8)(value>>8);
    p[2]=(u8)(value>>16);p[3]=(u8)(value>>24);
}
static u32 fnv(const u8 *p,u32 n) {
    u32 h=2166136261u;
    for(u32 i=0;i<n;i++)h=(h^p[i])*16777619u;
    return h;
}
static int same(const u8 *a,const u8 *b,u32 n) {
    for(u32 i=0;i<n;i++)if(a[i]!=b[i])return 0;
    return 1;
}
static int valid_fields(const MayStageRecord *s) {
    if(!s||!s->generation||s->total_bytes<=4096u||
       s->total_bytes>MAY_STAGE_CAPACITY||
       s->stored_bytes>s->total_bytes||
       (s->phase!=MAY_STAGE_DOWNLOADING&&s->phase!=MAY_STAGE_HASH_VALID))
        return 0;
    if(s->phase==MAY_STAGE_HASH_VALID&&s->stored_bytes!=s->total_bytes)
        return 0;
    if(s->stored_bytes<s->total_bytes &&
       (s->stored_bytes%MAY_STAGE_SECTOR_BYTES)!=0u)
        return 0;
    return 1;
}
void may_stage_encode(u8 out[512],const MayStageRecord *s) {
    for(u32 i=0;i<512u;i++)out[i]=0;
    if(!valid_fields(s))return;
    static const u8 magic[8]={'M','A','Y','S','T','G','0','1'};
    for(u32 i=0;i<8u;i++)out[i]=magic[i];
    put32(out+8,s->generation);
    put32(out+12,s->total_bytes);
    put32(out+16,s->stored_bytes);
    put32(out+20,s->phase);
    for(u32 i=0;i<32u;i++)out[24u+i]=s->digest[i];
    put32(out+56,fnv(out,56u));
}
int may_stage_decode(const u8 bytes[512],MayStageRecord *s) {
    if(!bytes||!s)return 0;
    static const u8 magic[8]={'M','A','Y','S','T','G','0','1'};
    if(!same(bytes,magic,8u)||le32(bytes+56)!=fnv(bytes,56u))
        return 0;
    for(u32 i=60;i<512u;i++)if(bytes[i])return 0;
    MayStageRecord candidate;
    candidate.generation=le32(bytes+8);
    candidate.total_bytes=le32(bytes+12);
    candidate.stored_bytes=le32(bytes+16);
    candidate.phase=le32(bytes+20);
    for(u32 i=0;i<32u;i++)candidate.digest[i]=bytes[24u+i];
    if(!valid_fields(&candidate))return 0;
    *s=candidate;
    return 1;
}
int may_stage_choose(const u8 a[512],const u8 b[512],
                     MayStageRecord *result,u32 *active_header_lba) {
    if(!a||!b||!result||!active_header_lba)return 0;
    MayStageRecord x,y;
    int va=may_stage_decode(a,&x),vb=may_stage_decode(b,&y);
    if(!va&&!vb)return 0;
    if(va&&vb) {
        if(x.generation==y.generation) {
            if(x.total_bytes!=y.total_bytes||x.stored_bytes!=y.stored_bytes||
               x.phase!=y.phase||!same(x.digest,y.digest,32u))
                return 0; /* contradictory equal-generation mirrors */
        }
        if(y.generation>x.generation) {
            *result=y;*active_header_lba=MAY_STAGE_HEADER_B;
            return 1;
        }
    }
    if(va) {
        *result=x;*active_header_lba=MAY_STAGE_HEADER_A;
    } else {
        *result=y;*active_header_lba=MAY_STAGE_HEADER_B;
    }
    return 1;
}
int may_stage_plan_append(const MayStageRecord *s,u32 length) {
    if(!valid_fields(s)||s->phase!=MAY_STAGE_DOWNLOADING||
       !length||length>MAY_STAGE_CHUNK_MAX||
       s->stored_bytes==s->total_bytes||
       length>s->total_bytes-s->stored_bytes)
        return 0;
    /* An unaligned ending is allowed only for the final chunk.
     * This keeps all other recovery checkpoints sector-aligned. */
    if((length%MAY_STAGE_SECTOR_BYTES)!=0u &&
       length!=s->total_bytes-s->stored_bytes)
        return 0;
    if((s->stored_bytes%MAY_STAGE_SECTOR_BYTES)!=0u)return 0;
    if(s->generation==0xffffffffu)return 0;
    return 1;
}
