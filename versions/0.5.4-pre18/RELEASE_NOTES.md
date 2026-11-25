# MayOS 0.5.4-pre18 · 4Kn 安装和启动

- 安装器根据设备逻辑扇区大小选择 GPT 与 EFI 布局。512 字节目标最少 80 MiB，4Kn 目标最少 288 MiB。
- MayKernel64 按实际逻辑扇区大小处理 NVMe、AHCI、GPT 和 MayFS 读写。MayFS 快照保留原有格式。
- 4Kn 安装仍先检查整盘是否为空，写入后验证 GPT 与系统数据，并为每次安装生成独立标识。
- NVMe 4Kn 可在 QEMU 测试。QEMU 的 AHCI 磁盘模型只接受 512 字节逻辑扇区，SATA 4Kn 待实体机验证。
