# MayOS Linux64 硬盘版

硬盘版使用独立的 x86_64 UEFI GPT 布局。Linux 直接挂载 ext4 系统分区作为根目录。BusyBox Live 环境仍可用于试用。硬盘版的 MayOS 桌面在 Xorg 上运行，Firefox ESR 可按需安装，安装后作为独立应用出现在启动台和 Dock。Firefox 与桌面都以普通用户运行。新构建的基础镜像不预装 Firefox。

## 创建和安装

在 Debian 构建机安装 `mmdebstrap`、`libx11-dev`、`grub-efi-amd64-bin`、`mtools`、`dosfstools`、`fdisk`、`e2fsprogs` 和常规 MayOS 编译依赖。请从完整源码目录运行创建命令，以生成与 Live ISO 共用的 MayOS 引导主题。使用独立构建虚拟机运行以下命令。

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

## 安装 Firefox

从已安装的 MayOS Linux64 登录虚拟终端或串口，下载 GitHub 上的安装脚本。

```sh
curl -fLo install-firefox https://raw.githubusercontent.com/ZhongYeMay/MayOS/feat/linux64-system-firefox/versions/0.5.4-beta.4-dev/linux/installed/install-firefox
sudo sh ./install-firefox
```

脚本通过 APT 安装 Debian 签名仓库中的 Firefox ESR，临时下载使用独立的用户数据分区，安装完成后自动清理。安装成功后可从启动台或 Dock 打开 Firefox，右键点击 Dock 图标可恢复、最小化或关闭窗口。每个系统槽的应用包各自独立，切换到未安装 Firefox 的槽后可以重新运行脚本。浏览器配置继续保存在共用的 `/home` 中。

## 原地升级

```sh
sudo mayos-linux-update status
sudo mayos-linux-update stage --bundle /path/to/new-root.img.gz --sha256 <release-sha256>
sudo reboot
sudo mayos-linux-update confirm
```

在试启动的系统内确认才会切换默认槽。不确认时，下一次启动回到旧槽。若要清除未确认的试用状态，从旧槽启动并运行 `sudo mayos-linux-update discard`。升级写入并校验备用系统分区，用户数据位于独立的 `/home` 分区。暂存要求可信发布页提供的 SHA-256。

## 构建离线安装 ISO

先用上面的打包步骤生成未压缩的 `root.img`。安装构建依赖
`grub-efi-amd64-bin`、`grub-common`、`xorriso`、`mtools`、
`squashfs-tools`、`e2fsprogs` 及 GRUB 主题所需字体，然后运行：

```sh
python3 scripts/linux_installed_iso.py --root-image /path/to/root.img \
  --busybox /path/to/static-x86_64-busybox \
  --output MayOS-Linux64-No-Firefox.iso
```

ISO 面向 x86_64 UEFI（关闭 Secure Boot），内含离线安装程序和 Linux64
系统。安装程序只接受完全空白的整块目标磁盘，要求 512 字节逻辑扇区和至少
4.1 GiB 容量。QEMU 可使用 OVMF，从光驱启动 ISO 并挂载另一块空白虚拟硬盘。
输入磁盘路径，再输入安装确认和自设的本地管理员密码。安装完成后移除 ISO，
从目标硬盘启动。浏览器可在启动后按需安装。
