[BITS 16]
[ORG 0x7C00]

start:
    ; 初始化段寄存器
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    
    ; 清屏
    mov ax, 0x0003
    int 0x10
    
    ; 显示启动信息
    mov si, boot_msg
    call print_string
    
    ; 加载内核到0x1000
    mov ah, 0x02        ; 读扇区
    mov al, 4           ; 读4个扇区
    mov ch, 0           ; 柱面0
    mov cl, 2           ; 扇区2
    mov dh, 0           ; 磁头0
    mov bx, 0x1000      ; 加载地址
    int 0x13
    
    jc load_error
    
    ; 跳转到内核
    jmp 0x1000

load_error:
    mov si, error_msg
    call print_string
    jmp $

print_string:
    mov ah, 0x0E
    mov bh, 0
.loop:
    lodsb
    cmp al, 0
    je .done
    int 0x10
    jmp .loop
.done:
    ret

boot_msg db 'MayOS ver0.1 - Booting...', 13, 10, 0
error_msg db 'Kernel load failed!', 13, 10, 0

times 510-($-$$) db 0
dw 0xAA55