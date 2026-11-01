# MayOS

> 实验性桌面操作系统 · MayKernel x86 / Linux x86_64 · BIOS / UEFI x64

MayOS 是一个不断迭代的系统开发项目。最新版源码提供自研 x86 32 位 MayKernel 桌面、Linux x86_64 用户态图形桌面、BusyBox 救援终端，以及共享的文件管理器、设置、终端与编辑器。

## 代码和网站

- [当前项目源码与构建说明](MayOS_0.3.0-pre1/README.md)
- [GitHub 当前源码 ZIP](https://github.com/ZhongYeMay/MayOS/archive/refs/heads/main.zip)
- [新版网站源码与部署说明](website/README.md)
- [GitHub Pages 网页版本（以部署成功为准）](https://zhongyemay.github.io/MayOS/)
- [原 MayOS 站点](https://mayos-official.business-tea-9633.chatgpt.site/)
- [版本说明](MayOS_0.3.0-pre1/RELEASE_NOTES.md)
- [测试记录](MayOS_0.3.0-pre1/docs/validation.md)
- [许可与第三方组件](MayOS_0.3.0-pre1/docs/third-party/dual-boot.md)
- [GitHub Releases](https://github.com/ZhongYeMay/MayOS/releases)

## 2026-11-01 开发快照（未打包成新的正式镜像）

- 采用 Ubuntu 风格的深紫灰窗口、橙色强调色及扁平化界面，降低卡通感。
- Linux 会话调用真实 Fastfetch ELF 读取系统信息；自研 MayKernel 不声称能运行 Linux 可执行文件。
- Linux fbdev 为常见 XRGB8888 布局增加快速逐行拷贝，在其他像素布局下保留转换路径。
- 新增独立官网源码、静态站点检查和 GitHub Pages 部署尝试。

**请区分源码和发布镜像：**上述改动已经进入仓库 `main`，但尚未生成、实测并发布新的匹配 ISO / 视频 / 校验清单。此前 `0.3.0-pre1` 发布包是旧构建，不能据此断言它含这些修改。GitHub Releases 中的旧版本与自动上传失败记录亦须单独核对。

MayOS 的 Linux 桌面文件目前存储在进程 RAM 中，关机后不会保留；自研内核运行在 32 位环境。UEFI x64 是固件/引导架构，不等于 64 位自研内核或 Secure Boot 支持。

仓库早期 16 位 MayOS 0.1 / 0.2 的源文件已从最新工作树删除，但 Git 历史和历史 Release 尚未改写；这不影响当前源码。

MayOS Team 2023 - 2026.  
This version of MayOS is for test use only.
