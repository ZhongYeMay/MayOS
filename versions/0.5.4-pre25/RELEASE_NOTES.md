# MayOS 0.5.4-pre25 · MayKernel 磁盘检测

- 任务管理器区分硬盘已识别但 MayFS 使用内存，以及没有识别到支持的硬盘。
- MayKernel Live 会识别原生驱动支持的 NVMe、SATA/AHCI 或 ATA 磁盘，继续使用 RAM 文件系统。
- `/proc/mounts` 显示实际挂载状态，`/proc/diskstats` 显示 MayKernel 记录的磁盘活动。
