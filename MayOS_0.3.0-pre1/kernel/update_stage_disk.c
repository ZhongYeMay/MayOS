#include "core.h"
#include "io.h"
#include "sha256.h"
#include "update_stage_codec.h"

/* STRICTLY NON-RELEASE DIAGNOSTIC.
 * Real 8 MiB QEMU ATA image, with magic MayFS marker, two staging headers
 * (LBA 1536/1537), and 512KiB data area (LBA 2048..3071).
 * No actual network download, signature verification, install, or boot switch.
 * All diagnostic commands are removed from regular MayKernel builds.
 */
#ifdef MAYOS_STAGE_DISK_DIAGNOSTIC
#define STAGE_TEST_DISK_SECTORS 16384u
#define STAGE_FIXTURE_BYTES 65536u
static u8 header_a[512],header_b[512],sector_data[512],sector_readback[512];
static u8 staging_chunk[4096];
static MayStageRecord current;
static u32 active_lba;

static int io_ready(int read_data) {
    for(u32 i=0;i<1000000u;i++) {
        u8 status=inb(0x1f7);
        if(!status||status==255u)return 0;
        if((status&0x80u)==0u) {
            if(status&0x21u)return 0;
            if(!read_data||(status&0x08u))return 1;
        }
    }
    return 0;
}
static u32 disk_capacity(void) {
    outb(0x1f6,0xe0u);
    for(int i=0;i<4;i++)(void)inb(0x3f6);
    outb(0x1f7,0xecu);
    if(!io_ready(1))return 0;
    u16 ids[256];
    for(int i=0;i<256;i++)ids[i]=inw(0x1f0);
    return (u32)ids[60]|((u32)ids[61]<<16);
}
static int permitted_lba(u32 lba) {
    return lba==MAY_STAGE_HEADER_A||lba==MAY_STAGE_HEADER_B||
           (lba>=MAY_STAGE_DATA_LBA &&
            lba<MAY_STAGE_DATA_LBA+MAY_STAGE_DATA_SECTORS);
}
static int sector_io(u32 lba,u8 *buffer,int write) {
    if(!permitted_lba(lba)||lba>=STAGE_TEST_DISK_SECTORS)return 0;
    outb(0x1f6,(u8)(0xe0u|((lba>>24)&15u)));
    for(int i=0;i<4;i++)(void)inb(0x3f6);
    if(!io_ready(0))return 0;
    outb(0x1f2,1u);
    outb(0x1f3,(u8)lba);
    outb(0x1f4,(u8)(lba>>8));
    outb(0x1f5,(u8)(lba>>16));
    outb(0x1f7,write?0x30u:0x20u);
    if(!io_ready(1))return 0;
    for(u32 i=0;i<256u;i++) {
        if(write)outw(0x1f0,(u16)buffer[2u*i]|((u16)buffer[2u*i+1u]<<8));
        else {
            u16 word=inw(0x1f0);
            buffer[2u*i]=(u8)word;
            buffer[2u*i+1u]=(u8)(word>>8);
        }
    }
    return io_ready(0);
}
static int flush_disk(void) {
    outb(0x1f7,0xe7u);
    return io_ready(0);
}
static int equals(const u8 *a,const u8 *b,u32 n) {
    for(u32 i=0;i<n;i++)if(a[i]!=b[i])return 0;
    return 1;
}
static int blank(const u8 *s) {
    for(u32 i=0;i<512u;i++)if(s[i])return 0;
    return 1;
}
static int owned_header(const u8 *s) {
    return equals(s,(const u8 *)"MAYSTG01",8u);
}
static int test_disk_allowed(void) {
    static const char marker[]="ISOLATED-QEMU-UPDATE-STAGE-ONLY";
    if(!disk_ready||disk_capacity()!=STAGE_TEST_DISK_SECTORS)return 0;
    int idx=fs_find(1,"STAGE-DIAGNOSTIC.marker");
    if(idx<0||!files[idx].used||files[idx].dir||
       files[idx].size!=(u32)(sizeof(marker)-1u))return 0;
    return equals(files[idx].data,(const u8 *)marker,sizeof(marker)-1u);
}
static int read_headers(void) {
    if(!test_disk_allowed()) {
        serial("MayOS: staging REFUSED (not a marked isolated 8MiB QEMU disk)\n");
        return 0;
    }
    if(!sector_io(MAY_STAGE_HEADER_A,header_a,0)||
       !sector_io(MAY_STAGE_HEADER_B,header_b,0))return 0;
    if((!blank(header_a)&&!owned_header(header_a))||
       (!blank(header_b)&&!owned_header(header_b))) {
        serial("MayOS: staging REFUSED (foreign header data preserved)\n");
        return 0;
    }
    return 1;
}
static int load_record(void) {
    if(!read_headers())return 0;
    return may_stage_choose(header_a,header_b,&current,&active_lba);
}
static int write_header(u32 lba,const MayStageRecord *record) {
    if(lba!=MAY_STAGE_HEADER_A&&lba!=MAY_STAGE_HEADER_B)return 0;
    may_stage_encode(sector_data,record);
    if(!owned_header(sector_data))return 0;
    if(!sector_io(lba,sector_data,1)||!flush_disk()||
       !sector_io(lba,sector_readback,0)||
       !equals(sector_data,sector_readback,512u))return 0;
    return 1;
}
static int verify_blank_staging_area(void) {
    for(u32 i=0;i<MAY_STAGE_DATA_SECTORS;i++) {
        if(!sector_io(MAY_STAGE_DATA_LBA+i,sector_data,0)||
           !blank(sector_data))return 0;
    }
    return 1;
}
int may_stage_diag_begin(u32 total,const u8 digest[32]) {
    if(!digest||total<=4096u||total>MAY_STAGE_CAPACITY||
       !read_headers()||!blank(header_a)||!blank(header_b)||
       !verify_blank_staging_area()) {
        serial("MayOS: staging begin REFUSED (reserved sectors must be blank)\n");
        return 0;
    }
    MayStageRecord record={0};
    record.generation=1u;
    record.total_bytes=total;
    record.phase=MAY_STAGE_DOWNLOADING;
    for(u32 i=0;i<32u;i++)record.digest[i]=digest[i];
    if(!write_header(MAY_STAGE_HEADER_A,&record))return 0;
    serial("MayOS: staging header initialized; ATA FLUSH confirmed; NO INSTALL\n");
    return 1;
}
int may_stage_diag_append(const u8 *chunk,u32 length) {
    if(!chunk||!load_record()||
       !may_stage_plan_append(&current,length))return 0;
    /* Data is never overwritten within the previous committed prefix.
     * A torn/uncommitted tail may be retried at the same sector-aligned offset. */
    u32 base=MAY_STAGE_DATA_LBA+current.stored_bytes/512u;
    for(u32 written=0;written<length;written+=512u) {
        u32 n=length-written;
        if(n>512u)n=512u;
        memset(sector_data,0,sizeof(sector_data));
        memcpy(sector_data,chunk+written,n);
        if(!sector_io(base+written/512u,sector_data,1))return 0;
    }
    /* Flush all data BEFORE advancing the committed header offset. */
    if(!flush_disk())return 0;
    for(u32 written=0;written<length;written+=512u) {
        u32 n=length-written;
        if(n>512u)n=512u;
        if(!sector_io(base+written/512u,sector_readback,0)||
           !equals(sector_readback,chunk+written,n))return 0;
        for(u32 j=n;j<512u;j++)if(sector_readback[j])return 0;
    }
    MayStageRecord next=current;
    next.generation++;
    next.stored_bytes+=length;
    /* Alternative journal copy is rewritten while current stays valid. */
    u32 target=active_lba==MAY_STAGE_HEADER_A?
               MAY_STAGE_HEADER_B:MAY_STAGE_HEADER_A;
    return write_header(target,&next);
}
int may_stage_diag_verify(void) {
    if(!load_record()||current.phase!=MAY_STAGE_DOWNLOADING||
       current.stored_bytes!=current.total_bytes||
       current.generation==0xffffffffu)return 0;
    MaySha256 ctx;
    may_sha256_init(&ctx);
    for(u32 offset=0;offset<current.total_bytes;offset+=512u) {
        if(!sector_io(MAY_STAGE_DATA_LBA+offset/512u,sector_data,0))return 0;
        u32 n=current.total_bytes-offset;
        if(n>512u)n=512u;
        may_sha256_update(&ctx,sector_data,n);
    }
    u8 digest[32];
    may_sha256_final(&ctx,digest);
    u8 diff=0;
    for(u32 i=0;i<32u;i++)diff|=(u8)(digest[i]^current.digest[i]);
    memset(digest,0,sizeof(digest));
    if(diff) {
        serial("MayOS: staging SHA-256 MISMATCH (NO INSTALL)\n");
        return 0;
    }
    MayStageRecord next=current;
    next.generation++;
    next.phase=MAY_STAGE_HASH_VALID;
    u32 target=active_lba==MAY_STAGE_HEADER_A?
               MAY_STAGE_HEADER_B:MAY_STAGE_HEADER_A;
    if(!write_header(target,&next))return 0;
    serial("MayOS: staged image SHA-256 verified from ATA disk; NO INSTALL\n");
    return 1;
}
int may_stage_diag_status(void) {
    if(!load_record()) {
        serial("MayOS: staging has no valid persistent header\n");
        return 0;
    }
    char n[24];
    serial("MayOS: staging recovered bytes=");
    number(n,current.stored_bytes);serial(n);
    serial("/");
    number(n,current.total_bytes);serial(n);
    serial(" generation=");
    number(n,current.generation);serial(n);
    serial(current.phase==MAY_STAGE_HASH_VALID?
        " SHA-256=VALID (NOT SIGNED; NO INSTALL)\n":
        " SHA-256=NOT-CHECKED (NO INSTALL)\n");
    return 1;
}

/* Explicitly synthetic data source: replace with an authenticated network
 * streaming client in a future milestone. This is NOT a GitHub download. */
static u8 fixture_byte(u32 i) {
    return (u8)(((i*13u)^(i>>5u)^0xa5u)&255u);
}
static void fixture_block(u32 base,u32 length) {
    for(u32 i=0;i<length;i++)staging_chunk[i]=fixture_byte(base+i);
}
int may_stage_diag_fixture_begin(void) {
    MaySha256 hash;
    may_sha256_init(&hash);
    for(u32 offset=0;offset<STAGE_FIXTURE_BYTES;offset+=4096u) {
        fixture_block(offset,4096u);
        may_sha256_update(&hash,staging_chunk,4096u);
    }
    u8 digest[32];
    may_sha256_final(&hash,digest);
    return may_stage_diag_begin(STAGE_FIXTURE_BYTES,digest);
}
int may_stage_diag_fixture_append(u32 maximum_chunks) {
    if(!maximum_chunks||maximum_chunks>16u||!load_record()||
       current.total_bytes!=STAGE_FIXTURE_BYTES)return 0;
    u32 done=0;
    while(done<maximum_chunks && current.stored_bytes<current.total_bytes) {
        u32 start=current.stored_bytes;
        fixture_block(start,4096u);
        if(!may_stage_diag_append(staging_chunk,4096u))return 0;
        if(!load_record()||current.stored_bytes!=start+4096u)return 0;
        done++;
    }
    if(!done)return 0;
    return may_stage_diag_status();
}
#endif
