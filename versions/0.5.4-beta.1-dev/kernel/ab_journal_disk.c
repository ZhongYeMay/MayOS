#include "core.h"
#include "io.h"
#include "rust_bridge.h"

/* Compiled only in explicitly opted-in QEMU-only diagnostic builds.
 * This must never be linked into production images or used as an installer.
 * MayFS occupies LBA 0..1023; our two test records use LBA 1024 and 1025.
 * The exact 8 MiB capacity AND an explicit MayFS marker are mandatory.
 */
#ifdef MAYOS_AB_DISK_DIAGNOSTIC
#define AB_RECORD_BYTES 48u
#define AB_TEST_SECTORS 16384u
#define AB_LBA_FIRST 1024u
#define AB_LBA_SECOND 1025u
static u8 ab_first[512],ab_second[512],ab_sector[512],ab_readback[512];
static u8 ab_current[AB_RECORD_BYTES],ab_new[AB_RECORD_BYTES];

static int ata_ready(int want_data) {
    for (u32 t=0;t<1000000u;t++) {
        u8 status=inb(0x1f7);
        if(status==0 || status==255)return 0;
        if(!(status&0x80u)) {
            if((status&0x21u)!=0u)return 0;
            if(!want_data || (status&0x08u))return 1;
        }
    }
    return 0;
}
static int ab_ata_capacity(void) {
    u16 ident[256];
    outb(0x1f6,0xe0);
    for(int i=0;i<4;i++)(void)inb(0x3f6);
    outb(0x1f7,0xec);
    if(!ata_ready(1))return 0;
    for(int i=0;i<256;i++)ident[i]=inw(0x1f0);
    return (int)((u32)ident[60]|((u32)ident[61]<<16));
}
static int ab_sector_io(u32 lba,u8 *buffer,int write) {
    if(lba!=AB_LBA_FIRST && lba!=AB_LBA_SECOND)return 0;
    outb(0x1f6,(u8)(0xe0u|((lba>>24)&15u)));
    for(int i=0;i<4;i++)(void)inb(0x3f6);
    if(!ata_ready(0))return 0;
    outb(0x1f2,1);
    outb(0x1f3,(u8)lba);
    outb(0x1f4,(u8)(lba>>8));
    outb(0x1f5,(u8)(lba>>16));
    outb(0x1f7,write?0x30:0x20);
    if(!ata_ready(1))return 0;
    for(u32 i=0;i<256u;i++) {
        if(write)outw(0x1f0,(u16)buffer[2*i]|((u16)buffer[2*i+1]<<8));
        else {
            u16 word=inw(0x1f0);
            buffer[2*i]=(u8)word;
            buffer[2*i+1]=(u8)(word>>8);
        }
    }
    return ata_ready(0);
}
static int ab_flush(void) {
    outb(0x1f7,0xe7); /* ATA FLUSH CACHE; journal persistence barrier */
    return ata_ready(0);
}
static int ab_equal(const u8 *a,const u8 *b,u32 length) {
    for(u32 i=0;i<length;i++)if(a[i]!=b[i])return 0;
    return 1;
}
static int ab_blank(const u8 *bytes) {
    for(u32 i=0;i<512u;i++)if(bytes[i])return 0;
    return 1;
}
static int ab_owned(const u8 *bytes) {
    return ab_equal(bytes,(const u8 *)"MAYABJ01",8u);
}
static int ab_allowed(void) {
    if(!disk_ready || ab_ata_capacity()!=AB_TEST_SECTORS)return 0;
    int idx=fs_find(1,"AB-DIAGNOSTIC.marker");
    static const char key[]="ISOLATED-QEMU-AB-JOURNAL-ONLY";
    if(idx<0 || !files[idx].used || files[idx].dir ||
       files[idx].size!=(u32)(sizeof(key)-1u))return 0;
    return ab_equal(files[idx].data,(const u8 *)key,(u32)(sizeof(key)-1u));
}
static int ab_read_all(void) {
    if(!ab_allowed()) {
        serial("MayOS: AB disk diagnostic REFUSED (not an isolated marked 8MiB test disk)\n");
        return 0;
    }
    if(!ab_sector_io(AB_LBA_FIRST,ab_first,0) ||
       !ab_sector_io(AB_LBA_SECOND,ab_second,0))return 0;
    if((!ab_blank(ab_first)&&!ab_owned(ab_first)) ||
       (!ab_blank(ab_second)&&!ab_owned(ab_second))) {
        serial("MayOS: AB disk diagnostic REFUSED (reserved sectors contain foreign data)\n");
        return 0;
    }
    return 1;
}
static u32 ab_select(u8 *out) {
    const u8 *left=ab_owned(ab_first)?ab_first+16u:ab_first;
    const u8 *right=ab_owned(ab_second)?ab_second+16u:ab_second;
    return mayrs_ab_disk_select(left,right,out,AB_RECORD_BYTES);
}
static void ab_seal_sector(const u8 *record) {
    memset(ab_sector,0,512);
    memcpy(ab_sector,"MAYABJ01",8u);
    ab_sector[8]=1;ab_sector[12]=(u8)~1u;
    memcpy(ab_sector+16u,record,AB_RECORD_BYTES);
}
static int ab_write_blank(u32 lba,const u8 *record) {
    const u8 *slot=(lba==AB_LBA_FIRST)?ab_first:ab_second;
    if(!ab_blank(slot))return 0; /* never replace last known-good record */
    ab_seal_sector(record);
    if(!ab_sector_io(lba,ab_sector,1) || !ab_flush() ||
       !ab_sector_io(lba,ab_readback,0) ||
       !ab_equal(ab_sector,ab_readback,512u))return 0;
    return 1;
}
static void ab_log_generation(u32 generation,const u8 *record) {
    char digits[24];
    serial("MayOS: AB journal recovered generation=");
    number(digits,generation);serial(digits);
    serial(" active=");
    number(digits,(u32)record[8]);serial(digits);
    serial(" trial=");
    number(digits,(u32)record[12]);serial(digits);
    serial(" (QEMU DIAGNOSTIC; NO INSTALL)\n");
}
int may_ab_journal_diagnostic(int command) {
    if(!ab_read_all())return 0;
    if(command==0) {
        u32 selected=ab_select(ab_current);
        if(!selected) {
            serial("MayOS: AB journal has no valid persistent record\n");
            return 0;
        }
        ab_log_generation(selected,ab_current);
        return 1;
    }
    if(command==1) { /* init on pristine, explicitly marked test disk */
        if(!ab_blank(ab_first) || !ab_blank(ab_second) ||
           !mayrs_ab_disk_initial(ab_new,AB_RECORD_BYTES) ||
           !ab_write_blank(AB_LBA_FIRST,ab_new))return 0;
        serial("MayOS: AB journal initialized and ATA FLUSH completed\n");
        return 1;
    }
    if(command==2) { /* synthetic stage: NOT an authenticated update */
        if(!ab_owned(ab_first) || !ab_blank(ab_second) ||
           ab_select(ab_current)!=1u ||
           !mayrs_ab_disk_synthetic_stage(ab_current,ab_new,AB_RECORD_BYTES) ||
           !ab_write_blank(AB_LBA_SECOND,ab_new))return 0;
        serial("MayOS: AB journal synthetic trial B committed and ATA FLUSH completed\n");
        return 1;
    }
    return 0;
}
#else
int may_ab_journal_diagnostic(int command) {
    (void)command;
    return 0;
}
#endif
