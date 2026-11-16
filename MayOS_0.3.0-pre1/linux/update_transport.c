/* Native Linux-only HTTPS transport for MayOS update checks and safe ISO
 * staging. Shared GUI/MayKernel release policy is not bypassed.
 * No boot partition, mount point, installer or reboot is ever modified. */
#define _GNU_SOURCE
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "update_sha256.h"

#define FEED_URL "https://raw.githubusercontent.com/ZhongYeMay/MayOS/main/website/update-feed-v1.txt"
#define RELEASE_BASE "https://github.com/ZhongYeMay/MayOS/releases/download/"
#define MAX_FEED 32768u
#define MAX_ROW 650u
#define MAX_ISO (512ull * 1024ull * 1024ull)
typedef struct {
    char bytes[MAX_FEED+1u];
    size_t used;
    int overflow;
} Feed;
typedef struct {
    char tag[32], variant[16], sha256[65], url[256];
    unsigned long long bytes;
} Asset;
typedef struct {
    int fd, error;
    unsigned long long transferred, total, last_reported;
    MayUpdateSha256 sha;
    time_t begin;
} Sink;

static size_t feed_write(void *data,size_t size,size_t count,void *opaque){
    Feed *feed=opaque;
    if(size && count>SIZE_MAX/size){feed->overflow=1;return 0;}
    size_t n=size*count;
    if(n>MAX_FEED-feed->used){feed->overflow=1;return 0;}
    memcpy(feed->bytes+feed->used,data,n);
    feed->used+=n; feed->bytes[feed->used]=0;
    return n;
}
static CURL *https(const char *url,long seconds){
    const char *ca=getenv("MAYOS_CA_BUNDLE");
    if(!ca || !*ca) ca="/etc/ssl/certs/ca-certificates.crt";
    CURL *c=curl_easy_init();
    if(!c)return NULL;
#define SET(k,v) do {if(curl_easy_setopt(c,k,v)!=CURLE_OK)goto fail;}while(0)
    SET(CURLOPT_URL,url);
    SET(CURLOPT_PROTOCOLS_STR,"https");
    SET(CURLOPT_REDIR_PROTOCOLS_STR,"https");
    SET(CURLOPT_FOLLOWLOCATION,1L);
    SET(CURLOPT_MAXREDIRS,6L);
    SET(CURLOPT_CONNECTTIMEOUT,12L);
    SET(CURLOPT_TIMEOUT,seconds);
    SET(CURLOPT_NOSIGNAL,1L);
    SET(CURLOPT_SSL_VERIFYPEER,1L);
    SET(CURLOPT_SSL_VERIFYHOST,2L);
    SET(CURLOPT_SSLVERSION,(long)CURL_SSLVERSION_TLSv1_2);
    SET(CURLOPT_CAINFO,ca);
    SET(CURLOPT_FAILONERROR,1L);
    SET(CURLOPT_USERAGENT,"MayOS-Update/0.6-development");
#undef SET
    return c;
fail:
    curl_easy_cleanup(c);return NULL;
}
static int catalog_fetch(Feed *feed){
    memset(feed,0,sizeof(*feed));
    CURL *c=https(FEED_URL,35);
    if(!c)return 0;
    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,feed_write);
    curl_easy_setopt(c,CURLOPT_WRITEDATA,feed);
    CURLcode result=curl_easy_perform(c);
    long status=0;
    curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&status);
    curl_easy_cleanup(c);
    static const char banner[]="MAYOS_UPDATE_FEED_V1\n";
    return result==CURLE_OK && status==200 && !feed->overflow &&
           feed->used>=sizeof(banner) &&
           !memcmp(feed->bytes,banner,sizeof(banner)-1);
}
static int tag_valid(const char *s){
    if(*s++!='v' || !*s)return 0;
    for(;*s;++s)
        if(!((*s>='0'&&*s<='9') || (*s>='a'&&*s<='z') ||
             *s=='.' || *s=='-'))return 0;
    return 1;
}
static int sha_valid(const char *s){
    if(strlen(s)!=64u)return 0;
    for(int i=0;i<64;++i)
        if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return 0;
    return 1;
}
static int parse_asset(const char *row,Asset *out){
    char buf[MAX_ROW];
    size_t len=strlen(row);
    if(len==0||len>=sizeof(buf)||row[0]!='v')return 0;
    memcpy(buf,row,len+1);
    char *fields[10]={0};int n=1;
    fields[0]=buf;
    for(char *p=buf;*p;p++){
        unsigned char ch=(unsigned char)*p;
        if(ch<32 || ch>=127)return 0;
        if(*p=='|'){
            if(n>=10)return 0;
            *p=0; fields[n++]=p+1;
        }
    }
    if(n!=10 || !tag_valid(fields[0]) || strcmp(fields[1],"dual") ||
       strcmp(fields[6],"x86_64") || !sha_valid(fields[4]))
        return 0;
    errno=0;char *end=NULL;
    unsigned long long bytes=strtoull(fields[3],&end,10);
    if(errno || !end || *end || bytes<1048576ull || bytes>MAX_ISO)return 0;
    const char *url=fields[8],*base=RELEASE_BASE;
    size_t base_len=strlen(base),tag_len=strlen(fields[0]);
    if(strncmp(url,base,base_len) ||
       strncmp(url+base_len,fields[0],tag_len) ||
       url[base_len+tag_len]!='/' ||
       strncmp(url+base_len+tag_len+1,"MayOS-",6))
        return 0;
    size_t url_len=strlen(url);
    if(url_len<base_len+tag_len+12 || url_len>=sizeof(out->url) ||
       strcmp(url+url_len-4,".iso"))return 0;
    for(const unsigned char *c=(const unsigned char*)url;*c;++c)
        if(*c<=32 || *c>=127 || *c=='?' || *c=='#' || *c=='@' ||
           *c=='\\')return 0;
    if(strlen(fields[0])>=sizeof(out->tag))return 0;
    snprintf(out->tag,sizeof(out->tag),"%s",fields[0]);
    snprintf(out->variant,sizeof(out->variant),"%s",fields[1]);
    snprintf(out->sha256,sizeof(out->sha256),"%s",fields[4]);
    snprintf(out->url,sizeof(out->url),"%s",url);
    out->bytes=bytes;
    return 1;
}
static int parse_catalog(Feed *feed,const Asset *requested,Asset *matched){
    char *p=strchr(feed->bytes,'\n');
    if(!p)return 0;
    p++;
    int count=0;
    while(*p){
        char *end=strchr(p,'\n');
        if(!end)return 0; /* fail closed on truncated feed */
        *end=0;
        if(*p!='#' && *p){
            Asset asset={0};
            if(parse_asset(p,&asset)){
                if(!requested){
                    printf("U|%s\n",p);
                    count++;
                }else if(!strcmp(asset.tag,requested->tag) &&
                         !strcmp(asset.sha256,requested->sha256) &&
                         !strcmp(asset.variant,requested->variant) &&
                         asset.bytes==requested->bytes){
                    if(matched)*matched=asset;
                    count++;
                }
            }
        }
        p=end+1;
    }
    return count>0;
}
static size_t iso_write(void *bytes,size_t size,size_t count,void *opaque){
    Sink *sink=opaque;
    if(size && count>SIZE_MAX/size)return 0;
    size_t n=size*count;
    if(n>UINT32_MAX || n>sink->total-sink->transferred)return 0;
    size_t off=0;
    while(off<n){
        ssize_t w=write(sink->fd,(const char*)bytes+off,n-off);
        if(w<0 && errno==EINTR)continue;
        if(w<=0){sink->error=1;return 0;}
        off+=(size_t)w;
    }
    may_update_sha256_write(&sink->sha,bytes,(unsigned int)n);
    sink->transferred+=n;
    if(sink->transferred-sink->last_reported>=262144ull ||
       sink->transferred==sink->total){
        time_t elapsed=time(NULL)-sink->begin;
        if(elapsed<1)elapsed=1;
        printf("P|%llu|%llu|%lu\n",sink->transferred,sink->total,
               (unsigned long)elapsed);
        fflush(stdout);
        sink->last_reported=sink->transferred;
    }
    return n;
}
static int stage_iso(const Asset *asset){
    char directory[]="/tmp/mayos-update.XXXXXX";
    if(!mkdtemp(directory)){puts("E|Staging directory unavailable");return 1;}
    char partial[160],verified[160];
    snprintf(partial,sizeof(partial),"%s/pending.iso.part",directory);
    snprintf(verified,sizeof(verified),"%s/verified.iso",directory);
    int fd=open(partial,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0){rmdir(directory);puts("E|Staging file unavailable");return 1;}
    Sink sink={.fd=fd,.total=asset->bytes,.begin=time(NULL)};
    may_update_sha256_init(&sink.sha);
    CURL *c=https(asset->url,300);
    if(!c){
        close(fd);unlink(partial);rmdir(directory);
        puts("E|Verified HTTPS unavailable");return 1;
    }
    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,iso_write);
    curl_easy_setopt(c,CURLOPT_WRITEDATA,&sink);
    curl_easy_setopt(c,CURLOPT_MAXFILESIZE_LARGE,(curl_off_t)asset->bytes);
    CURLcode result=curl_easy_perform(c);
    long status=0;
    curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&status);
    curl_easy_cleanup(c);
    puts("V|SHA-256");
    fflush(stdout);
    char actual[65];
    may_update_sha256_hex(&sink.sha,actual);
    int synced=fsync(fd)==0;
    int closed=close(fd)==0;
    if(result!=CURLE_OK || status!=200 || sink.error ||
       sink.transferred!=sink.total || !synced || !closed ||
       strcmp(actual,asset->sha256)){
        unlink(partial);rmdir(directory);
        puts("E|Incomplete or mismatched SHA-256; partial file destroyed");
        return 1;
    }
    if(rename(partial,verified)){
        unlink(partial);rmdir(directory);
        puts("E|Could not finalize staged ISO");
        return 1;
    }
    /* The application may inspect this ephemeral file; do not install it.
     * Existing ISO assets are not A/B installer payloads. */
    puts("D|verified");
    fflush(stdout);
    return 0;
}
int mayos_update_command(int argc,char **argv){
    int check=argc==2 && !strcmp(argv[1],"--mayos-update-check");
    int download=argc==6 && !strcmp(argv[1],"--mayos-update-download");
    if(!check && !download){puts("E|Unsupported system update request");return 2;}
    Asset requested={0};
    if(download){
        if(!tag_valid(argv[2]) || strcmp(argv[3],"dual") ||
           !sha_valid(argv[4]) || strlen(argv[2])>=sizeof(requested.tag)){
            puts("E|Unsafe release arguments");return 2;
        }
        errno=0;char *end=NULL;
        unsigned long long bytes=strtoull(argv[5],&end,10);
        if(errno || !end || *end || bytes<1048576ull || bytes>MAX_ISO){
            puts("E|Invalid release file size");return 2;
        }
        snprintf(requested.tag,sizeof(requested.tag),"%s",argv[2]);
        snprintf(requested.variant,sizeof(requested.variant),"%s",argv[3]);
        snprintf(requested.sha256,sizeof(requested.sha256),"%s",argv[4]);
        requested.bytes=bytes;
    }
    if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK){
        puts("E|libcurl initialization failed");return 1;
    }
    Feed *feed=calloc(1,sizeof(Feed));
    if(!feed){puts("E|Out of memory");curl_global_cleanup();return 1;}
    if(!catalog_fetch(feed)){
        free(feed);curl_global_cleanup();
        puts("E|GitHub catalog HTTPS failure or invalid signature");return 1;
    }
    Asset matched={0};
    int valid=parse_catalog(feed,download?&requested:NULL,&matched);
    free(feed);
    if(!valid){
        curl_global_cleanup();
        puts("E|No verified compatible Release in catalog");return 1;
    }
    int result=0;
    if(check){puts("C|ok");fflush(stdout);}
    else result=stage_iso(&matched);
    curl_global_cleanup();
    return result;
}
