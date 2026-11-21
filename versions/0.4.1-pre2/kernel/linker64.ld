/* Keep the Multiboot header in the first 8 KiB and the ELF64 entry below
 * 4 GiB. GRUB enters the 32-bit bootstrap, which switches into long mode. */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
SECTIONS {
 . = 1M;
 .multiboot : { KEEP(*(.multiboot)) }
 . = ALIGN(4K);
 .text : { *(.text .text.*) }
 . = ALIGN(4K);
 .rodata : { *(.rodata .rodata.*) }
 . = ALIGN(4K);
 .data : { *(.data .data.*) }
 . = ALIGN(4K);
 .bss : { *(COMMON) *(.bss .bss.*) }
 kernel_end = .;
 /DISCARD/ : { *(.comment .note.GNU-stack .eh_frame*) }
}
