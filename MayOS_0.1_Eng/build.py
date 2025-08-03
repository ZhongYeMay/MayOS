#!/usr/bin/env python3

import os
import subprocess

def run_nasm(source, output, format_type="bin"):
    """运行NASM编译"""
    nasm_path = r"D:\Program Files\NASM\nasm.exe"
    cmd = [nasm_path, f"-f", format_type, source, "-o", output]
    
    print(f"Compiling {source}...")
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    if result.returncode == 0:
        print(f"[OK] {source} compiled successfully")
        return True
    else:
        print(f"[ERROR] Error compiling {source}:")
        print(result.stderr)
        return False

def create_os_image():
    """创建操作系统镜像"""
    print("Building MayOS - Simple Operating System")
    print("=" * 50)
    
    # 编译启动扇区
    if not run_nasm("boot.asm", "boot.bin"):
        return False
    
    # 编译内核
    if not run_nasm("kernel.asm", "kernel.bin"):
        return False
    
    # 创建磁盘镜像
    print("\nCreating disk image...")
    
    # 读取启动扇区
    with open("boot.bin", "rb") as f:
        boot_sector = f.read()
    
    # 读取内核
    with open("kernel.bin", "rb") as f:
        kernel = f.read()
    
    # 创建1.44MB软盘镜像
    img_size = 1474560
    
    with open("mayos.img", "wb") as f:
        # 写入启动扇区 (扇区0)
        f.write(boot_sector)
        if len(boot_sector) < 512:
            f.write(b'\x00' * (512 - len(boot_sector)))
        
        # 写入内核 (从扇区1开始)
        f.write(kernel)
        
        # 填充剩余空间
        written = 512 + len(kernel)
        remaining = img_size - written
        if remaining > 0:
            f.write(b'\x00' * remaining)
    
    print(f"[OK] Created mayos.img ({img_size} bytes)")
    print(f"  Boot sector: {len(boot_sector)} bytes")
    print(f"  Kernel: {len(kernel)} bytes")
    
    # 验证启动签名
    if len(boot_sector) >= 2 and boot_sector[-2:] == b'\x55\xAA':
        print("[OK] Boot signature valid")
    else:
        print("[ERROR] Boot signature invalid")
    
    print("\n" + "=" * 50)
    print("MayOS build completed successfully!")
    print("\nFeatures:")
    print("- Boot loader with kernel loading")
    print("- Interactive command line")
    print("- Built-in commands: help, clear, info, time, reboot")
    print("- Real-time clock display")
    print("- System information")
    print("\nTo run:")
    print('  "D:\\Program Files\\qemu\\qemu-system-i386.exe" -fda mayos.img')
    
    return True

if __name__ == "__main__":
    create_os_image()