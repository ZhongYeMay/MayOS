# MayOS 0.5.4-pre8 · MayKernel64 原生 Bing 壁纸

- 新增 MayKernel64 原生 Bing 壁纸下载。使用 DHCPv4、DNS 和校验证书的 TLS 1.2，沿用「设置 → 个性化 → 桌面 → Bing 每日」。
- 可信时间有效时，每小时检查一次 Bing 的发布日期，日期更新才下载。失败时每五分钟重试，保留已显示的壁纸。壁纸保存在本次会话的内存中。
- 公共 ISO 无预装的时间签发机构。部署时用自己的公钥构建镜像，启动后验签 UTC。签名时间六小时后需续签。
- 此版源码包含 Mozilla CA 原始 PEM 和校验值，补齐上一版源码归档中缺少的文件。

本版为 MayKernel64 BIOS / x86_64 UEFI 预发布。自动刷新在隔离的 QEMU HTTPS 环境验证。
