# MayOS

## 当前测试版：0.3.0-pre1

同一张 BIOS / UEFI x64 混合 ISO 可选择 **MayOS 自研内核桌面**、**Linux 7.3.1 图形桌面** 或 Linux 救援环境。

- [当前版本源码与构建说明](MayOS_0.3.0-pre1/README.md)
- [0.3.0-pre1 完整下载与 QEMU 启动录像](https://github.com/ZhongYeMay/MayOS/releases/tag/v0.3.0-pre1)
- [公开官网](https://mayos-official.business-tea-9633.chatgpt.site/)
- [本次更新说明](MayOS_0.3.0-pre1/RELEASE_NOTES.md)
- [第三方组件、来源和许可](MayOS_0.3.0-pre1/docs/third-party/dual-boot.md)

该版本已在 QEMU 的 BIOS 和 OVMF x64 UEFI 环境中验证。MayKernel 仍是 32 位自研内核，Linux 分支为 x86_64；UEFI 测试关闭 Secure Boot。Linux 图形桌面的文件存于进程 RAM，退出桌面后清空。完整范围与验证记录见当前版本文档。

MayOS Team 2023 - 2026.  
This version of MayOS is for test use only.

## 历史版本

`MayOS_0.1_Eng/` 和 `MayOS_0.2_Eng/` 为此前的 16 位实验版本。以下保留原有说明；它描述的是历史版本，不代表上面的 0.3.0-pre1 图形桌面版本。仓库原有 LICENSE 与各第三方组件许可保持不变；当前源码的许可说明以其自身文档为准。

---

# MayOS
An operating system that can run on a physical machine.

# MayOS ver0.1 - Simple Operating System | 简单操作系统

[English](#english) | [中文](#中文)

---

## English

A simple operating system written in assembly language with complete boot process and interactive command-line interface.

### Features

#### System Functions
- **Boot Loader** - Boot from floppy disk and load kernel
- **Kernel** - 16-bit real mode kernel providing basic system services
- **Command Line Interface** - Interactive shell supporting multiple commands
- **Real-time Clock** - Display system time
- **System Information** - Display OS version and architecture info

#### Available Commands
- `help` - Show help information and available commands
- `clear` - Clear screen
- `info` - Display system information
- `time` - Show current time
- `reboot` - Restart system

#### User Interface
- Boot message "MayOS ver0.1 - Booting..."
- Interactive command prompt "MayOS> "
- Keyboard input support with backspace deletion
- Real-time command response

### Technical Specifications

- **Architecture**: x86 (16-bit real mode)
- **Boot Method**: Floppy disk boot (1.44MB)
- **Memory Requirement**: Minimum 16MB
- **File System**: None (direct sector loading from floppy)

### File Structure

- `boot.asm` - Boot sector code (512 bytes)
- `kernel.asm` - Kernel code (2048 bytes)
- `build.py` - Build script
- `run.bat` - Run script
- `start.bat` - Build and run script
- `mayos.img` - Generated disk image

### Build Requirements

- NASM assembler
- Python 3.x
- QEMU emulator (optional, for running)

### Build Instructions

```bash
# Build the operating system
python build.py

# Or use batch script (builds and runs)
start.bat
```

### Running Instructions

#### Using QEMU directly
```bash
qemu-system-i386.exe -fda mayos.img -m 16
```

#### Using batch script
```bash
run.bat
```

### Usage Instructions

1. After boot, you'll see "MayOS ver0.1 - Booting..." message
2. Kernel loads and displays "MayOS ver0.1 - Kernel Ready"
3. System initializes and shows command prompt "MayOS> "
4. Enter commands and press Enter to execute
5. Use `help` command to see all available commands

### Example Session

```
MayOS ver0.1 - Booting...
MayOS ver0.1 - Kernel Ready
System Initialized

MayOS> help
Available Commands:
  help   - Show this help
  clear  - Clear screen
  info   - System information
  time   - Show current time
  reboot - Restart system

MayOS> info
MayOS ver0.1
Simple Operating System
Architecture: x86 16-bit
Memory: Real Mode

MayOS> time
Current Time: 14:30:25

MayOS> reboot
Rebooting system...
```

### Future Extensions

This basic operating system can be further extended with:
- 32-bit protected mode support
- Memory management
- Multitasking
- File system
- Device drivers
- Network support

---

## 中文

一个用汇编语言编写的简单操作系统，具有完整的启动过程和交互式命令行界面。

### 功能特性

#### 系统功能
- **启动加载器** - 从软盘启动并加载内核
- **内核** - 16位实模式内核，提供基本系统服务
- **命令行界面** - 交互式shell，支持多种命令
- **实时时钟** - 显示系统时间
- **系统信息** - 显示操作系统版本和架构信息

#### 可用命令
- `help` - 显示帮助信息和可用命令列表
- `clear` - 清屏
- `info` - 显示系统信息
- `time` - 显示当前时间
- `reboot` - 重启系统

#### 用户界面
- 启动时显示 "MayOS ver0.1 - Booting..." 消息
- 交互式命令提示符 "MayOS> "
- 支持键盘输入和退格删除
- 实时响应用户命令

### 技术规格

- **架构**: x86 (16位实模式)
- **启动方式**: 软盘启动 (1.44MB)
- **内存需求**: 最小16MB
- **文件系统**: 无 (直接从软盘扇区加载)

### 文件结构

- `boot.asm` - 启动扇区代码 (512字节)
- `kernel.asm` - 内核代码 (2048字节)
- `build.py` - 构建脚本
- `run.bat` - 运行脚本
- `start.bat` - 构建并运行脚本
- `mayos.img` - 生成的磁盘镜像

### 构建要求

- NASM汇编器
- Python 3.x
- QEMU模拟器 (可选，用于运行)

### 构建步骤

```bash
# 构建操作系统
python build.py

# 或使用批处理脚本 (构建并运行)
start.bat
```

### 运行方法

#### 直接使用QEMU
```bash
qemu-system-i386.exe -fda mayos.img -m 16
```

#### 使用批处理脚本
```bash
run.bat
```

### 使用说明

1. 启动后会看到 "MayOS ver0.1 - Booting..." 消息
2. 内核加载完成后显示 "MayOS ver0.1 - Kernel Ready"
3. 系统初始化后显示命令提示符 "MayOS> "
4. 输入命令并按回车执行
5. 使用 `help` 命令查看所有可用命令

### 示例会话

```
MayOS ver0.1 - Booting...
MayOS ver0.1 - Kernel Ready
System Initialized

MayOS> help
Available Commands:
  help   - Show this help
  clear  - Clear screen
  info   - System information
  time   - Show current time
  reboot - Restart system

MayOS> info
MayOS ver0.1
Simple Operating System
Architecture: x86 16-bit
Memory: Real Mode

MayOS> time
Current Time: 14:30:25

MayOS> reboot
Rebooting system...
```

### 扩展可能

这个基础操作系统可以进一步扩展：
- 32位保护模式支持
- 内存管理
- 多任务处理
- 文件系统
- 设备驱动程序
- 网络支持

