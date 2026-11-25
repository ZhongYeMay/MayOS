# MayOS 0.5.4-pre22 · 任务管理器内存统计

- MayKernel 从 BIOS 或 UEFI 内存表统计检测到的可用物理 RAM，合并重叠并扣除固件保留页。
- 页分配器由 128 MiB 地址范围扩大到 32 位 DMA 可用范围（4 GiB 以下）。如存在 4 GiB 以上的 RAM，系统会检测并单独显示，当前分配器无法使用该部分。
- 任务管理器分别列出已用、可用和检测容量。显示单位自动选择 KiB、MiB 或 GiB，图表只计算系统能管理的内存。
- MayKernel 的 /proc/meminfo 与任务管理器使用同一快照，并附加 MayDetected、MayAllocTotal 和 MayUnmanaged。Linux 继续读取其自身的 MemTotal 和 MemAvailable。
