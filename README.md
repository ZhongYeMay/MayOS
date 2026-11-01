# MayOS

## 当前测试版：0.3.0-pre1

MayOS 是实验性操作系统项目。当前源码提供 **MayKernel 自研 x86 32 位桌面**、**Linux x86_64 图形桌面** 和救援环境，使用 GRUB 在 BIOS / x64 UEFI 下选择启动。UEFI x64 支持不代表自研内核为 64 位，也不代表支持 Secure Boot。

- [当前源码与构建说明](MayOS_0.3.0-pre1/README.md)
- [版本更新说明](MayOS_0.3.0-pre1/RELEASE_NOTES.md)
- [第三方组件及许可](MayOS_0.3.0-pre1/docs/third-party/dual-boot.md)
- [验证记录](MayOS_0.3.0-pre1/docs/validation.md)
- [GitHub Releases（请核对附件是否已发布）](https://github.com/ZhongYeMay/MayOS/releases)
- [MayOS 官网](https://mayos-official.business-tea-9633.chatgpt.site/)

Linux 桌面文件目前仅保存在运行进程的 RAM 中；退出桌面后内容丢失。构建和运行条件、已验证范围与限制请以对应版本文档为准。

### 桌面改进（开发中）

[Fastfetch + Ubuntu 风格桌面开发分支](https://github.com/ZhongYeMay/MayOS/tree/feature/fastfetch-ubuntu-ui) 包含新的窗口外观与 Linux 系统信息集成；未合并或未经验证的功能不视为正式发布内容。Fastfetch 仅适用于 Linux 用户态启动项，自研内核使用原生系统信息输出。

### 历史说明

旧版 `MayOS_0.1_Eng/` 与 `MayOS_0.2_Eng/` 文件已从当前工作分支移除，以避免误用早期 16 位实验镜像。它们仍可通过仓库的 Git 提交历史查阅；本次清理**未改写历史提交**。

原仓库 LICENSE 保留；第三方组件遵循各自许可，详见对应声明。清理旧目录不改变任何既有授权。

MayOS Team 2023 - 2026.  
This version of MayOS is for test use only.
