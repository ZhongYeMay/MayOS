[BITS 16]
[ORG 0x1000]

kernel_start:
    ; 显示内核启动信息
    mov si, kernel_msg
    call print_string
    
    ; 初始化系统
    call init_system
    
    ; 显示提示符
    call show_prompt

main_loop:
    ; 获取用户输入
    call get_input
    
    ; 处理命令
    call process_command
    
    ; 显示新提示符
    call show_prompt
    jmp main_loop

init_system:
    mov si, init_msg
    call print_string
    ret

show_prompt:
    mov si, prompt
    call print_string
    ret

get_input:
    mov di, input_buffer
    mov cx, 0
    
.input_loop:
    mov ah, 0x00
    int 0x16        ; 获取键盘输入
    
    cmp al, 13      ; 回车键
    je .input_done
    
    cmp al, 8       ; 退格键
    je .backspace
    
    cmp al, 32      ; 空格及以上可打印字符
    jl .input_loop
    
    cmp cx, 79      ; 最大输入长度
    jge .input_loop
    
    ; 显示字符
    mov ah, 0x0E
    int 0x10
    
    ; 存储字符
    mov [di], al
    inc di
    inc cx
    jmp .input_loop

.backspace:
    cmp cx, 0
    je .input_loop
    
    ; 删除字符
    mov ah, 0x0E
    mov al, 8
    int 0x10
    mov al, ' '
    int 0x10
    mov al, 8
    int 0x10
    
    dec di
    dec cx
    jmp .input_loop

.input_done:
    mov byte [di], 0    ; 字符串结束符
    
    ; 换行
    mov ah, 0x0E
    mov al, 13
    int 0x10
    mov al, 10
    int 0x10
    ret

process_command:
    mov si, input_buffer
    
    ; 检查help命令
    mov di, cmd_help
    call strcmp
    cmp ax, 0
    je .cmd_help
    
    ; 检查clear命令
    mov di, cmd_clear
    call strcmp
    cmp ax, 0
    je .cmd_clear
    
    ; 检查info命令
    mov di, cmd_info
    call strcmp
    cmp ax, 0
    je .cmd_info
    
    ; 检查time命令
    mov di, cmd_time
    call strcmp
    cmp ax, 0
    je .cmd_time
    
    ; 检查reboot命令
    mov di, cmd_reboot
    call strcmp
    cmp ax, 0
    je .cmd_reboot
    
    ; 检查空命令
    cmp byte [input_buffer], 0
    je .cmd_empty
    
    ; 未知命令
    mov si, unknown_msg
    call print_string
    ret

.cmd_help:
    mov si, help_msg
    call print_string
    ret

.cmd_clear:
    mov ax, 0x0003
    int 0x10
    mov si, kernel_msg
    call print_string
    ret

.cmd_info:
    mov si, info_msg
    call print_string
    ret

.cmd_time:
    call show_time
    ret

.cmd_reboot:
    mov si, reboot_msg
    call print_string
    ; 重启系统
    mov al, 0xFE
    out 0x64, al
    jmp $

.cmd_empty:
    ret

show_time:
    ; 获取系统时间
    mov ah, 0x02
    int 0x1A
    
    ; 显示时间
    mov si, time_msg
    call print_string
    
    ; 显示小时
    mov al, ch
    call print_hex
    mov al, ':'
    call print_char
    
    ; 显示分钟
    mov al, cl
    call print_hex
    mov al, ':'
    call print_char
    
    ; 显示秒
    mov al, dh
    call print_hex
    
    ; 换行
    call print_newline
    ret

print_hex:
    push ax
    shr al, 4
    call print_hex_digit
    pop ax
    and al, 0x0F
    call print_hex_digit
    ret

print_hex_digit:
    cmp al, 9
    jle .digit
    add al, 7
.digit:
    add al, '0'
    call print_char
    ret

print_char:
    mov ah, 0x0E
    int 0x10
    ret

print_newline:
    mov al, 13
    call print_char
    mov al, 10
    call print_char
    ret

strcmp:
    push si
    push di
.loop:
    mov al, [si]
    mov bl, [di]
    cmp al, bl
    jne .not_equal
    cmp al, 0
    je .equal
    inc si
    inc di
    jmp .loop
.equal:
    mov ax, 0
    jmp .done
.not_equal:
    mov ax, 1
.done:
    pop di
    pop si
    ret

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

; 数据段
kernel_msg db 'MayOS ver0.1 - Kernel Ready', 13, 10, 0
init_msg db 'System Initialized', 13, 10, 13, 10, 0
prompt db 'MayOS> ', 0
help_msg db 'Available Commands:', 13, 10
         db '  help   - Show this help', 13, 10
         db '  clear  - Clear screen', 13, 10
         db '  info   - System information', 13, 10
         db '  time   - Show current time', 13, 10
         db '  reboot - Restart system', 13, 10, 0
info_msg db 'MayOS ver0.1', 13, 10
         db 'Simple Operating System', 13, 10
         db 'Architecture: x86 16-bit', 13, 10
         db 'Memory: Real Mode', 13, 10, 0
time_msg db 'Current Time: ', 0
reboot_msg db 'Rebooting system...', 13, 10, 0
unknown_msg db 'Unknown command. Type "help" for available commands.', 13, 10, 0

cmd_help db 'help', 0
cmd_clear db 'clear', 0
cmd_info db 'info', 0
cmd_time db 'time', 0
cmd_reboot db 'reboot', 0

input_buffer times 80 db 0

; 填充到扇区边界
times 2048-($-$$) db 0