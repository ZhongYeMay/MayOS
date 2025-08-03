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
