#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "updater.h"

int main(void) {
    may_updater_init();
    const MayUpdaterState *u=may_updater_state();
    assert(u->has_latest);
    assert(!strcmp(u->latest.variant,"dual"));
    assert(u->latest.bytes==42061824u);
    assert(!u->has_upgrade);
    assert(!may_updater_linux_begin()); /* no false offline download */
    const char *fixture=
        "v0.6.0-pre1|dual|MayOS 0.6.0 pre1|1048576|"
        "4e29ad18ab9f42d7c233500771a39d7c852b200baf328fd00fbbe3fecea1eb56|"
        "2026-11-17|x86_64|MayKernel x86_64 + Linux x86_64|"
        "https://github.com/ZhongYeMay/MayOS/releases/download/v0.6.0-pre1/MayOS-test.iso|Test fixture";
    assert(may_updater_feed_line(fixture));
    assert(u->has_upgrade);
    assert(!may_updater_linux_begin()); /* authenticated online check required */
    may_updater_checking();
    may_updater_checked();
    assert(u->online_checked);
    assert(may_updater_linux_begin());
    assert(u->phase==MAY_UPDATE_DOWNLOADING && !u->verified);
    assert(may_updater_linux_progress(524288ull,1048576ull,10));
    assert(may_updater_download_percent()==50u);
    assert(may_updater_download_eta()==10u);
    assert(!may_updater_linux_progress(524287ull,1048576ull,10)); /* no backwards progress */
    assert(u->phase==MAY_UPDATE_FAILED);
    may_updater_checking();may_updater_checked();
    assert(may_updater_linux_begin());
    assert(!may_updater_linux_progress(2048ull,1048577ull,10)); /* wrong total rejected */
    assert(u->phase==MAY_UPDATE_FAILED);
    may_updater_checking();may_updater_checked();
    assert(may_updater_linux_begin());
    assert(may_updater_linux_progress(1048576ull,1048576ull,15));
    may_updater_linux_verifying();
    assert(u->phase==MAY_UPDATE_VERIFYING);
    assert(may_updater_linux_verified());
    assert(u->phase==MAY_UPDATE_VERIFIED&&u->verified);
    assert(!may_updater_linux_begin()); /* cannot silently restart a finished transfer */
    may_updater_failed("Simulated local worker crash");
    assert(u->phase==MAY_UPDATE_FAILED && !u->verified);
    puts("PASS: Linux updater state only advances from authenticated catalog and real monotonic bytes");
    return 0;
}
