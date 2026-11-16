/* MayOS native Updates: intentionally isolated from the approved system bar,
 * common titlebar, Dock, Settings sidebar, and existing theme implementation.
 *
 * Three views instead of a single overcrowded screen. Each section uses the
 * real release state. In particular 0% is NEVER presented as installation
 * progress, and the native MayKernel has no fake GitHub 'online' indicator. */
static int updater_view;
#define UPDATE_NAV_HEIGHT 32
#define UPDATE_NAV_TOP 65
#define UPDATE_NAV_GAP 8
#define UPDATE_NAV_MARGIN 0
#define UPDATE_INFO_TOP 111

static int updater_tab_width(int cw) {
    return (cw - 2 * UPDATE_NAV_GAP) / 3;
}
static int updater_tab_at(int rx, int ry, int cw) {
    if (ry < 53 + UPDATE_NAV_TOP || ry >= 53 + UPDATE_NAV_TOP + UPDATE_NAV_HEIGHT)
        return -1;
    int w = updater_tab_width(cw);
    for (int i = 0; i < 3; ++i) {
        int left = 205 + i * (w + UPDATE_NAV_GAP);
        if (rx >= left && rx < left + w) return i;
    }
    return -1;
}
static void updater_nav(int cx, int cy, int cw) {
    static const char *labels[] = {"Overview", "Release notes", "Progress"};
    int tw = updater_tab_width(cw);
    for (int i = 0; i < 3; ++i) {
        int x = cx + i * (tw + UPDATE_NAV_GAP);
        ui_card(x, cy + UPDATE_NAV_TOP, tw, UPDATE_NAV_HEIGHT, 10,
                updater_view == i ? selected_fill : surface_high);
        text(x + 12, cy + UPDATE_NAV_TOP + 5, labels[i],
             updater_view == i ? ink : muted);
    }
}
static void updater_overview(int cx, int cy, int cw, const MayUpdaterState *u) {
    int top = cy + UPDATE_INFO_TOP;
    ui_card(cx, top, cw, 220, 20, surface_high);
    text(cx + 18, top + 16, "THIS DEVICE", muted);
    text(cx + 18, top + 37, "MayOS " MAYOS_RELEASE, ink);
    rect(cx + 18, top + 68, cw - 36, 1, line);
    text(cx + 18, top + 82,
         u->online_checked ? "LATEST / GITHUB VERIFIED" : "LATEST / SAVED RECORD",
         muted);
    if (u->has_latest) {
        /* The public release may be older than a development build. */
        titlebar_text(cx + 18, top + 105, u->latest.name, cw - 36, ink);
        text(cx + 18, top + 131, "Arch  x86_64", muted);
        text(cx + 18, top + 154,
             u->latest.variant[0] == 'd' ? "MayKernel64 + Linux64" : "MayKernel64", muted);
        text(cx + 18, top + 177, u->latest.date, muted);
        text(cx + cw - 123, top + 177, "ISO:", muted);
        numtext(cx + cw - 83, top + 177,
                (u->latest.bytes + 524288u) / 1048576u, ink);
        text(cx + cw - 48, top + 177, "MiB", muted);
        text(cx + 18, top + 198,
             u->has_upgrade ? "A newer release is listed" :
             "No newer release in saved records", muted);
    } else {
        text(cx + 18, top + 113, "No verified release information", muted);
    }
    text(cx + 2, top + 245, "STATUS", muted);
    text(cx + 2, top + 267,
         u->online_checked ? "Online catalog checked" : "Offline catalog only", ink);
    text(cx + 2, top + 295, "Install requires safe A/B slots; not yet available", muted);
}
static void updater_notes_view(int cx, int cy, int cw, const MayUpdaterState *u) {
    int top = cy + UPDATE_INFO_TOP;
    ui_card(cx, top, cw, 222, 20, surface_high);
    text(cx + 18, top + 17, "RELEASE NOTES", muted);
    if (!u->has_latest) {
        text(cx + 18, top + 53, "No release catalog available", muted);
    } else {
        titlebar_text(cx + 18, top + 44, u->latest.name, cw - 36, ink);
        text(cx + 18, top + 77, "Published", muted);
        text(cx + 123, top + 77, u->latest.date, ink);
        rect(cx + 18, top + 102, cw - 36, 1, line);
        /* Existing Release summary is source metadata, not invented changes.
         * Limit text to 2 lines instead of overflowing the Settings window. */
        titlebar_text(cx + 18, top + 120, u->latest.notes, cw - 40, ink);
        if (u->latest.notes[0]) {
            int n = (cw - 40) / 8;
            int len = (int)strlen(u->latest.notes);
            if (n < len && n > 0)
                titlebar_text(cx + 18, top + 143, u->latest.notes + n, cw - 40, muted);
        }
        text(cx + 18, top + 185, "Source: GitHub Release metadata", muted);
    }
    text(cx + 2, top + 247, "For full history, see the MayOS website changelog", muted);
    text(cx + 2, top + 276, "Preview and source-only releases are not installers", muted);
}
static void updater_progress_view(int cx, int cy, int cw, const MayUpdaterState *u) {
    int top = cy + UPDATE_INFO_TOP;
    ui_card(cx, top, cw, 232, 20, surface_high);
    text(cx + 18, top + 16, "DOWNLOAD", ink);
    text(cx + 18, top + 42,
         u->phase == MAY_UPDATE_DOWNLOADING ? "Receiving bytes" :
         u->phase == MAY_UPDATE_VERIFYING ? "Verifying downloaded data" :
         u->phase == MAY_UPDATE_VERIFIED ? "Digest verified" : "Not started",
         muted);
    if (u->total && u->phase >= MAY_UPDATE_DOWNLOADING &&
        u->phase <= MAY_UPDATE_VERIFIED) {
        numtext(cx + cw - 65, top + 42, may_updater_download_percent(), ink);
        text(cx + cw - 40, top + 42, "%", muted);
    }
    soft_roundrect(cx + 18, top + 69, cw - 36, 6, 3, line);
    if (u->total && u->phase >= MAY_UPDATE_DOWNLOADING &&
        u->phase <= MAY_UPDATE_VERIFIED) {
        int width = (int)(((unsigned long long)(cw - 36) *
                            u->transferred) / u->total);
        if (width > 0)
            soft_roundrect(cx + 18, top + 69, width, 6, 3, accent);
    }
    rect(cx + 18, top + 91, cw - 36, 1, line);
    text(cx + 18, top + 109, "SHA-256", ink);
    text(cx + 18, top + 133,
         u->verified ? "Verified" :
         u->phase == MAY_UPDATE_VERIFYING ? "Verifying actual data" :
         "Waiting for complete download", muted);
    rect(cx + 18, top + 158, cw - 36, 1, line);
    text(cx + 18, top + 175, "INSTALLATION", ink);
    text(cx + 18, top + 200, "Unavailable - boot slots not implemented", muted);

    /* Buttons have real, explicitly limited behavior. They cannot write a
     * disk until a verified transactional installer actually exists. */
    ui_card(cx + 12, cy + 333, 176, 30, 10, surface_high);
    text(cx + 24, cy + 339, "Check unavailable", muted);
    ui_card(cx + 198, cy + 333, 176, 30, 10, surface_high);
    text(cx + 210, cy + 339, "Install disabled", muted);
    if (u->phase == MAY_UPDATE_FAILED)
        titlebar_text(cx + 2, cy + 374, u->message, cw - 6, muted);
    else
        text(cx + 2, cy + 374, "No network writes, no boot changes", muted);
}
static void updater_draw(int cx, int cy, int cw) {
    const MayUpdaterState *u = may_updater_state();
    text(cx, cy, "MAYOS / SOFTWARE UPDATE", muted);
    heading_text(cx, cy + 23, "System Update", ink);
    updater_nav(cx, cy, cw);
    if (updater_view == 1)
        updater_notes_view(cx, cy, cw, u);
    else if (updater_view == 2)
        updater_progress_view(cx, cy, cw, u);
    else
        updater_overview(cx, cy, cw, u);
}
