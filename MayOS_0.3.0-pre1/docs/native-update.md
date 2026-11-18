# MayKernel 原生系统更新 · 阶段一（离线元数据检查）

本阶段提供可运行的 **MayKernel 内核侧**更新清单解析器，而非网页模拟。
由于 MayKernel 尚无原生网卡、IP/TCP、TLS 和引导安装/回滚机制，**不支持直接从 GitHub 检查、下载、安装或刷写系统**。

## 使用
1. 在 MayOS 自研 MayKernel 桌面的文件管理器中，前往 \`Documents\`。
2. 用文本编辑器建立名为 \`Update.may\` 的**纯 ASCII** 文件，格式如下（所有值均为示例，不代表真实 Release）：

\`\`\`ini
name=MayOS
version=0.5.0-pre3
architecture=x86_64
kernel=MayKernel
release_date=2026-11-18
size_bytes=1048576
sha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
eta_minutes=15
changelog=Example metadata only
\`\`\`

3. 在「设置 → Updates」点击 **Check local manifest**，或在 MayShell 输入 \`update check\`。
4. \`update status\` 读取最后一次检查结果。此操作无网络、无下载、无刷写。

## 安全约束

- 读取范围由 MayFS 的文件长度限制，最多 2 KiB，拒绝 NUL、过长行、重复/未知字段和数值溢出。
- 仅接受 \`architecture=x86_64\`、\`kernel=MayKernel\` 和符合版本规则的候选；候选是 **UNVERIFIED** 状态。
- \`sha256\` 字段只校验字符格式，**没有**读取及核对完整发行镜像，也没有验证签名；清单完全可能由本地攻击者伪造。
- 从不自动下载、改写数据盘或启动分区，不能将此功能称为安全更新器。
- 安装之前必须补全：原生受信任 HTTPS 或离线可信介质读取、发行公钥签名验证、完整镜像 SHA-256、原子化 A/B 引导切换、回滚、断电恢复、真实下载与安装进度、设备架构适配和 QEMU 验证。

## 开发验证

\`\`\`sh
make test-update    # 独立原生 C 解析器测试（无需 QEMU）
make test           # MayKernel ISO + 原有 BIOS QEMU 回归及解析器测试
\`\`\`

本文件不属于发布说明。没有通过真实 ISO / QEMU 回归之前，不发布新的稳定版本。
