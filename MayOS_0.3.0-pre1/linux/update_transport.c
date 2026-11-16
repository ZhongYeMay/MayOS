/* MayOS Linux-only update catalog transport.
 * TLS-verified, bounded download of the GitHub-hosted release catalog.
 * This module never downloads an ISO, never installs, and never writes disks.
 * Deliberately isolated from the freestanding MayKernel networking path. */
#define _GNU_SOURCE
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define MAY_UPDATE_FEED_URL "https://raw.githubusercontent.com/ZhongYeMay/MayOS/main/website/update-feed-v1.txt"
#define MAY_UPDATE_MAX_FEED 32768u
#define MAY_UPDATE_MAX_ROW 650u
typedef struct {
    char bytes[MAY_UPDATE_MAX_FEED + 1u];
    size_t len;
    int overflow;
} UpdateFeed;
static size_t received(void *ptr, size_t size, size_t count, void *ctx) {
    UpdateFeed *feed=ctx;
    if (size && count > SIZE_MAX / size) {feed->overflow=1;return 0;}
    size_t n=size*count;
    if (n > MAY_UPDATE_MAX_FEED-feed->len) {feed->overflow=1;return 0;}
    memcpy(feed->bytes+feed->len,ptr,n);
    feed->len+=n;
    feed->bytes[feed->len]=0;
    return n;
}
static int plausible_release_line(const char *s, size_t len) {
    if (len==0 || len>=MAY_UPDATE_MAX_ROW || s[0]!='v') return 0;
    unsigned int pipes=0;
    for(size_t i=0;i<len;i++) {
        unsigned char c=(unsigned char)s[i];
        if(c=='|') pipes++;
        if(c<32u || c>=127u) return 0;
    }
    if(pipes!=9) return 0;
    const char *sep=strchr(s,'|');
    if(!sep || strncmp(sep,"|dual|",6)) return 0;
    if(!strstr(s,"|x86_64|")) return 0;
    if(!strstr(s,"https://github.com/ZhongYeMay/MayOS/releases/download/"))
        return 0;
    return 1; /* The MayUpdater parser revalidates every field. */
}
static int request_catalog(void) {
    const char *ca=getenv("MAYOS_CA_BUNDLE");
    if(!ca || !*ca) ca="/etc/ssl/certs/ca-certificates.crt";
    if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK) {
        puts("E|TLS transport initialization failed");
        return 1;
    }
    CURL *handle=curl_easy_init();
    if(!handle) { puts("E|Cannot allocate HTTPS request");return 1; }
    UpdateFeed *feed=calloc(1,sizeof(UpdateFeed));
    if(!feed){curl_easy_cleanup(handle);puts("E|Cannot allocate update catalog");return 1;}
    char error[CURL_ERROR_SIZE]={0};
#define SET(opt,val) do {if(curl_easy_setopt(handle,opt,val)!=CURLE_OK)goto fail;} while(0)
    SET(CURLOPT_URL,MAY_UPDATE_FEED_URL);
    SET(CURLOPT_PROTOCOLS_STR,"https");
    SET(CURLOPT_REDIR_PROTOCOLS_STR,"https");
    SET(CURLOPT_FOLLOWLOCATION,1L);
    SET(CURLOPT_MAXREDIRS,4L);
    SET(CURLOPT_CONNECTTIMEOUT,12L);
    SET(CURLOPT_TIMEOUT,35L);
    SET(CURLOPT_NOSIGNAL,1L);
    SET(CURLOPT_CAINFO,ca);
    SET(CURLOPT_SSL_VERIFYPEER,1L);
    SET(CURLOPT_SSL_VERIFYHOST,2L);
    SET(CURLOPT_SSLVERSION,(long)CURL_SSLVERSION_TLSv1_2);
    SET(CURLOPT_USERAGENT,"MayOS-SystemUpdate/0.6-development");
    SET(CURLOPT_FAILONERROR,1L);
    SET(CURLOPT_WRITEFUNCTION,received);
    SET(CURLOPT_WRITEDATA,feed);
    SET(CURLOPT_ERRORBUFFER,error);
#undef SET
    CURLcode result=curl_easy_perform(handle);
    long status=0;
    curl_easy_getinfo(handle,CURLINFO_RESPONSE_CODE,&status);
    curl_easy_cleanup(handle);
    if(result!=CURLE_OK || status!=200 || feed->overflow) {
        puts("E|GitHub HTTPS catalog request failed");
        free(feed);curl_global_cleanup();return 1;
    }
    const char *header="MAYOS_UPDATE_FEED_V1\n";
    size_t header_len=strlen(header);
    if(feed->len<=header_len || memcmp(feed->bytes,header,header_len)) {
        puts("E|GitHub catalog signature is invalid");
        free(feed);curl_global_cleanup();return 1;
    }
    int listed=0;
    char *at=feed->bytes+header_len;
    while(*at){
        char *newline=strchr(at,'\n');
        if(!newline) {
            puts("E|Truncated GitHub release feed");
            free(feed);curl_global_cleanup();return 1;
        }
        *newline=0;
        size_t len=(size_t)(newline-at);
        if(len && at[0]!='#' && plausible_release_line(at,len)) {
            printf("U|%s\n",at);
            listed++;
        }
        at=newline+1;
    }
    if(!listed) {
        puts("E|No compatible verified releases in GitHub feed");
        free(feed);curl_global_cleanup();return 1;
    }
    puts("C|ok");
    fflush(stdout);
    free(feed);curl_global_cleanup();return 0;
fail:
    curl_easy_cleanup(handle);
    free(feed);
    curl_global_cleanup();
    puts("E|HTTPS security option unavailable");
    return 1;
}
int mayos_update_command(int argc, char **argv) {
    if(argc==2 && !strcmp(argv[1],"--mayos-update-check")) return request_catalog();
    puts("E|Unsupported system update request");
    return 2;
}
