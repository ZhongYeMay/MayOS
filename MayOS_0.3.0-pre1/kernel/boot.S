.set MAGIC, 0x1badb002
.set FLAGS, 0x00000007
.section .multiboot,"a"
.align 4
.long MAGIC, FLAGS, -(MAGIC + FLAGS)
.long 0,0,0,0,0
# Firmware/GRUB selects a supported RGB mode; no fixed resolution or depth.
.long 0,0,0,0
.section .bss
.align 16
stack_bottom:
.skip 65536
stack_top:
.section .text
.global _start
.type _start,@function
_start:
 cli
 cld
 lgdt gdt_ptr
 ljmp $8, $protected
protected:
 mov $16, %cx
 mov %cx, %ds
 mov %cx, %es
 mov %cx, %ss
 mov %cx, %fs
 mov %cx, %gs
 mov $stack_top, %esp
 xor %ebp,%ebp
 push %ebx
 push %eax
 call kernel_main
1: hlt
 jmp 1b
.global timer_stub
timer_stub:
 pushal
 call timer_interrupt
 popal
 iret
 .align 8
gdt:
 .quad 0, 0x00cf9a000000ffff, 0x00cf92000000ffff
gdt_end:
gdt_ptr:
 .word gdt_end-gdt-1
 .long gdt
.section .note.GNU-stack,"",@progbits
