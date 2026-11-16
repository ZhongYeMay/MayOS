#ifndef MAYOS_UPDATER_H
#define MAYOS_UPDATER_H
/* Shared, bounded release metadata and update state; no network, disk writes,
 * install or reboot side effects in this module. Safe in freestanding x86_64. */
#define MAYOS_UPDATER_TAG 32
#define MAYOS_UPDATER_NAME 64
#define MAYOS_UPDATER_KERNEL 72
#define MAYOS_UPDATER_DATE 16
#define MAYOS_UPDATER_NOTE 128
#define MAYOS_UPDATER_URL 224
#define MAYOS_UPDATER_SHA 65

typedef struct {
    char tag[MAYOS_UPDATER_TAG];
    char variant[16];
    char name[MAYOS_UPDATER_NAME];
    char arch[16];
    char kernel[MAYOS_UPDATER_KERNEL];
    char date[MAYOS_UPDATER_DATE];
    char notes[MAYOS_UPDATER_NOTE];
    char url[MAYOS_UPDATER_URL];
    char sha256[MAYOS_UPDATER_SHA];
    unsigned int bytes;
    int preview;
} MayUpdateRelease;

typedef struct {
    const char *tag, *variant, *name, *arch, *kernel, *date;
    const char *url, *sha256, *notes;
    unsigned int bytes;
} MayUpdateCatalogRow;

enum MayUpdatePhase {
    MAY_UPDATE_OFFLINE = 0,
    MAY_UPDATE_CHECKING,
    MAY_UPDATE_NO_NEWER,
    MAY_UPDATE_AVAILABLE,
    MAY_UPDATE_DOWNLOADING,
    MAY_UPDATE_VERIFYING,
    MAY_UPDATE_VERIFIED,
    MAY_UPDATE_FAILED
};

typedef struct {
    MayUpdateRelease latest;
    enum MayUpdatePhase phase;
    unsigned long long transferred, total;
    unsigned int elapsed_seconds;
    int has_latest, has_upgrade, online_checked, verified;
    char message[96];
} MayUpdaterState;

const MayUpdaterState *may_updater_state(void);
void may_updater_init(void);
void may_updater_offline(void);
#ifdef MAYOS_LINUX
int platform_update_check_start(void);
int platform_update_check_poll(void);
#endif
const char *may_updater_current_version(void);
int may_updater_compare(const char *a, const char *b);
int may_updater_feed_line(const char *line);
void may_updater_checking(void);
void may_updater_checked(void);
/* SHA-256 completes only after real data chunks are ingested. The caller must
 * separately stage data safely: this module NEVER writes a boot device. */
int may_updater_stream_begin(void);
int may_updater_stream_chunk(const void *data, unsigned int size,
                             unsigned int elapsed_seconds);
int may_updater_stream_finish(void);
void may_updater_failed(const char *message);
unsigned int may_updater_download_percent(void);
unsigned int may_updater_download_eta(void);
#endif
