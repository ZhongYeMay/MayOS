# MayOS 0.5.4-pre21 · 任务管理器与显示模式

- MayOS Task Manager 使用 Rust，在 MayKernel64 和 Linux64 提供设备侧栏、独立性能页和一分钟历史。布局以开源 Mission Center 的性能视图为参考，保留 MayOS 窗口控件。
- CPU、内存、磁盘和网络来自内核计数器。Linux 提供逻辑 CPU 视图与进程操作。MayKernel 的 MayFS 统计按实际扇区大小累计字节，兼容 512 字节扇区和 4Kn。
- GPU 视图显示驱动实际报告的占用率。Linux 支持 gpu_busy_percent 及 Intel i915 渲染引擎 PMU 接口。MayKernel 当前显示 PCI 图形设备与桌面提交速率，3D 引擎占用率依赖对应 GPU 驱动。
- Linux 设置通过 DRM/KMS 切换单显示器的分辨率和驱动提供的刷新率组合。显示模式可以确认、手动恢复，或在 15 秒未确认时自动恢复。没有兼容 KMS 的环境继续使用固件缓冲区。
- 原生 Explorer 可在收到完整且经过认证的定长 HTTP 响应后显示内容。TLS 证书和签名时间要求保留。
