# MayOS Linux64 硬盘版

硬盘版使用独立的 x86_64 UEFI GPT 布局。Linux 直接挂载 ext4 系统分区作为根目录。BusyBox Live 环境仍可用于试用。硬盘版的 MayOS 桌面在 Xorg 上运行，启动台的浏览器入口在该环境启动 Debian 的 Firefox ESR。Firefox 与桌面都以普通用户运行。

## 创建和安装

在 Debian 构建机安装 `mmdebstrap`、`libx11-dev`、`grub-efi-amd64-bin`、`mtools`、`dosfstools`、`fdisk`、`e2fsprogs` 和常规 MayOS 编译依赖。使用独立构建虚拟机运行以下命令。

```sh
make linux-installed-desktop
sudo linux/installed/bootstrap-root /path/to/new-root
sudo linux/installed/package-root /path/to/new-root "$(cat VERSION)" \
  build/linux/mayos-desktop-installed /path/to/root.img
gzip -1 /path/to/root.img
sha256sum /path/to/root.img.gz
python3 linux/installed/disk.py create --bundle /path/to/root.img.gz \
  --sha256 <root-bundle-sha256> --output MayOS-Linux64.raw
```

创建时设置 `mayos` 的本地管理密码。QEMU 可用 OVMF 直接启动生成的原始磁盘文件，从串口或 Ctrl+Alt+F2 的虚拟终端登录可执行管理命令。实体机可在另一个 Linux 环境下，使用 `disk.py deploy --image MayOS-Linux64.raw --sha256 <raw-image-sha256> --target /dev/设备 --confirm /dev/设备` 写入新盘。命令要求选择完全空白的整盘、512 字节逻辑扇区和足够的空间。启动方式为 x86_64 UEFI，测试使用关闭 Secure Boot 的 OVMF。硬盘版安装布局需要一块单独的盘。

## 原地升级

```sh
sudo mayos-linux-update status
sudo mayos-linux-update stage --bundle /path/to/new-root.img.gz --sha256 <release-sha256>
sudo reboot
sudo mayos-linux-update confirm
```

在试启动的系统内确认才会切换默认槽。不确认时，下一次启动回到旧槽。若要清除未确认的试用状态，从旧槽启动并运行 `sudo mayos-linux-update discard`。升级写入并校验备用系统分区，用户数据位于独立的 `/home` 分区。暂存要求可信发布页提供的 SHA-256。
