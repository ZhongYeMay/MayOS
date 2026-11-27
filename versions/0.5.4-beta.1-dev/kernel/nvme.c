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

static NvmeCommand admin_sq[NVME_QUEUE_SIZE] __attribute__((aligned(4096)));
static NvmeCompletion admin_cq[NVME_QUEUE_SIZE] __attribute__((aligned(4096)));
static NvmeCommand io_sq[NVME_QUEUE_SIZE] __attribute__((aligned(4096)));
static NvmeCompletion io_cq[NVME_QUEUE_SIZE] __attribute__((aligned(4096)));
static u8 identify_page[4096] __attribute__((aligned(4096)));
static NvmeQueue admin_queue, io_queue;
static volatile u8 *registers, *doorbells;
static u32 doorbell_stride, namespace_id;
static u64 namespace_sectors;

static u32 reg32(u32 offset) { return *(volatile u32 *)(registers + offset); }
static u64 reg64(u32 offset) { return *(volatile u64 *)(registers + offset); }
static void set32(u32 offset, u32 value) { *(volatile u32 *)(registers + offset) = value; }
static void set64(u32 offset, u64 value) { *(volatile u64 *)(registers + offset) = value; }

static int wait_ready(int ready) {
    for (u32 timeout = 0; timeout < NVME_TIMEOUT; timeout++)
        if (((reg32(0x1c) & 1u) != 0) == ready) return 1;
    return 0;
}

static void prps(NvmeCommand *command, void *buffer, u32 bytes) {
    u64 address = (u64)(unsigned long)buffer;
    command->prp1 = address;
    if ((address & 4095u) + bytes > 4096u)
        command->prp2 = (address + 4096u) & ~4095ULL;
}

static int submit(NvmeQueue *queue, const NvmeCommand *source) {
    NvmeCommand command = *source;
    command.cid = queue->tail;
    queue->sq[queue->tail] = command;
    queue->tail = (queue->tail + 1u) % NVME_QUEUE_SIZE;
    __asm__ volatile("mfence" ::: "memory");
    *(volatile u32 *)(doorbells +
        (2u * queue->qid) * doorbell_stride) = queue->tail;
    for (u32 timeout = 0; timeout < NVME_TIMEOUT; timeout++) {
        volatile NvmeCompletion *completion = &queue->cq[queue->head];
        if ((completion->status & 1u) != queue->phase) continue;
        u16 status = completion->status;
        queue->head++;
        if (queue->head == NVME_QUEUE_SIZE) {
            queue->head = 0;
            queue->phase ^= 1u;
        }
        *(volatile u32 *)(doorbells +
            (2u * queue->qid + 1u) * doorbell_stride) = queue->head;
        return (status >> 1) == 0;
    }
    return 0;
}

static int admin_create_queues(void) {
    NvmeCommand command;
    memset(&command, 0, sizeof(command));
    command.opcode = 0x05; /* Create I/O Completion Queue. */
    command.prp1 = (u64)(unsigned long)io_cq;
    command.cdw10 = 1u | ((NVME_QUEUE_SIZE - 1u) << 16);
    command.cdw11 = 1u; /* Physically contiguous, polled. */
    if (!submit(&admin_queue, &command)) return 0;
    memset(&command, 0, sizeof(command));
    command.opcode = 0x01; /* Create I/O Submission Queue. */
    command.prp1 = (u64)(unsigned long)io_sq;
    command.cdw10 = 1u | ((NVME_QUEUE_SIZE - 1u) << 16);
    command.cdw11 = 1u | (1u << 16); /* Contiguous; completion queue 1. */
    return submit(&admin_queue, &command);
}

static int identify_namespace(void) {
    NvmeCommand command;
    memset(identify_page, 0, sizeof(identify_page));
    memset(&command, 0, sizeof(command));
    command.opcode = 0x06;
    command.cdw10 = 2; /* Active namespace ID list. */
    prps(&command, identify_page, sizeof(identify_page));
    if (!submit(&admin_queue, &command)) return 0;
    namespace_id = *(u32 *)identify_page;
    if (!namespace_id) return 0;
    memset(identify_page, 0, sizeof(identify_page));
    memset(&command, 0, sizeof(command));
    command.opcode = 0x06;
    command.nsid = namespace_id;
    prps(&command, identify_page, sizeof(identify_page));
    if (!submit(&admin_queue, &command)) return 0;
    u8 format = identify_page[26] & 0x0fu;
    u8 lba_shift = identify_page[128 + (u32)format * 4u + 2u];
    if (lba_shift != 9) {
        serial("MayOS: NVMe namespace does not use 512-byte logical blocks\n");
        return 0;
    }
    namespace_sectors = *(u64 *)identify_page;
    return namespace_sectors != 0;
}

int nvme_init(void) {
    registers = 0;
    doorbells = 0;
    namespace_sectors = 0;
    const PciDevice *device = pci_find_class(0x01, 0x08, -1);
    if (!device || device->prog_if != 0x02) return 0;
    u32 bar0 = device->bar[0];
    if (bar0 & 1u) return 0;
    u64 address = bar0 & ~0x0fu;
    if ((bar0 & 6u) == 4u) address |= (u64)device->bar[1] << 32;
    if (!address || !(registers = mmio_map(address, 0x1000))) return 0;
    pci_enable_memory_busmaster(device);
    u64 capabilities = reg64(0x00);
    if (!(capabilities & (1ULL << 37)) || ((capabilities >> 48) & 0x0fu))
        return 0; /* NVM command set and 4 KiB minimum pages required. */
    doorbell_stride = 4u << ((capabilities >> 32) & 0x0fu);
    doorbells = mmio_map(address + 0x1000, 3u * doorbell_stride + 4u);
    if (!doorbells) return 0;
    if (address >= (64ULL << 30)) serial("MayOS: NVMe uses a 64-bit high MMIO BAR\n");
    set32(0x14, reg32(0x14) & ~1u);
    if (!wait_ready(0)) return 0;
    memset(admin_sq, 0, sizeof(admin_sq)); memset(admin_cq, 0, sizeof(admin_cq));
    memset(io_sq, 0, sizeof(io_sq)); memset(io_cq, 0, sizeof(io_cq));
    admin_queue = (NvmeQueue){admin_sq, admin_cq, 0, 0, 1, 0};
    io_queue = (NvmeQueue){io_sq, io_cq, 0, 0, 1, 1};
    set32(0x24, (NVME_QUEUE_SIZE - 1u) | ((NVME_QUEUE_SIZE - 1u) << 16));
    set64(0x28, (u64)(unsigned long)admin_sq);
    set64(0x30, (u64)(unsigned long)admin_cq);
    set32(0x14, (6u << 16) | (4u << 20) | 1u);
    if (!wait_ready(1) || !admin_create_queues() || !identify_namespace()) {
        registers = 0;
        return 0;
    }
    serial("MayOS: NVMe generic namespace ready (512-byte NVM blocks)\n");
    return 1;
}

u64 nvme_sector_count(void) { return namespace_sectors; }

static int io(u8 opcode, u64 lba, void *buffer) {
    if (!registers || lba >= namespace_sectors) return 0;
    NvmeCommand command;
    memset(&command, 0, sizeof(command));
    command.opcode = opcode; command.nsid = namespace_id;
    command.cdw10 = (u32)lba; command.cdw11 = (u32)(lba >> 32);
    prps(&command, buffer, 512);
    return submit(&io_queue, &command);
}
int nvme_read_sector(u64 lba, void *buffer) { return io(0x02, lba, buffer); }
int nvme_write_sector(u64 lba, const void *buffer) { return io(0x01, lba, (void *)buffer); }
int nvme_flush(void) {
    if (!registers) return 0;
    NvmeCommand command; memset(&command, 0, sizeof(command));
    command.nsid = namespace_id;
    return submit(&io_queue, &command);
}
