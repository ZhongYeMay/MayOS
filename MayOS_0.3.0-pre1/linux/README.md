# Linux 图形桌面与救援环境

`kernel.config` 是为 QEMU x86_64 PC / BIOS 与 x64 UEFI 准备的内核配置片段。
`scripts/linux.py` 使用官方稳定内核源码，将片段交给 Kconfig 解析，编译
`bzImage`，并用静态 x86_64 BusyBox 制作 gzip / newc initramfs。

Linux 启动后在 tty1 运行真实的 `mayos-desktop` 用户态图形进程，并在
COM1 / ttyS0 保留独立 BusyBox shell。桌面复用 MayOS 的窗口、文件管理器、
编辑器、终端与设置前端，通过 Linux fbdev 显示、evdev 键鼠、时钟与系统信息
系统调用运行；不会执行 ATA 端口 I/O 或 `hlt` 等特权指令。

桌面文件保存在该进程的 RAM 对象中，桌面退出即丢失；与 Linux shell 中的
临时根文件系统是两个独立空间。系统不自动挂载、格式化或写入 MayFS 数据盘。
Linux 根目录内的临时文件在关机后丢失。尚无桌面文件持久化、包管理器、
完整发行版服务或应用沙箱。

启动菜单的 Linux 文本救援项使用 `mayos.desktop=0`，tty1 改为 BusyBox shell。
图形进程如果无法打开 framebuffer 或输入设备，会记录明确错误并回退到
tty1 shell。日志在 `/run/mayos-desktop.log`，进程号在 `/run/mayos-desktop.pid`。
可用 `/proc/$(cat /run/mayos-desktop.pid)/comm` 和 `uname -r` 验证真实进程与内核。

## 依赖与构建

Debian / Ubuntu 示例依赖：

```sh
sudo apt-get install build-essential flex bison bc libelf-dev libssl-dev \
  xz-utils busybox-static musl-tools rsync python3 python3-pil \
  fonts-noto-cjk fonts-dejavu-core
python3 scripts/linux.py build --version latest --jobs 4
python3 scripts/linux.py status
```

也可指定版本和静态 BusyBox：

```sh
python3 scripts/linux.py build --version 7.3.1 --jobs 4 \
  --busybox /path/to/busybox
```

`--desktop-cc /path/to/musl-gcc` 或环境变量 `MAYOS_LINUX_CC` 可指定桌面编译器。
脚本从实际 Linux 源码导出 UAPI 头文件，并独立编译静态 ELF64 桌面二进制。
共享 GUI、文件系统、字体/照片生成器与 Linux 后端均被纳入输入哈希，修改后
会重新打包 Linux 桌面，避免 ISO 内携带旧界面。

`latest` 每次显式构建都会查询 kernel.org 的 `latest_stable`，不会选择 RC。
固定版本已有源码和校验文件时可以离线重建；`--offline` 禁止网络，
`--offline --version latest` 使用上一次已记录的版本。
输入及输出哈希未变化时复用编译产物。`--rebuild` 强制重新配置和构建。
下载使用 TLS，并核对 kernel.org 发布的 SHA-256；当前脚本没有独立验证
PGP 签名，不应将 SHA-256 校验描述为 PGP 验签。

## 产物与许可

`build/linux/manifest.json` 记录内核版本、源码地址和 SHA-256、配置、
BusyBox 版本及二进制哈希、各产物 SHA-256。`status` 检查这些哈希。
完整实际配置保存在 `build/linux/kernel.config`。
桌面产物为 `build/linux/mayos-desktop`，同时位于 initramfs 的 `/bin/mayos-desktop`。
开源组件的许可文本位于运行环境 `/usr/share/licenses/`。
为便于重现，内核嵌入的构建用户、主机和时间固定为 `mayos`、`builder`
与 Unix epoch（1970-01-01）；实际构建完成时间记录在 manifest 的 `built_at`。
`uname` 显示的这个固定构建时间不代表 Linux 源码发布年份。

Linux 是 GPL-2.0-only（含 Linux-syscall-note 例外）的独立第三方组件；
BusyBox 按 GPL v2 分发。分发含这些二进制的 ISO / 压缩包时，必须同时提供
对应完整源码（含发行版补丁，如适用）、实际配置、本文目录与构建脚本，
并保留许可文本。脚本保留官方 Linux 源码归档于
`build/linux/downloads/`，并复制内核许可到 `build/linux/licenses/`。
如果使用发行版 BusyBox，请一并提供该二进制对应的发行版源码包；
仅提供上游 URL 或另一版本源码不足以替代对应源码。

本配置面向验证过的 QEMU 虚拟硬件；要求至少 1024×768 的 RGB888 / 32 位
framebuffer。BIOS 与 OVMF x64 UEFI 需要分别通过启动验证，UEFI 不等于
Secure Boot 支持。不能据此承诺实体机、任意 GPU、无线网络或候选主线版本可用。
