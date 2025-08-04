[BITS 16]
[ORG 0x7C00]

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov ax, 0x0003
    int 0x10

    mov si, title
    call print_string
    
    ; 长进度条
    mov bx, 0
progress:
    mov ah, 0x02
    mov bh, 0
    mov dh, 13
    mov dl, 5
    int 0x10
    
    mov si, bar
    call print_string
    
    mov cx, bx
    mov al, 0xDB
    mov ah, 0x0E
.fill:
    test cx, cx
    jz .done
    int 0x10
    dec cx
    jmp .fill
.done:
    
    call delay
    inc bx
    cmp bx, 15
    jle progress
    
    mov si, info
    call print_string

loop:
    hlt
    jmp loop

print_string:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print_string
.done:
    ret

delay:
    push cx
    mov cx, 0x2000
.wait:
    nop
    loop .wait
    pop cx
    ret

title   db '===============================================', 0x0D, 0x0A
        db 'M     M     AAAAA     Y   Y     OOOOO    SSSSS', 0x0D, 0x0A
        db 'MM   MM    A     A     Y Y     O     O  S     ', 0x0D, 0x0A
        db 'M M M M   AAAAAAAA      Y      O     O   SSS  ', 0x0D, 0x0A
        db 'M  M  M  A       A      Y      O     O      S ', 0x0D, 0x0A
        db 'M     M A         A     Y       OOOOO   SSSSS', 0x0D, 0x0A
        db '===============================================', 0x0D, 0x0A
        db '   MayOS v0.2 [Hopeful]', 0x0D, 0x0A
        db '    Team 2023~2025', 0x0D, 0x0A, 0x0D, 0x0A, 0
bar     db 'Loading [', 0
info    db '] System Ready!', 0x0D, 0x0A, 0

times 510-($-$$) db 0
dw 0xAA55