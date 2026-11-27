# MayOS 0.5.5-pre1 · 预览版候选说明（未发布）

**当前版本 0.5.5-pre1-dev，仅供开发验收，不是 GitHub Release。**

## 候选功能

- MayKernel x86_64 / Linux64 双启动、Live 与安装器路径将在实际 QEMU 验收后分别说明。
- Linux64 追加 E1000、E1000e、VirtIO-net 驱动并进行三网卡 DHCP/TLS 1.3 验收；对应 CI 未通过前不得宣称已支持。
- Linux64 追加 Bing 每日 HTTPS 图片获取、1280×720 RGB565 缓存、热更新与断网回退；代码及离线测试已具备，真实 Bing 联网仍需验收。
- MayKernel 当前保持本地黑灰壁纸，自动获取 Bing 图片须待可信时间、加密熵、CA/X.509/TLS 与大图片持久化完成。严禁通过明文 HTTP 绕过。
- Linux64 RAM-only 默认保护和显式标记 ext4 A/B 持久化，必须通过实际 QEMU 掉电重启回归。

## 验收与 Pre 发布节奏

1. 同 SHA 的 native/Linux64 构建、GRUB 双启动与真实 QEMU 网络和数据保护回归全部成功。
2. 与当前主分支 v0.5.4-pre31 的 Explorer、更新页面及其他功能差异完成整合审查。
3. 公开链接、SHA-256、运行截图和已知限制均在发布前核查。
4. 达标后**只发布一次** `v0.5.5-pre1`；后续常规修复继续 commit/PR，避免每修一次发布一个 Pre。

详细准入条件见 `RELEASE_GATE.md`；实际 QEMU 证据见 workflow `mayos-055-pre1-integration.yml`。
