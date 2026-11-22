# MayOS 0.5.4-pre9 · MayKernel64 自动可信时间

- 启动并取得 DHCP 租约后，MayKernel64 通过 Cloudflare Roughtime 自动获取签名 UTC，无需手动输入时间凭据。内核固定服务公钥，并检查一次性挑战、签名、时间精度和回拨。
- 签名时间有效时，Bing 每日壁纸继续自动检查发布日期。成功同步后每小时续期。网络故障时保留已经显示的壁纸。
- 需要支持的 Ethernet 网卡、RDSEED、DNS、Roughtime UDP 2003 和 Bing HTTPS。自动同步曾在 QEMU 的隔离签名时间及 HTTPS 服务中验证，公网直连仍待验证。
