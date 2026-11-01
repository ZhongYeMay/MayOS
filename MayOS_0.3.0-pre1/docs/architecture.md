# MayOS 0.3 双内核与 UEFI 架构

## 固件与引导

同一 `build/mayos-dual.iso` 包含 BIOS GRUB `i386-pc` 和 UEFI GRUB `x86_64-efi`。BIOS 使用 El Torito BIOS 引导项；UEFI 使用嵌入 FAT EFI 系统镜像中的 `EFI/BOOT/BOOTX64.EFI`。该 EFI 可执行文件由 `grub-mkstandalone` 生成，加载相同的 GRUB 配置。ISO 包含 BIOS 与 UEFI 两种 El Torito 记录以及 GPT 引导布局；实际检测报告保存在 `.iso.boot.json`。

菜单 5 秒后默认进入自研桌面，也可选择 Linux 图形桌面或 Linux 救援终端。GRUB 一次选择一个内核；两个内核不会同时运行。自研内核使用 Multiboot v1 ELF32；Linux 使用 x86_64 bzImage 和 gzip/newc initramfs。UEFI x64 引导器将控制权交给自研内核时，后者仍以 32 位保护模式运行。本版 EFI 程序未签名，没有实现 Secure Boot 信任链。

`build/mayos.iso` 使用相同混合引导构建流程，仅提供自研内核。`BOOT_FIRMWARE=bios` 可显式只生成 BIOS 输出，默认 `hybrid`。

## 自研内核路径

1. GRUB 提供 Multiboot v1 内存映射与 1024×768×32 RGB framebuffer。
2. `boot.S` 建立 GDT 与 64 KiB 栈，进入 C 初始化。
3. 物理页分配器读取内存映射的可用区，优先保留固件保留区、低端内存与内核区域。EFI 的 `mem_upper` 仅描述首段连续内存，不能用于统计全部可用内存；本版修复此问题。
4. 初始化串口、8042 PS/2、8259 PIC、100 Hz PIT 与定时中断。
5. MayFS 尝试挂载主 IDE master，未知格式保持原样并使用 RAM 回退。
6. 共享 GUI 的协作事件循环读取输入、操作文件、按需绘制，空闲时由 PIT 唤醒。

GUI 应用共享内核静态对象和地址空间。页分配器的 4 KiB 页位图最多管理前 128 MiB，统计显示可覆盖更大可用内存；未实现用户态地址空间、分页、SMP 或通用异常处理。

## Linux 用户态路径

`/init` 挂载 `/proc`、`/sys`、`/dev`、`/tmp` 与 `/run`，交给 BusyBox init 管理 tty1 与串口 ttyS0。默认 tty1 运行 `desktop-session`，启动真正静态链接的 ELF64 `/bin/mayos-desktop`；串口保留独立 shell。救援项设置 `mayos.desktop=0`，tty1 使用文本 shell。

`linux/desktop.c` 提供平台后端：fbdev mmap、evdev 键盘鼠标、单调时钟、系统内存、uname、内存页模拟分配与重启/关机。它和共享 `kernel/gui.c`、RAM 模式的 `kernel/fs.c` 编译成一个 Linux 用户进程；不执行 ATA I/O、`hlt` 或内核 PS/2 端口操作。桌面无多进程应用隔离。

Linux 输入设备可能分批出现，启动采用最多 5 秒的有界发现，保留已找到的键盘或鼠标，等候另一设备。无法初始化显示或输入时输出具体错误并回退到 shell。桌面日志为 `/run/mayos-desktop.log`，PID 为 `/run/mayos-desktop.pid`。

Linux 图形文件模型位于桌面进程 RAM，对象生命周期等于该进程；Linux shell 使用独立临时根文件系统，二者不共享文件。Linux 默认没有 MayFS 驱动或挂载动作。测试在图形运行、救援终端和关机前后对整块 MayFS 盘比较 SHA-256。

内核配置为 QEMU PC / BIOS / OVMF x64 UEFI，编入串口、PS/2、evdev、fbdev、EFI framebuffer 等所需驱动。UEFI 使用 `video=efifb:1024x768-32` 请求准确模式。配置存在的其他驱动不代表已经验证所有硬件。

## 显示与界面

统一画布为 1024×768，内部颜色为 RGB888。自研内核按 Multiboot pitch 与颜色位布局写 framebuffer；Linux 后端按 fbdev pitch 与通道布局复制到 mmap 区域。字体由 Noto CJK / DejaVu 在构建时生成子集；照片资源由原 JPEG 转换成 RGB565。

默认风格为深石墨色桌面、青绿强调色、窄顶栏、左侧应用启动区与底部任务区。设置含“关于”“外观”“存储”“开源组件”，关于页与 `neofetch` 使用平台钩子显示当前真实内核。版本和两句测试声明在两个后端共享。

## 构建与可核对性

`scripts/linux.py` 从 kernel.org 解析最新稳定版，固定源码 SHA-256，编译后核对实际 release。源码验证基于官方 HTTPS SHA-256 清单，没有独立 PGP 验签。共享 GUI、RAM 文件代码、字体、照片生成器与 Linux 后端都纳入输入指纹，产物包括 bzImage、initramfs、静态桌面、BusyBox 与实际配置。

`scripts/dual_iso.py` 检查缓存指纹、文件哈希、x86 启动头、64 位标志和内核版本，使用独立暂存目录创建镜像，成功后替换最终文件。`.iso.boot.json` 记录平台、GRUB 版本、EFI loader 哈希、ISO 哈希与 El Torito 检测输出。BIOS / UEFI 验证报告与真实录屏也记录同一 ISO 哈希。

## MayFS 磁盘布局

扇区大小 512 字节。驱动只读写前 512 个扇区（256 KiB），其余虚拟磁盘空间尚不使用。无分区表，与 FAT/ext 系列不兼容。

| 字节位置 | 内容 |
| --- | --- |
| 0..7 | ASCII `MAYFS001` |
| 8..11 | 文件数组的 FNV-1a 32 位校验，小端 |
| 12..511 | 保留，全零 |
| 512..200191 | 48 条 `File`，每条 4160 字节 |
| 200192..262143 | 保留，全零 |

每条 `File`：

| 相对偏移 | 类型 | 内容 |
| --- | --- | --- |
| 0 | u32 | used，0/1 |
| 4 | u32 | dir，0/1 |
| 8 | u32 | 父目录索引 |
| 12 | u32 | 文件字节数，不含结尾 NUL |
| 16 | char[48] | NUL 结尾文件名 |
| 64 | u8[4096] | NUL 结尾文本内容 |

索引 0 是根目录，1 是 Documents，2 是 Desktop，3 是 Trash。这四项受保护；普通文件和子目录从索引 4 起分配。可建立嵌套目录；移动前检查祖先链，避免目录循环。复制仅支持文本文件，复制目录会显示说明。

保存过程先写零头部，再写数据区，最后提交带校验值的头部，并发送 ATA FLUSH CACHE。挂载时检查标识、校验和、字段范围、系统目录和目录循环。无效磁盘原样保留，启动内存文件系统。该顺序可发现中断写入，但不能保证恢复旧数据或原子持久性。


## 验证边界

本版实际验证 QEMU 8.2.2 TCG、x86 PC、512 MiB、标准 VGA、PS/2、IDE，分别覆盖 BIOS 与 OVMF x64 UEFI。尚未验证实体机器、Secure Boot、ARM、任意 GPU 或长时间稳定性。自研文件模型可持久化，但没有日志或断电恢复；Linux 桌面文件仅在进程内。

## 照片彩蛋

原照片保存在 `assets/easter-cat.jpg`，`photo.py` 等比例限制到 360×480 并生成 RGB565 常量。弹层位于窗口与菜单之上，独占输入，关闭后恢复焦点。`meow` 和关于页图标五连击共用入口；像素与模态行为有实际 QEMU 回归验证。
