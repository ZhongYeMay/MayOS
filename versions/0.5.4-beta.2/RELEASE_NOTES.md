# MayOS 0.5.4 Beta 2

基于 MayOS 0.5.4-pre32 的 x86_64 测试版。

- 双内核 Live ISO 可启动 Linux64（Linux 7.3.1）和 MayKernel64，并提供 Linux64 救援入口。
- Linux64 的「设置 → 系统更新」可下载并校验 MayKernel64 更新，显示下载和安装进度，将更新写入指定 UEFI 安装盘的备用槽。
- 已写入的新版可在从 MayOS 硬盘启动时试用一次。可通过独立安装 ISO 确认或放弃试用。未确认时，下次启动会回到原内核。
- 在 Linux64 的 GRUB 启动项按 E，在 linux 行末尾加入 `mayos.update=/dev/sda`（替换为实际的 MayOS 安装盘），按 Ctrl+X 启动后再打开系统更新。
