/* Linux DRM/KMS scanout for the desktop's single connected display.
 * Included by desktop.c. The firmware framebuffer remains mapped as a fallback. */
#include <drm/drm.h>

enum { KMS_CARD_LIMIT = 16, KMS_CONNECTOR_LIMIT = 32, KMS_MODE_LIMIT = 128 };
static struct {
    int fd, count, selected, dirtyfb;
    uint32_t connector;
    struct drm_mode_crtc original;
    struct drm_mode_modeinfo modes[KMS_MODE_LIMIT];
} kms = {.fd = -1, .selected = -1};

typedef struct {
    uint32_t handle, fb;
    size_t size;
    MaySurface surface;
} KmsBuffer;

static KmsBuffer scanout, trial_scanout;
static int kms_trial = -1;

static int kms_ioctl(int fd, unsigned long request, void *argument) {
    int result;
    do result = ioctl(fd, request, argument);
    while (result < 0 && errno == EINTR);
    return result;
}

static int kms_modes_match(const struct drm_mode_modeinfo *a,
                           const struct drm_mode_modeinfo *b) {
    return a->clock == b->clock && a->hdisplay == b->hdisplay &&
           a->hsync_start == b->hsync_start && a->hsync_end == b->hsync_end &&
           a->htotal == b->htotal && a->hskew == b->hskew &&
           a->vdisplay == b->vdisplay && a->vsync_start == b->vsync_start &&
           a->vsync_end == b->vsync_end && a->vtotal == b->vtotal &&
           a->vscan == b->vscan && a->flags == b->flags;
}

static int kms_probe_card(int fd) {
    uint32_t connectors[KMS_CONNECTOR_LIMIT];
    struct drm_mode_card_res resources = {0};
    if (kms_ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &resources) < 0 ||
        !resources.count_connectors || resources.count_connectors > KMS_CONNECTOR_LIMIT) return 0;
    resources.connector_id_ptr = (uintptr_t)connectors;
    resources.count_crtcs = resources.count_encoders = resources.count_fbs = 0;
    if (kms_ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &resources) < 0 ||
        resources.count_connectors > KMS_CONNECTOR_LIMIT) return 0;

    unsigned active = 0;
    struct drm_mode_get_connector primary = {0};
    struct drm_mode_crtc current = {0};
    for (uint32_t i = 0; i < resources.count_connectors; i++) {
        struct drm_mode_get_connector connector = {.connector_id = connectors[i]};
        if (kms_ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &connector) < 0) return 0;
        if (connector.connection != 1 || !connector.encoder_id) continue;
        struct drm_mode_get_encoder encoder = {.encoder_id = connector.encoder_id};
        if (kms_ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &encoder) < 0) return 0;
        if (!encoder.crtc_id) continue;
        struct drm_mode_crtc crtc = {.crtc_id = encoder.crtc_id};
        if (kms_ioctl(fd, DRM_IOCTL_MODE_GETCRTC, &crtc) < 0) return 0;
        if (!crtc.mode_valid || !crtc.fb_id) continue;
        if (++active > 1) return 0;
        primary = connector;
        current = crtc;
    }
    if (active != 1 || !primary.count_modes || primary.count_modes > KMS_MODE_LIMIT ||
        current.mode.hdisplay != display.width || current.mode.vdisplay != display.height) return 0;
    struct drm_mode_modeinfo modes[KMS_MODE_LIMIT];
    primary.modes_ptr = (uintptr_t)modes;
    primary.count_props = primary.count_encoders = 0;
    if (kms_ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &primary) < 0 ||
        primary.count_modes > KMS_MODE_LIMIT) return 0;
    int count = 0, selected = -1;
    for (uint32_t i = 0; i < primary.count_modes; i++) {
        struct drm_mode_modeinfo *mode = &modes[i];
        if (mode->hdisplay < 640 || mode->vdisplay < 480 ||
            mode->hdisplay > 4096 || mode->vdisplay > 4096 ||
            (mode->flags & (DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLSCAN))) continue;
        if (kms_modes_match(mode, &current.mode)) selected = count;
        kms.modes[count++] = *mode;
    }
    if (!count || kms_ioctl(fd, DRM_IOCTL_SET_MASTER, NULL) < 0) return 0;
    kms.fd = fd;
    kms.connector = primary.connector_id;
    kms.original = current;
    kms.count = count;
    kms.selected = selected;
    kms.dirtyfb = 1;
    return 1;
}

static void kms_probe(void) {
    for (int i = 0; i < KMS_CARD_LIMIT; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/dri/card%d", i);
        int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;
        if (kms_probe_card(fd)) {
            serial("MayOS display: DRM/KMS active display detected\n");
            return;
        }
        close(fd);
    }
}

static void kms_release(KmsBuffer *buffer) {
    if (buffer->surface.data) munmap(buffer->surface.data, buffer->size);
    if (buffer->fb) kms_ioctl(kms.fd, DRM_IOCTL_MODE_RMFB, &buffer->fb);
    if (buffer->handle) {
        struct drm_mode_destroy_dumb destroy = {.handle = buffer->handle};
        kms_ioctl(kms.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
    }
    *buffer = (KmsBuffer){0};
}

static int kms_apply(struct drm_mode_crtc *request) {
    request->set_connectors_ptr = (uintptr_t)&kms.connector;
    request->count_connectors = 1;
    return kms_ioctl(kms.fd, DRM_IOCTL_MODE_SETCRTC, request) == 0;
}

static int kms_set_output(uint32_t fb, const struct drm_mode_modeinfo *mode) {
    struct drm_mode_crtc request = {.crtc_id = kms.original.crtc_id,
                                   .fb_id = fb, .mode_valid = 1, .mode = *mode};
    return kms_apply(&request);
}

static int kms_start_trial(int direction) {
    if (kms_trial >= 0 || kms.count < 2 || (direction != -1 && direction != 1)) return 0;
    int index = kms.selected < 0 ? 0 : (kms.selected + kms.count + direction) % kms.count;
    const struct drm_mode_modeinfo *mode = &kms.modes[index];
    struct drm_mode_create_dumb create = {.width = mode->hdisplay, .height = mode->vdisplay, .bpp = 32};
    if (kms_ioctl(kms.fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) return 0;
    trial_scanout.handle = create.handle;
    struct drm_mode_fb_cmd fb = {.width = create.width, .height = create.height,
        .pitch = create.pitch, .bpp = 32, .depth = 24, .handle = create.handle};
    if (kms_ioctl(kms.fd, DRM_IOCTL_MODE_ADDFB, &fb) < 0) goto failed;
    trial_scanout.fb = fb.fb_id;
    struct drm_mode_map_dumb map = {.handle = create.handle};
    MaySurface surface = {.width = create.width, .height = create.height, .pitch = create.pitch,
        .bpp = 32, .red = 16, .red_bits = 8, .green = 8, .green_bits = 8, .blue_bits = 8};
    if (!surface_valid(&surface, create.size) ||
        kms_ioctl(kms.fd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) goto failed;
    void *memory = mmap(NULL, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, kms.fd, map.offset);
    if (memory == MAP_FAILED) goto failed;
    trial_scanout.surface = surface;
    trial_scanout.surface.data = memory;
    trial_scanout.size = create.size;
    if (!kms_set_output(fb.fb_id, mode)) goto failed;
    kms_trial = index;
    serial("MayOS display: DRM/KMS selected monitor mode activated\n");
    return 1;
failed:
    kms_release(&trial_scanout);
    return 0;
}

static int kms_finish_trial(int keep) {
    if (kms_trial < 0) return 0;
    if (!keep) {
        int restored = scanout.fb ? kms_set_output(scanout.fb, &kms.modes[kms.selected])
                                  : kms_apply(&kms.original);
        if (!restored) return 0;
        kms_release(&trial_scanout);
        serial("MayOS display: DRM/KMS previous mode restored\n");
    } else {
        kms_release(&scanout);
        scanout = trial_scanout;
        trial_scanout = (KmsBuffer){0};
        kms.selected = kms_trial;
        serial("MayOS display: DRM/KMS selected mode kept\n");
    }
    kms_trial = -1;
    return 1;
}

static const MaySurface *kms_surface(void) {
    return kms_trial >= 0 ? &trial_scanout.surface : scanout.fb ? &scanout.surface : &display;
}

static void kms_present(const u32 *pixels) {
    const KmsBuffer *buffer = kms_trial >= 0 ? &trial_scanout : &scanout;
    surface_present(&buffer->surface, pixels, display_scale_mode);
    if (kms.dirtyfb) {
        struct drm_mode_fb_dirty_cmd dirty = {.fb_id = buffer->fb};
        if (kms_ioctl(kms.fd, DRM_IOCTL_MODE_DIRTYFB, &dirty) < 0) kms.dirtyfb = 0;
    }
}

static void kms_close(void) {
    if (kms.fd < 0) return;
    if (scanout.fb || trial_scanout.fb) kms_apply(&kms.original);
    kms_release(&trial_scanout);
    kms_release(&scanout);
    close(kms.fd);
    kms.fd = -1;
    kms_trial = -1;
}
