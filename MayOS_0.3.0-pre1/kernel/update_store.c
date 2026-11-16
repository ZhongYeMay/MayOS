#include "update_store.h"
static unsigned int decode_word(const unsigned char *b) {
    return (unsigned int)b[0] | ((unsigned int)b[1]<<8) |
           ((unsigned int)b[2]<<16) | ((unsigned int)b[3]<<24);
}
static void encode_word(unsigned char *p,unsigned int v) {
    for(int i=0;i<4;++i)p[i]=(unsigned char)(v>>(8*i));
}
static void store_encode(const MayBootRecord *r,
                         unsigned char block[MAY_UPDATE_STORE_BYTES]) {
    const unsigned int words[7]={
        r->magic,r->format,r->generation,r->confirmed_slot,
        r->candidate_slot,r->attempts_remaining,r->crc32
    };
    for(unsigned int i=0;i<MAY_UPDATE_STORE_BYTES;++i)block[i]=0;
    for(unsigned int i=0;i<7;++i)encode_word(block+i*4,words[i]);
}
static int store_decode(const unsigned char block[MAY_UPDATE_STORE_BYTES],
                        MayBootRecord *r) {
    for(unsigned int i=28;i<MAY_UPDATE_STORE_BYTES;++i)
        if(block[i])return 0; /* no unrecognized metadata/extensions */
    r->magic=decode_word(block);
    r->format=decode_word(block+4);
    r->generation=decode_word(block+8);
    r->confirmed_slot=decode_word(block+12);
    r->candidate_slot=decode_word(block+16);
    r->attempts_remaining=decode_word(block+20);
    r->crc32=decode_word(block+24);
    return may_boot_valid(r);
}
static int record_equal(const MayBootRecord *a,const MayBootRecord *b) {
    return a->magic==b->magic && a->format==b->format &&
           a->generation==b->generation &&
           a->confirmed_slot==b->confirmed_slot &&
           a->candidate_slot==b->candidate_slot &&
           a->attempts_remaining==b->attempts_remaining &&
           a->crc32==b->crc32;
}
static int load_two(const MayUpdateStoreIO *io,MayBootRecord *a,MayBootRecord *b,
                    int *av,int *bv) {
    if(!io || !io->read || !a || !b || !av || !bv)return 0;
    unsigned char block[MAY_UPDATE_STORE_BYTES];
    *av=io->read(io->user,0u,block) && store_decode(block,a);
    *bv=io->read(io->user,1u,block) && store_decode(block,b);
    if(!*av && !*bv)return 0;
    /* Conflicting copies at the same generation cannot be resolved safely. */
    if(*av && *bv && a->generation==b->generation && !record_equal(a,b))
        return 0;
    return 1;
}
int may_update_store_load(const MayUpdateStoreIO *io,MayBootRecord *out) {
    if(!out)return 0;
    MayBootRecord a={0},b={0};
    int av=0,bv=0;
    if(!load_two(io,&a,&b,&av,&bv))return 0;
    if(!av)*out=b;
    else if(!bv||a.generation>=b.generation)*out=a;
    else *out=b;
    return 1;
}
int may_update_store_commit(const MayUpdateStoreIO *io,
                            const MayBootRecord *next) {
    if(!io||!io->read||!io->write||!io->flush||
       !may_boot_valid(next))return 0;
    MayBootRecord a={0},b={0},current={0};
    int av=0,bv=0;
    if(!load_two(io,&a,&b,&av,&bv))return 0;
    unsigned int target;
    if(!av){current=b;target=0u;}
    else if(!bv){current=a;target=1u;}
    else if(a.generation>=b.generation){current=a;target=1u;}
    else{current=b;target=0u;}
    if(current.generation==0xffffffffu ||
       next->generation!=current.generation+1u)return 0;
    unsigned char block[MAY_UPDATE_STORE_BYTES],verify[MAY_UPDATE_STORE_BYTES];
    store_encode(next,block);
    /* The confirmed latest copy is NEVER overwritten by this commit. */
    if(!io->write(io->user,target,block) ||
       !io->flush(io->user) ||
       !io->read(io->user,target,verify))
        return 0;
    MayBootRecord on_disk={0};
    return store_decode(verify,&on_disk) && record_equal(&on_disk,next);
}
