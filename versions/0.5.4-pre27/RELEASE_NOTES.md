# MayOS 0.5.4-pre27 · MayKernel 断网重连

- 以太网链路断开后，MayKernel 清除旧的 IPv4 租约与 DNS 结果。链路恢复后自动重新获取 IPv4。
- 断网前仍在运行的 DNS、UDP 和 TCP 请求会及时失败。网页和其他应用可以在恢复后重新发起请求。
- 修复 DHCP 租约到期时，正在等待的 DNS 请求可能得不到失败通知的问题。
