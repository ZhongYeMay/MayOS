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
