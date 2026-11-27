# MayOS 0.5.4-pre32 · 下载并安装 MayKernel

- Linux64 更新页可下载并校验 MayKernel64 更新，随后写入已指定安装盘的备用启动槽。下载和安装分别显示进度。
- 在 Linux64 的 GRUB 启动项按 E，在 linux 行末尾加入 `mayos.update=/dev/sda`（替换为实际的 MayOS 安装盘），按 Ctrl+X 启动，再打开「设置 → 系统更新」。
- 更新完成后从硬盘重启，可试用一次新版。继续使用原有离线安装器确认新版或放弃试用。
