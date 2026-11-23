# MayOS 0.5.4-pre16 · SATA 多盘启动

- MayKernel64 枚举 AHCI 控制器上的 SATA 磁盘，并按引导器传入的标识选择安装对应的 MayFS。
- 每个 SATA 端口使用独立的 DMA 缓冲区。若发现两个相同标识，或磁盘扫描未能完成，安装版进入 RAM 会话。
