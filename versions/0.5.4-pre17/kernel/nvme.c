#include "core.h"
#include "pci.h"
#include "nvme.h"
#include "mmio.h"

#define NVME_QUEUE_SIZE 64
#define NVME_TIMEOUT 20000000u

typedef struct {
    u8 opcode, flags;
    u16 cid;
    u32 nsid;
    u64 reserved;
    u64 metadata;
    u64 prp1, prp2;
    u32 cdw10, cdw11, cdw12, cdw13, cdw14, cdw15;
} NvmeCommand;

typedef struct {
    u32 result, reserved;
    u16 sq_head, sq_id, cid, status;
} NvmeCompletion;

typedef struct {
    NvmeCommand *sq;
    volatile NvmeCompletion *cq;
    u16 tail, head;
    u8 phase, qid;
} NvmeQueue;

#define NVME_PAGE_BYTES 4096u
#define NVME_IDS_PER_PAGE (NVME_PAGE_BYTES / sizeof(u32))
#define NVME_MAX_NAMESPACES 1024u
#define NVME_STATUS_READY 1u
#define NVME_STATUS_FATAL 2u

typedef struct {
    NvmeQueue admin, io;
    volatile u8 *registers, *doorbells;
    u8 *data;
    u32 stride;
    u16 depth;
    int failed;
} NvmeController;

typedef struct {
    NvmeController *controller;
    u32 id;
    u64 sectors;
} NvmeNamespace;

static NvmeController controllers[PCI_MAX_DEVICES];
static NvmeNamespace namespaces[NVME_MAX_NAMESPACES];
static u32 active_ids[NVME_IDS_PER_PAGE];
static int namespace_count;

static u32 reg32(NvmeController *ctrl, u32 offset) {
    return *(volatile u32 *)(ctrl->registers + offset);
}

static u64 reg64(NvmeController *ctrl, u32 offset) {
    return *(volatile u64 *)(ctrl->registers + offset);
}

static void set32(NvmeController *ctrl, u32 offset, u32 value) {
    *(volatile u32 *)(ctrl->registers + offset) = value;
}

static void set64(NvmeController *ctrl, u32 offset, u64 value) {
    *(volatile u64 *)(ctrl->registers + offset) = value;
}

static int wait_ready(NvmeController *ctrl, int ready) {
    for (u32 timeout = 0; timeout < NVME_TIMEOUT; timeout++) {
        u32 status = reg32(ctrl, 0x1c);
        if (ready && (status & NVME_STATUS_FATAL)) return 0;
        if (((status & NVME_STATUS_READY) != 0) == ready) return 1;
    }
    return 0;
}

static int submit(NvmeController *ctrl, NvmeQueue *queue, NvmeCommand command) {
    if (ctrl->failed) return 0;
    command.cid = queue->tail;
    queue->sq[queue->tail] = command;
    queue->tail = (queue->tail + 1u) % ctrl->depth;
    __asm__ volatile("mfence" ::: "memory");
    *(volatile u32 *)(ctrl->doorbells +
        (2u * queue->qid) * ctrl->stride) = queue->tail;
    for (u32 timeout = 0; timeout < NVME_TIMEOUT; timeout++) {
        volatile NvmeCompletion *completion = &queue->cq[queue->head];
        u16 status = completion->status;
        if ((status & 1u) != queue->phase) {
            if (reg32(ctrl, 0x1c) & NVME_STATUS_FATAL) break;
            continue;
        }
        __asm__ volatile("mfence" ::: "memory");
        if (completion->cid != command.cid || completion->sq_id != queue->qid)
            break;
        queue->head++;
        if (queue->head == ctrl->depth) {
            queue->head = 0;
            queue->phase ^= 1u;
        }
        *(volatile u32 *)(ctrl->doorbells +
            (2u * queue->qid + 1u) * ctrl->stride) = queue->head;
        return (status & 0x0ffeu) == 0; /* Status code and code type. */
    }
    /* Pending DMA only addresses this controller's retained queues and data
     * page. Never submit another command or reuse those pages after timeout. */
    ctrl->failed = 1;
    return 0;
}

static int admin_create_queues(NvmeController *ctrl) {
    u32 queue_size = 1u | ((u32)(ctrl->depth - 1u) << 16);
    NvmeCommand command = { .opcode = 0x05, .prp1 = (u64)(uptr)ctrl->io.cq,
                            .cdw10 = queue_size, .cdw11 = 1u };
    if (!submit(ctrl, &ctrl->admin, command)) return 0;
    command = (NvmeCommand){ .opcode = 0x01, .prp1 = (u64)(uptr)ctrl->io.sq,
                            .cdw10 = queue_size, .cdw11 = 1u | (1u << 16) };
    return submit(ctrl, &ctrl->admin, command);
}

static int identify(NvmeController *ctrl, u32 id, u32 cns) {
    memset(ctrl->data, 0, NVME_PAGE_BYTES);
    NvmeCommand command = { .opcode = 0x06, .nsid = id,
                            .prp1 = (u64)(uptr)ctrl->data, .cdw10 = cns };
    return submit(ctrl, &ctrl->admin, command);
}

static int scan_namespaces(NvmeController *ctrl) {
    u32 previous = 0, seen = 0;
    for (;;) {
        if (!identify(ctrl, previous, 2)) return 0; /* Active NSIDs > previous. */
        memcpy(active_ids, ctrl->data, sizeof(active_ids));
        for (u32 i = 0; i < NVME_IDS_PER_PAGE; i++) {
            u32 id = active_ids[i];
            if (!id) return 1;
            if (id <= previous || id == 0xffffffffu || ++seen > NVME_MAX_NAMESPACES ||
                !identify(ctrl, id, 0))
                return 0;
            previous = id;

            const u8 *info = ctrl->data;
            u8 format = (info[26] & 0x0fu) | ((info[26] & 0x60u) >> 1);
            if (format > info[25]) return 0;
            const u8 *lba = info + 128 + (u32)format * 4u;
            if (lba[2] != 9 || lba[0] || lba[1] || (info[29] & 7u)) {
                serial("MayOS: NVMe namespace needs unsupported block format or metadata\n");
                continue;
            }
            u64 sectors = *(const u64 *)info;
            if (!sectors || namespace_count == NVME_MAX_NAMESPACES) return 0;
            namespaces[namespace_count++] = (NvmeNamespace){ctrl, id, sectors};
            serial("MayOS: NVMe generic namespace ready (512-byte NVM blocks)\n");
        }
    }
}

/* Returns 1 for a usable controller, 0 for an unsupported one, and -1 when
 * a supported controller could not be initialized safely. */
static int start_controller(NvmeController *ctrl, const PciDevice *device) {
    u32 bar = device->bar[0];
    if (bar & 1u) return -1;
    u64 address = bar & ~0x0fu;
    if ((bar & 6u) == 4u) address |= (u64)device->bar[1] << 32;
    if (!address || !(ctrl->registers = mmio_map(address, 0x1000))) return -1;
    pci_enable_memory_busmaster(device);
    u64 cap = reg64(ctrl, 0x00);
    if (cap == ~0ULL) return -1;
    if (!(cap & (1ULL << 37)) || ((cap >> 48) & 0x0fu))
        return 0; /* Requires the NVM command set and 4 KiB pages. */
    u32 depth = (u32)(cap & 0xffffu) + 1u;
    if (depth < 2) return 0;
    ctrl->depth = depth < NVME_QUEUE_SIZE ? depth : NVME_QUEUE_SIZE;
    ctrl->stride = 4u << ((cap >> 32) & 0x0fu);
    ctrl->doorbells = mmio_map(address + 0x1000, 3u * ctrl->stride + 4u);
    if (!ctrl->doorbells) return -1;
    if (address >= (64ULL << 30)) serial("MayOS: NVMe uses a 64-bit high MMIO BAR\n");
    set32(ctrl, 0x14, reg32(ctrl, 0x14) & ~1u);
    if (!wait_ready(ctrl, 0)) return -1;

    void *pages[5] = {0};
    for (u32 i = 0; i < 5; i++) {
        pages[i] = page_alloc();
        if (!pages[i]) {
            while (i) page_free(pages[--i]);
            return -1;
        }
    }
    ctrl->admin = (NvmeQueue){pages[0], pages[1], 0, 0, 1, 0};
    ctrl->io = (NvmeQueue){pages[2], pages[3], 0, 0, 1, 1};
    ctrl->data = pages[4];
    u32 queue_size = ctrl->depth - 1u;
    set32(ctrl, 0x24, queue_size | (queue_size << 16));
    set64(ctrl, 0x28, (u64)(uptr)ctrl->admin.sq);
    set64(ctrl, 0x30, (u64)(uptr)ctrl->admin.cq);
    set32(ctrl, 0x14, (6u << 16) | (4u << 20) | 1u);
    if (!wait_ready(ctrl, 1) || !admin_create_queues(ctrl)) return -1;
    return 1;
}

int nvme_init(void) {
    int after = -1;
    const PciDevice *device;
    while ((device = pci_find_class(0x01, 0x08, after))) {
        after = (int)(device - pci_device_at(0));
        if (device->prog_if != 0x02) continue;
        NvmeController *ctrl = &controllers[after];
        int started = start_controller(ctrl, device);
        if (started < 0 || (started && !scan_namespaces(ctrl))) return -1;
    }
    return namespace_count;
}

u32 nvme_namespace_count(void) { return (u32)namespace_count; }

u64 nvme_sector_count(int index) { return namespaces[index].sectors; }

static int transfer(int index, u8 opcode, u64 lba, void *buffer) {
    const NvmeNamespace *ns = &namespaces[index];
    NvmeController *ctrl = ns->controller;
    if (lba >= ns->sectors || ctrl->failed) return 0;
    if (opcode == 0x01) memcpy(ctrl->data, buffer, 512);
    NvmeCommand command = { .opcode = opcode, .nsid = ns->id,
                            .cdw10 = (u32)lba, .cdw11 = (u32)(lba >> 32),
                            .prp1 = (u64)(uptr)ctrl->data };
    if (!submit(ctrl, &ctrl->io, command)) return 0;
    if (opcode == 0x02) memcpy(buffer, ctrl->data, 512);
    return 1;
}

int nvme_read_sector(int index, u64 lba, void *buffer) {
    return transfer(index, 0x02, lba, buffer);
}

int nvme_write_sector(int index, u64 lba, const void *buffer) {
    return transfer(index, 0x01, lba, (void *)buffer);
}

int nvme_flush(int index) {
    const NvmeNamespace *ns = &namespaces[index];
    NvmeCommand command = { .nsid = ns->id };
    return submit(ns->controller, &ns->controller->io, command);
}
