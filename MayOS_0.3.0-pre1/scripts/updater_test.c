#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "updater.h"
#include "update_sha256.h"

static void vectors(void) {
    MayUpdateSha256 sha;
    char digest[65];
    may_update_sha256_init(&sha);
    may_update_sha256_hex(&sha, digest);
    assert(!strcmp(digest,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    may_update_sha256_init(&sha);
    may_update_sha256_write(&sha, "a", 1);
    may_update_sha256_write(&sha, "bc", 2);
    may_update_sha256_hex(&sha, digest);
    assert(!strcmp(digest,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    /* 1 MiB also exercises many 64-byte blocks and the 64-bit length field. */
    unsigned char block[4096];
    memset(block, 'A', sizeof(block));
    may_update_sha256_init(&sha);
    for (unsigned i=0;i<256;++i) may_update_sha256_write(&sha, block, sizeof(block));
    may_update_sha256_hex(&sha, digest);
    assert(!strcmp(digest,"4e29ad18ab9f42d7c233500771a39d7c852b200baf328fd00fbbe3fecea1eb56"));
}
static void catalog(void) {
    may_updater_init();
    const MayUpdaterState *s=may_updater_state();
    assert(s->phase == MAY_UPDATE_OFFLINE && s->has_latest);
    assert(!strcmp(s->latest.tag,"v0.5.0-pre1"));
    assert(s->latest.bytes==24047616u && !s->has_upgrade);
    assert(!s->online_checked && !s->verified);
    assert(!may_updater_stream_begin()); /* offline must never allow a download */
    assert(may_updater_compare("0.5.0-pre2-dev","0.5.0-pre1")>0);
    assert(may_updater_compare("0.5.0","0.5.0-pre99")>0);
    assert(may_updater_compare("0.5.0-pre2","0.5.0-pre2-dev")>0);
}
static void feed_and_download(void) {
    const char *bad=
        "v0.6.0-pre1|maykernel|MayOS 0.6|1048576|"
        "4e29ad18ab9f42d7c233500771a39d7c852b200baf328fd00fbbe3fecea1eb56|"
        "2026-11-17|x86_64|MayKernel64|https://attacker.example/evil.iso|Bad";
    assert(!may_updater_feed_line(bad));
    const char *fixture=
        "v0.6.0-pre1|maykernel|MayOS 0.6.0 preview|1048576|"
        "4e29ad18ab9f42d7c233500771a39d7c852b200baf328fd00fbbe3fecea1eb56|"
        "2026-11-17|x86_64|MayKernel64|"
        "https://github.com/ZhongYeMay/MayOS/releases/download/v0.6.0-pre1/MayOS-test.iso|Test only";
    assert(may_updater_feed_line(fixture));
    assert(!may_updater_stream_begin()); /* TLS check must precede download */
    may_updater_checking();
    may_updater_checked();
    assert(may_updater_state()->phase==MAY_UPDATE_AVAILABLE);
    assert(may_updater_stream_begin());
    unsigned char block[4096]; memset(block,'A',sizeof(block));
    for(unsigned i=0;i<256;++i) {
        assert(may_updater_stream_chunk(block,sizeof(block),(i+1)/13));
        if(i==127) {
            assert(may_updater_download_percent()==50);
            assert(may_updater_download_eta()>0);
        }
    }
    assert(may_updater_download_percent()==100);
    assert(may_updater_stream_finish());
    assert(may_updater_state()->verified && may_updater_state()->phase==MAY_UPDATE_VERIFIED);
    assert(!may_updater_stream_begin()); /* no re-entry into verified stream */
    /* Corrupted payload: correct byte count but incorrect real digest. */
    may_updater_checking(); may_updater_checked(); assert(may_updater_stream_begin());
    block[0]='B';
    assert(may_updater_stream_chunk(block,sizeof(block),1));
    block[0]='A';
    for(unsigned i=1;i<256;++i) assert(may_updater_stream_chunk(block,sizeof(block),i/13));
    assert(!may_updater_stream_finish());
    assert(may_updater_state()->phase==MAY_UPDATE_FAILED && !may_updater_state()->verified);
    /* Truncated file: cannot ever show verified. */
    may_updater_checking(); may_updater_checked(); assert(may_updater_stream_begin());
    assert(may_updater_stream_chunk(block,64,1));
    assert(!may_updater_stream_finish());
    assert(may_updater_state()->phase==MAY_UPDATE_FAILED);
    /* Oversized write: fail before consuming attacker-controlled data. */
    may_updater_checking(); may_updater_checked(); assert(may_updater_stream_begin());
    for(unsigned i=0;i<256;++i) assert(may_updater_stream_chunk(block,sizeof(block),i/13));
    assert(!may_updater_stream_chunk(block,1,20));
    assert(may_updater_state()->phase==MAY_UPDATE_FAILED && !may_updater_state()->verified);
    may_updater_init();
    assert(!may_updater_state()->has_upgrade);
}
int main(void) {
    vectors(); catalog(); feed_and_download();
    puts("PASS: native SHA-256 vectors, real byte progress, invalid-domain, truncation, corruption and oversize rejection");
    return 0;
}
