/* Settings > Updates: native framebuffer UI, deliberately scoped.
 * Does NOT modify the approved MayOS system bar, Dock, About card or
 * window chrome. A progress bar is filled ONLY by measured byte counts. */
static void updater_draw(int cx, int cy, int cw) {
    const MayUpdaterState *u = may_updater_state();
    text(cx, cy, "MAYOS / SOFTWARE UPDATE", muted);
    heading_text(cx, cy + 24, "系统更新", ink);
    text(cx, cy + 51, "Installed: MayOS " MAYOS_RELEASE, muted);

    ui_card(cx, cy + 71, cw, 178, 20, surface_high);
    text(cx + 16, cy + 83,
         u->online_checked ? "GITHUB RELEASE / ONLINE" : "GITHUB RELEASE / SAVED",
         muted);
    if (u->has_latest) {
        heading_text(cx + 16, cy + 106, u->latest.name, ink);
        text(cx + 16, cy + 136, u->latest.arch, muted);
        if (u->has_upgrade)
            text(cx + 122, cy + 136, "New release available", accent);
        else
            text(cx + 122, cy + 136, "No newer release in saved data", muted);
        text(cx + 16, cy + 159,
             u->latest.variant[0] == 'd' ? "MayKernel64 + Linux64" : "MayKernel64", ink);
        text(cx + 16, cy + 183, u->latest.date, muted);
        text(cx + 121, cy + 183, "ISO size:", muted);
        numtext(cx + 211, cy + 183,
                (u->latest.bytes + 524288u) / 1048576u, ink);
        text(cx + 242, cy + 183, "MiB", muted);
        text(cx + 16, cy + 215, "Estimated install: unavailable", muted);
    } else {
        text(cx + 16, cy + 120, "No verified release catalog available", muted);
    }

    if (u->has_latest)
        titlebar_text(cx, cy + 258, u->latest.notes, cw - 7, muted);
    text(cx, cy + 284, "Download / actual bytes", ink);
    if (u->total && u->phase >= MAY_UPDATE_DOWNLOADING &&
        u->phase <= MAY_UPDATE_VERIFIED) {
        numtext(cx + cw - 67, cy + 284, may_updater_download_percent(), ink);
        text(cx + cw - 42, cy + 284, "%", muted);
    }
    rect(cx, cy + 304, cw, 6, line);
    if (u->phase == MAY_UPDATE_DOWNLOADING || u->phase == MAY_UPDATE_VERIFYING ||
        u->phase == MAY_UPDATE_VERIFIED)
        rect(cx, cy + 304, (cw * (int)may_updater_download_percent()) / 100, 6, accent);
    text(cx, cy + 318, "Installation (not available)", muted);
    rect(cx, cy + 338, cw, 5, line);
    /* No installer exists: never draw a fake positive install percentage. */
    button(cx, cy + 345, 142, "检查不可用", 0);
    button(cx + 162, cy + 345, 170, "安装不可用", 0);
    if (u->phase == MAY_UPDATE_OFFLINE)
        text(cx, cy + 381, "本地发布记录 · 未与 GitHub 实时连接", muted);
    else
        titlebar_text(cx, cy + 381, u->message, cw - 10, muted);
    /* Real transfer ETA is maintained by may_updater_download_eta().
     * Keep the fixed-height Settings pane within its original bounds. */
}
