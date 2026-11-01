# MayOS 0.3 第三方组件、工具与对应源码

适用于 **MayOS 0.3.0-pre1**。MayOS 自研代码沿用已有授权，本次没有重新授权。关于页显示团队与测试声明，“开源组件”页明确以下依赖；完整许可文本随发布包提供。

MayOS Team 2023 - 2026.

This version of MayOS is for test use only.

| 组件 | 精确版本 / 角色 | 许可与来源 |
| --- | --- | --- |
| Linux | 7.3.1，独立 x86_64 内核 | GPL-2.0-only，相关接口含 Linux-syscall-note；官方完整源码归档 |
| GNU GRUB | Ubuntu 2.12-1ubuntu7.3，BIOS i386-pc / UEFI x86_64-efi | GPL-3.0-or-later；对应 Ubuntu 源码与补丁 |
| BusyBox | 1.36.1，Ubuntu 源码包 1:1.36.1-6ubuntu3.1 | GPL-2.0-only；init、shell、命令行工具，静态 musl 构建 |
| musl | Ubuntu 1.2.4-2 | MIT 及源码 COPYRIGHT 完整通知；链接进 BusyBox 和 Linux 桌面 |
| Noto CJK | 构建时生成中文字体子集 | SIL OFL-1.1；见 Noto-CJK-copyright.txt |
| DejaVu | 构建时生成 ASCII 字体子集 | DejaVu / Bitstream Vera 字体许可；见 DejaVu-copyright.txt |
| QEMU | 8.2.2 / Ubuntu 1:8.2.2+ds-0ubuntu1.18 | GPL-2.0 等；宿主模拟与录屏工具，不在启动 ISO 内 |
| OVMF / EDK II | Ubuntu 2024.02-2ubuntu0.10 | BSD-2-Clause-Patent 等上游组件许可；宿主 UEFI 测试固件，不在启动 ISO 内 |

GCC、GNU binutils、GNU make、xorriso、mtools / dosfstools、Python / Pillow、FFmpeg、flex / bison、bc、rsync 等是外部构建或测试工具。它们没有被作为运行程序嵌入 ISO；生成的字体、引导程序、Linux / BusyBox 与静态链接库按各自许可随产物声明。项目没有宣称这些工具由 MayOS Team 开发。

## 配套完整源码

`MayOS-0.3.0-pre1-third-party-sources.zip` 提供：

- `linux/linux-7.3.1.tar.xz`：本次官方 Linux 完整源码；`sha256sums-v7.asc` 为官方校验清单。
- `linux/kernel.config` 与 `linux/manifest.json`：实际 Kconfig 结果、版本与构建输入输出记录。
- `components/sources/`：BusyBox、musl、GRUB 的 `.dsc`、orig 与 debian 归档；对应发行版补丁均保留。
- `components/busybox.config`、`rebuild-busybox.sh`、`PROVENANCE.md`、`provenance.json`、`SHA256SUMS`：实际配置、构建方式及校验。
- `mayos-build/`：本版 MayOS 共享桌面源码、Linux 后端、资源与构建脚本，便于核对静态桌面的编译输入。

主发布包也包含 MayOS 全部项目源码、完整构建脚本与已经编译的 Linux 桌面。官网源码页支持浏览器校验分段后保存完整 ZIP，也可下载所有分段、清单和 Python 合并脚本在本机重建；完整 ZIP 的哈希在发布校验文件中。

## 许可位置

Linux `COPYING`、GPL-2.0 与 Linux-syscall-note 在 `build/linux/licenses/`；BusyBox、GRUB、musl 完整许可及发行版版权文件在 `docs/third-party/dual-boot-licenses/`；字体通知在本目录。Linux 客体 `/usr/share/licenses/` 也保留运行时组件许可。精确 BusyBox 哈希与 GRUB 来源见 `dual-boot-provenance.json`，EFI loader 与 ISO 哈希见引导 manifest。

再次发布包含第三方二进制的 ISO 或压缩包时，应同时提供本版匹配的完整源码、补丁、实际配置、构建脚本及许可。更换第三方组件后需同步更新配套源码，不能用另一个版本的源码替代。

## 离线重建

将配套源码中的 `linux/linux-7.3.1.tar.xz` 与 `sha256sums-v7.asc` 放入主项目 `build/linux/downloads/`，安装 README 中依赖并执行：

```sh
python3 scripts/linux.py build --version 7.3.1 --offline --rebuild --jobs 4
make dual LINUX_VERSION=7.3.1
make test-dual
make test-uefi
```

`linux/kernel.config` 是项目片段，`build/linux/kernel.config` 是本次生成的完整配置。源码下载检查 HTTPS 清单中的 SHA-256，没有独立 PGP 验签。BusyBox 的精确 musl 静态重建步骤见 `components/rebuild-busybox.sh`；GRUB 与 musl 对应发行版规则位于各自源码中。更换宿主工具链可能改变二进制哈希，不应声称任意宿主都会字节一致。

## 上游资料

- Linux 稳定版元数据：<https://www.kernel.org/releases.json>
- Linux 官方归档：<https://cdn.kernel.org/pub/linux/kernel/v7.x/>
- Linux x86 启动协议：<https://docs.kernel.org/arch/x86/boot.html>
- Linux EFI stub：<https://docs.kernel.org/admin-guide/efi-stub.html>
- GNU GRUB：<https://www.gnu.org/software/grub/>
- BusyBox：<https://busybox.net/>
- musl：<https://musl.libc.org/>
- Ubuntu 对应源码：<https://archive.ubuntu.com/ubuntu/pool/>
- QEMU：<https://www.qemu.org/>
- TianoCore / EDK II：<https://github.com/tianocore/edk2>
