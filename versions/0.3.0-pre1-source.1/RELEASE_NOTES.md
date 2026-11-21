# MayOS 0.3.0-pre1 · BIOS / UEFI 双内核桌面

**预发布测试版。** 同一张 ISO 可选择 MayOS 自研内核、Linux 7.3.1 图形桌面或 Linux 文本救援环境。

MayOS Team 2023 - 2026.

This version of MayOS is for test use only.

## 本次更新

- 增加 BIOS / x64 UEFI 混合引导，保留原有自研内核与 MayFS 文件持久化。
- 将共享桌面移植为真实 Linux 用户态进程 `mayos-desktop`，使用 fbdev 显示、evdev 键鼠和静态 musl；串口保留独立 BusyBox shell。
- 更新工作站风格界面、“关于系统”和开源组件页面，展示实际内核、团队声明与测试用途声明。
- 提供匹配的第三方完整源码、发行版补丁、构建配置、许可文本、SHA-256 校验值，以及两段真实 QEMU UEFI 启动录像。

## 实际验证

BIOS 和 OVMF x64 UEFI 均验证两种内核与桌面启动、真实 Linux 进程及版本、MayFS 冷启动读取与正常关机。图形回归还覆盖键鼠输入、文件创建与读写、编辑器 Ctrl+S；Linux 测试覆盖串口 shell 退出后的自动重启。Linux 运行前后的 MayFS 测试盘哈希保持一致。

两段录像分别展示 UEFI 启动自研桌面和 Linux 桌面，包括“关于”、开源组件、系统信息与文件操作。测试报告和录屏元数据均绑定到本次发布 ISO：

```text
d19ee6ed95c153bded249dd82d7ba2c07c4d0626197cb4a7edb008dabd70fe65
```

## 下载内容

| 文件 | 内容 |
| --- | --- |
| `MayOS-0.3.0-pre1.zip` | 项目源码、启动 ISO、数据盘、编译产物、许可与验证记录 |
| `MayOS-0.3.0-pre1-third-party-sources.zip` | Linux、BusyBox、musl、GRUB 对应完整源码与补丁，以及共享桌面源码、配置和构建脚本 |
| `MayOS-0.3.0-pre1-UEFI-MayKernel.mp4` | 自研内核的实际 UEFI 启动与桌面操作录像 |
| `MayOS-0.3.0-pre1-UEFI-Linux.mp4` | Linux 7.3.1 图形桌面的实际 UEFI 启动与操作录像 |
| `MayOS-0.3.0-pre1-SHA256SUMS.txt` | 发布包、源码包、录像与双启动 ISO 的 SHA-256 |

运行和重建方法见项目 `README.md`；组件来源与原有许可见 `docs/third-party/`。发布包与对应源码包应配套保留。

## 支持范围

- 本次验证环境为 QEMU PC / TCG、512 MiB 内存、标准 VGA、PS/2、IDE，以及关闭 Secure Boot 的 OVMF x64 UEFI；尚未验证实体硬件安装。
- **自研内核仍为 32 位。** UEFI x64 指 64 位 EFI 引导程序，不表示 MayKernel 已改为 64 位内核。
- **Linux 桌面文件仅保存在该进程的 RAM 中，退出桌面即丢失。** BusyBox shell 的临时根文件系统在关机后清空；两者是独立文件空间。Linux 默认不挂载或写入 MayFS 数据盘。
- 当前是开发原型，未提供完整发行版包管理器、独立应用沙箱或通用实体硬件支持。
