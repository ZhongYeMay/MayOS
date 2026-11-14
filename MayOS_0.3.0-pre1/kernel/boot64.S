/* Multiboot1 x86 entry -> genuine x86_64 long mode.
 * GRUB hands off in 32-bit protected mode on BIOS and x64 UEFI.
 * Identity-map 0..64 GiB with 2 MiB huge pages before executing 64-bit C.
 * CPU must support PAE and long mode; no 32-bit kernel is linked. */
.set MB_MAGIC, 0x1badb002
.set MB_FLAGS, 0x00000007
.section .multiboot,"a"
.balign 4
.long MB_MAGIC, MB_FLAGS, -(MB_MAGIC + MB_FLAGS)
.long 0,0,0,0,0
.long 0,0,0,0

.section .text
.code32
.global _start
.type _start,@function
_start:
    cli
    cld
    movl %eax, boot_magic
    movl %ebx, boot_info

    /* Reject CPUs lacking CPUID long-mode support. */
    movl $0x80000000, %eax
    cpuid
    cmpl $0x80000001, %eax
    jb .Lhalt32
    movl $0x80000001, %eax
    cpuid
    btl $29, %edx
    jnc .Lhalt32

    /* PML4[0] -> PDPT; 64 consecutive 1-GiB PDs cover [0, 64 GiB).
     * This includes firmware RGB BARs above 4 GiB without truncating pointers. */
    movl $(pdpt + 3), pml4
    xorl %ebx, %ebx
.Lpdpt:
    movl %ebx, %eax
    shll $12, %eax
    addl $(pd_tables + 3), %eax
    movl %eax, pdpt(,%ebx,8)
    movl $0, pdpt + 4(,%ebx,8)
    incl %ebx
    cmpl $64, %ebx
    jb .Lpdpt
    xorl %ebx, %ebx
.Lidentity:
    movl %ebx, %eax
    shll $21, %eax
    orl $0x83, %eax              /* present | writable | 2 MiB page */
    movl %eax, pd_tables(,%ebx,8)
    movl %ebx, %eax
    shrl $11, %eax
    movl %eax, pd_tables + 4(,%ebx,8)
    incl %ebx
    cmpl $32768, %ebx
    jb .Lidentity

    movl $pml4, %eax
    movl %eax, %cr3
    movl %cr4, %eax
    orl $0x20, %eax               /* CR4.PAE */
    movl %eax, %cr4
    movl $0xc0000080, %ecx       /* EFER.LME */
    rdmsr
    orl $0x100, %eax
    wrmsr
    lgdt gdt64_ptr
    movl %cr0, %eax
    orl $0x80000000, %eax        /* CR0.PG -> long mode active */
    movl %eax, %cr0
    ljmp $0x08, $long_mode_entry

.Lhalt32:
    cli
    hlt
    jmp .Lhalt32

.code64
long_mode_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
    leaq stack_top(%rip), %rsp
    andq $-16, %rsp
    xorl %ebp, %ebp
    movl boot_magic(%rip), %edi
    movl boot_info(%rip), %esi
    call kernel_main
.Lhalt64:
    cli
    hlt
    jmp .Lhalt64

.global timer_stub
.type timer_stub,@function
timer_stub:
    pushq %rax
    incl ticks(%rip)
    movb $0x20, %al
    outb %al, $0x20
    popq %rax
    iretq

/* Fatal CPU exception entries. For vectors with CPU-pushed error codes,
 * preserve that word. For other vectors synthesize a zero error code.
 * At dispatch: [vector, error, RIP, CS, RFLAGS, ...].
 * The handler logs details and halts; exception recovery is not implemented. */
.global mayos_exc_0
.type mayos_exc_0,@function
mayos_exc_0:
    pushq $0
    pushq $0
    jmp mayos_exception_entry

.global mayos_exc_1
.type mayos_exc_1,@function
mayos_exc_1:
    pushq $0
    pushq $1
    jmp mayos_exception_entry

.global mayos_exc_2
.type mayos_exc_2,@function
mayos_exc_2:
    pushq $0
    pushq $2
    jmp mayos_exception_entry

.global mayos_exc_3
.type mayos_exc_3,@function
mayos_exc_3:
    pushq $0
    pushq $3
    jmp mayos_exception_entry

.global mayos_exc_4
.type mayos_exc_4,@function
mayos_exc_4:
    pushq $0
    pushq $4
    jmp mayos_exception_entry

.global mayos_exc_5
.type mayos_exc_5,@function
mayos_exc_5:
    pushq $0
    pushq $5
    jmp mayos_exception_entry

.global mayos_exc_6
.type mayos_exc_6,@function
mayos_exc_6:
    pushq $0
    pushq $6
    jmp mayos_exception_entry

.global mayos_exc_7
.type mayos_exc_7,@function
mayos_exc_7:
    pushq $0
    pushq $7
    jmp mayos_exception_entry

.global mayos_exc_8
.type mayos_exc_8,@function
mayos_exc_8:
    pushq $8
    jmp mayos_exception_entry

.global mayos_exc_9
.type mayos_exc_9,@function
mayos_exc_9:
    pushq $0
    pushq $9
    jmp mayos_exception_entry

.global mayos_exc_10
.type mayos_exc_10,@function
mayos_exc_10:
    pushq $10
    jmp mayos_exception_entry

.global mayos_exc_11
.type mayos_exc_11,@function
mayos_exc_11:
    pushq $11
    jmp mayos_exception_entry

.global mayos_exc_12
.type mayos_exc_12,@function
mayos_exc_12:
    pushq $12
    jmp mayos_exception_entry

.global mayos_exc_13
.type mayos_exc_13,@function
mayos_exc_13:
    pushq $13
    jmp mayos_exception_entry

.global mayos_exc_14
.type mayos_exc_14,@function
mayos_exc_14:
    pushq $14
    jmp mayos_exception_entry

.global mayos_exc_15
.type mayos_exc_15,@function
mayos_exc_15:
    pushq $0
    pushq $15
    jmp mayos_exception_entry

.global mayos_exc_16
.type mayos_exc_16,@function
mayos_exc_16:
    pushq $0
    pushq $16
    jmp mayos_exception_entry

.global mayos_exc_17
.type mayos_exc_17,@function
mayos_exc_17:
    pushq $17
    jmp mayos_exception_entry

.global mayos_exc_18
.type mayos_exc_18,@function
mayos_exc_18:
    pushq $0
    pushq $18
    jmp mayos_exception_entry

.global mayos_exc_19
.type mayos_exc_19,@function
mayos_exc_19:
    pushq $0
    pushq $19
    jmp mayos_exception_entry

.global mayos_exc_20
.type mayos_exc_20,@function
mayos_exc_20:
    pushq $0
    pushq $20
    jmp mayos_exception_entry

.global mayos_exc_21
.type mayos_exc_21,@function
mayos_exc_21:
    pushq $21
    jmp mayos_exception_entry

.global mayos_exc_22
.type mayos_exc_22,@function
mayos_exc_22:
    pushq $0
    pushq $22
    jmp mayos_exception_entry

.global mayos_exc_23
.type mayos_exc_23,@function
mayos_exc_23:
    pushq $0
    pushq $23
    jmp mayos_exception_entry

.global mayos_exc_24
.type mayos_exc_24,@function
mayos_exc_24:
    pushq $0
    pushq $24
    jmp mayos_exception_entry

.global mayos_exc_25
.type mayos_exc_25,@function
mayos_exc_25:
    pushq $0
    pushq $25
    jmp mayos_exception_entry

.global mayos_exc_26
.type mayos_exc_26,@function
mayos_exc_26:
    pushq $0
    pushq $26
    jmp mayos_exception_entry

.global mayos_exc_27
.type mayos_exc_27,@function
mayos_exc_27:
    pushq $0
    pushq $27
    jmp mayos_exception_entry

.global mayos_exc_28
.type mayos_exc_28,@function
mayos_exc_28:
    pushq $0
    pushq $28
    jmp mayos_exception_entry

.global mayos_exc_29
.type mayos_exc_29,@function
mayos_exc_29:
    pushq $29
    jmp mayos_exception_entry

.global mayos_exc_30
.type mayos_exc_30,@function
mayos_exc_30:
    pushq $30
    jmp mayos_exception_entry

.global mayos_exc_31
.type mayos_exc_31,@function
mayos_exc_31:
    pushq $0
    pushq $31
    jmp mayos_exception_entry

mayos_exception_entry:
    cli
    movq (%rsp), %rdi         /* vector */
    movq 8(%rsp), %rsi        /* error code */
    movq 16(%rsp), %rdx       /* saved instruction pointer */
    andq $-16, %rsp
    call kernel_exception
.Lfatal_halt:
    cli
    hlt
    jmp .Lfatal_halt

.section .rodata
.balign 8
gdt64:
    .quad 0x0000000000000000
    .quad 0x00af9a000000ffff
    .quad 0x00cf92000000ffff
gdt64_end:
gdt64_ptr:
    .word gdt64_end - gdt64 - 1
    .long gdt64

.balign 8
.global mayos_exception_stubs
.type mayos_exception_stubs,@object
mayos_exception_stubs:
    .quad mayos_exc_0
    .quad mayos_exc_1
    .quad mayos_exc_2
    .quad mayos_exc_3
    .quad mayos_exc_4
    .quad mayos_exc_5
    .quad mayos_exc_6
    .quad mayos_exc_7
    .quad mayos_exc_8
    .quad mayos_exc_9
    .quad mayos_exc_10
    .quad mayos_exc_11
    .quad mayos_exc_12
    .quad mayos_exc_13
    .quad mayos_exc_14
    .quad mayos_exc_15
    .quad mayos_exc_16
    .quad mayos_exc_17
    .quad mayos_exc_18
    .quad mayos_exc_19
    .quad mayos_exc_20
    .quad mayos_exc_21
    .quad mayos_exc_22
    .quad mayos_exc_23
    .quad mayos_exc_24
    .quad mayos_exc_25
    .quad mayos_exc_26
    .quad mayos_exc_27
    .quad mayos_exc_28
    .quad mayos_exc_29
    .quad mayos_exc_30
    .quad mayos_exc_31

.section .bss
.balign 4
boot_magic: .skip 4
boot_info:  .skip 4
.balign 4096
pml4: .skip 4096
pdpt: .skip 4096
pd_tables: .skip 262144
.balign 16
stack_bottom: .skip 65536
stack_top:

.section .note.GNU-stack,"",@progbits
