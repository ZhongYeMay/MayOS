#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "updater.h"

static void snapshot_tests(void) {
    may_updater_init();
    const MayUpdaterState *state = may_updater_state();
    assert(state->phase == MAY_UPDATE_OFFLINE);
    assert(state->has_latest);
    assert(!strcmp(state->latest.tag, "v0.5.0-pre1"));
    assert(!strcmp(state->latest.arch, "x86_64"));
    assert(state->latest.bytes == 24047616u);
    assert(!state->has_upgrade); /* running 0.5.0-pre2-dev is newer */
    assert(!state->online_checked);
}
static void version_tests(void) {
    assert(may_updater_compare("0.5.0-pre2-dev", "v0.5.0-pre1") > 0);
    assert(may_updater_compare("0.5.0", "0.5.0-pre99") > 0);
    assert(may_updater_compare("0.5.0-pre2", "0.5.0-pre2-dev") > 0);
    assert(may_updater_compare("v0.6.0-pre1", "0.5.9") > 0);
    assert(may_updater_compare("v0.5.0-pre1", "0.5.0-pre1") == 0);
    assert(may_updater_compare("v0.4.1", "0.5.0-pre1") < 0);
}
static void feed_tests(void) {
    const char *bad_host =
        "v0.6.0-pre1|maykernel|MayOS 0.6|4194304|"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa|"
        "2026-11-17|x86_64|MayKernel64|https://attacker.example/evil.iso|No";
    assert(!may_updater_feed_line(bad_host));
    assert(!may_updater_state()->has_upgrade);
    const char *good =
        "v0.6.0-pre1|maykernel|MayOS 0.6.0 preview|4194304|"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa|"
        "2026-11-17|x86_64|MayKernel64|"
        "https://github.com/ZhongYeMay/MayOS/releases/download/v0.6.0-pre1/MayOS-test.iso|Verified test fixture";
    assert(may_updater_feed_line(good));
    assert(may_updater_state()->has_upgrade);
    assert(!strcmp(may_updater_state()->latest.tag, "v0.6.0-pre1"));
    may_updater_checking();
    may_updater_checked();
    assert(may_updater_state()->online_checked);
    assert(may_updater_state()->phase == MAY_UPDATE_AVAILABLE);
    may_updater_progress(2097152ull, 4194304ull, 10u);
    assert(may_updater_download_percent() == 50u);
    assert(may_updater_download_eta() == 10u);
    may_updater_progress(4194304ull, 4194304ull, 20u);
    may_updater_verifying();
    may_updater_verified();
    assert(may_updater_state()->verified);
    assert(may_updater_state()->phase == MAY_UPDATE_VERIFIED);
    may_updater_failed("Simulated digest mismatch");
    assert(!may_updater_state()->verified);
    assert(may_updater_state()->phase == MAY_UPDATE_FAILED);
    may_updater_init();
    assert(!may_updater_state()->has_upgrade);
}
int main(void) {
    snapshot_tests();
    version_tests();
    feed_tests();
    puts("PASS: MayOS update feed whitelist, version ordering, actual-byte progress and no automatic install");
    return 0;
}
