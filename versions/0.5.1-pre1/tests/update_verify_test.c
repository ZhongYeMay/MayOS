#include "update.h"
#include "sha256.h"
#include "version.h"
/* Host test: compare actual MayFS bytes, reject corruption, wrong size or
 * absent files. The test intentionally does not pretend SHA is a signature. */
File files[FS_MAX];
void *memset(void *dst,int v,u32 n) {
    u8 *d=dst;for(u32 i=0;i<n;i++)d[i]=(u8)v;return dst;
}
void *memcpy(void *dst,const void *src,u32 n) {
    u8 *d=dst;const u8 *s=src;
    for(u32 i=0;i<n;i++)d[i]=s[i];
    return dst;
}
u32 strlen(const char *s) {
    u32 n=0;while(s[n])n++;return n;
}
int strcmp(const char *a,const char *b) {
    while(*a&&*a==*b){a++;b++;}
    return (u8)*a-(u8)*b;
}
int fs_find(int parent,const char *name) {
    if(parent!=1)return -1;
    if(!strcmp(name,"Update.may")&&files[4].used)return 4;
    if(!strcmp(name,"Update.bin")&&files[5].used)return 5;
    return -1;
}
static const char manifest[]=
"name=MayOS\n"
"version=0.5.1-pre2\n"
"architecture=x86_64\n"
"kernel=MayKernel\n"
"release_date=2026-11-18\n"
"size_bytes=3\n"
"sha256=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n"
"eta_minutes=1\n"
"changelog=Test payload only\n";
int main(void) {
    memset(files,0,sizeof(files));
    files[4].used=1;files[4].parent=1;
    files[4].size=strlen(manifest);
    memcpy(files[4].data,manifest,files[4].size);
    files[5].used=1;files[5].parent=1;
    files[5].size=3;
    files[5].data[0]='a';files[5].data[1]='b';files[5].data[2]='c';
    if(may_update_verify_local())return 1;/* unverified manifest */
    may_update_check();
    if(may_update_info()->state!=MAY_UPDATE_CANDIDATE)return 2;
    if(!may_update_verify_local())return 3;
    files[5].data[2]='x';
    if(may_update_verify_local())return 4;
    if(!strcmp(may_update_verify_status(),
       "Verify: SHA-256 matches UNTRUSTED manifest; NOT signed"))return 5;
    files[5].data[2]='c';files[5].size=2;
    if(may_update_verify_local())return 6;
    files[5].size=3;files[5].used=0;
    if(may_update_verify_local())return 7;
    files[5].used=1;
    may_update_check();/* checking resets verification */
    if(!strcmp(may_update_verify_status(),
       "Verify: SHA-256 matches UNTRUSTED manifest; NOT signed"))return 8;
    return 0;
}
