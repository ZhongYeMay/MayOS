# MayOS 0.5.4-pre26 · MayKernel 网络状态

- 任务管理器和网络设置区分未识别网卡、以太网链路断开、等待 IPv4 和已分配 IPv4。
- MayShell 可用 `cat /proc/net/mayos` 查询链路与 IPv4 状态。`/proc/net/dev` 继续提供收发计数。
- 已分配 IPv4 只表示取得了地址。网络吞吐量仍来自驱动计数。
