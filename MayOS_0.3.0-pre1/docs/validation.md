# MayOS 0.3.0-pre1 实际验证记录

验证日期：2026-11-01。构建环境为 Ubuntu 24.04 x86_64、GCC 13、GRUB 2.12；实际运行使用 QEMU 8.2.2 TCG、PC、512 MiB、标准 VGA、PS/2、IDE。UEFI 固件为 OVMF x64 / Ubuntu 2024.02-2ubuntu0.10，未启用 Secure Boot。

## 最终镜像身份

BIOS 双启动、UEFI 全套测试、两份录屏以及新版展示截图对应同一份最终 `build/mayos-dual.iso`，SHA-256：

```text
d19ee6ed95c153bded249dd82d7ba2c07c4d0626197cb4a7edb008dabd70fe65
```

所有测试均通过真实 GRUB 菜单进入系统，没有使用 QEMU `-kernel` 绕过引导。独立自研 `mayos.iso` 已重新组装，内核 ELF 与最终双启动镜像完全一致，BIOS / EFI 引导目录通过组装检查；完整启动矩阵以双启动镜像为证据。

## 实测矩阵

| 项目 | BIOS | OVMF x64 UEFI |
| --- | --- | --- |
| GRUB 默认进入自研桌面 | 通过 | 通过 |
| 自研 framebuffer / PS/2 / 页分配测试 | 通过 | 通过 |
| MayFS 写入与关机后冷启动读取 | 通过 | 通过 |
| Linux 实际 uname release 7.3.1 | 通过 | 通过 |
| 真实 mayos-desktop 进程与 GUI 键盘命令 | 通过 | 通过 |
| Linux 图形文件写入、读取 | 通过 | 通过 |
| Linux 鼠标创建文件、编辑器大写输入、Ctrl+S、cat 复核 | 通过 | 由共享输入实现，UEFI 录像覆盖鼠标导航 |
| Linux EFI 运行环境、1024×768×32 framebuffer | 不适用 | 通过 |
| Linux init、proc/sys/dev、串口 shell 与关机 | 通过 | 通过 |
| Linux 串口 shell 退出后重新生成进程 | 通过 | 未单独重复该项 |
| 文本救援启动项、显示控制台键盘与关机 | 未单独重复该项 | 通过 |
| Linux 前后整块 MayFS 数据盘 SHA-256 一致 | 通过 | 通过 |

BIOS 报告：`build/dual-boot-test-results.json`、`build/dual-boot-regression.log` 与 `test-dual-*.log`。UEFI 报告：`build/uefi-test-results.json`、`build/uefi-full-test.log` 与 `test-uefi-*.log`。

BIOS MayFS 测试盘前后哈希均为 `bfc0109f155bd52dbd9f95197c7aee845fb3dce83009d070bd9d3a191d73140c`；UEFI 测试盘前后哈希均为 `380ef5d63e766caa6993f4a1ca429ad287bae2d411b4a97dad684873519886cd`。两套测试使用不同专用盘。交付的 `build/mayos-disk.img` 与上传 MayOS 0.1 包中的原文件逐字节比较一致。

## 桌面与原功能回归

共享 GUI 源码对应的 `smoke.py`、`ui_smoke.py`、`easter_smoke.py` 全部通过，包括：

- 启动、键鼠、终端、页分配 / 写入 / 释放、GUI 编辑器保存、ATA 持久化与冷启动。
- 无盘 RAM 回退、未知格式磁盘逐字节保护。
- 浅色 / 深色、复制、重命名、回收站恢复、新建文件夹、窗口拖动 / 最大化 / 最小化与应用搜索。
- 猫咪彩蛋的命令和五连击入口、真实图片像素、模态输入、退出与终端恢复。

`build/gui-03-validation.json` 记录冻结 GUI 的源码哈希和回归索引。最终双启动测试又验证了最终内核内存映射和 seed 更新后的持久化，以及修复后的 Linux 输入、鼠标编辑路径。About 和开源组件截图均已检查，两句指定英文完整可见。截图来源记录为 `build/release03-screenshot-provenance.json`。

## 实测发现并修复的问题

1. OVMF 将可用 RAM 分成多个区间，旧的 `mem_upper` 统计只显示首段 8 MiB。自研内核现解析 Multiboot 内存映射，512 MiB 虚拟机显示 505 MiB 可用 RAM，并将固件保留页排除出分配器。分配器本身仍只管理前 128 MiB。
2. Linux 启动时 PS/2 鼠标晚于键盘注册，最初桌面立即退回 shell。现采用最多 5 秒的有界设备发现，保留已发现设备；修复后两种固件的桌面测试通过。
3. EFI stub 原先恢复 OVMF 的 1280×800 模式。添加实际启动参数 `video=efifb:1024x768-32` 后，客体 framebuffer 报告 1024×768×32，并通过 GUI 操作验证。

## 真实启动录屏

| 视频 | 时长 | 格式 | 内容 |
| --- | --- | --- | --- |
| MayOS-0.3.0-pre1-UEFI-MayKernel.mp4 | 40.3 秒 | H.264、1024×768、10 fps、无音轨 | OVMF → GRUB → 自研桌面 → 关于 / 开源组件 → neofetch → 文件写入读回 |
| MayOS-0.3.0-pre1-UEFI-Linux.mp4 | 42.6 秒 | H.264、1024×768、10 fps、无音轨 | OVMF → GRUB 选择 Linux → 真实 Linux 桌面 → 关于 / 开源组件 → neofetch → RAM 文件写入读回 |

`scripts/record_boot.py` 从虚拟 CPU 启动前开始，以 QMP screendump 连续采集实际 framebuffer，经 FFmpeg 编码；不同固件分辨率按比例放入固定画布。时间轴按真实采集间隔维持；没有合成系统界面，也没有用历史版本帧替换。本次录屏各使用独立空白数据盘和独立 OVMF VARS 副本。

录屏元数据包含 ISO / 视频 SHA-256、固件 SHA-256、UTC 时间、帧数、QEMU 版本与操作列表，位于发布包的 `build/recordings/`。串口证据为 `build/record-uefi-custom.log` 和 `build/record-uefi-linux.log`。视频单独提供，官网可直接播放。

## 可复核范围

以上结果仅覆盖列出的 QEMU 配置。没有验证 Secure Boot、ARM、实体安装、任意 GPU、长时间稳定性或写入中途断电恢复。Linux 桌面为实际用户态进程，但文件只在桌面进程内存中；Linux shell rootfs 是独立临时空间。尚无 MayFS Linux 驱动、完整发行版包管理器或独立应用沙箱。

MayOS Team 2023 - 2026.

This version of MayOS is for test use only.
