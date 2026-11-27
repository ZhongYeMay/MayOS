# MayOS Bing 每日壁纸 · 0.5.4-dev 开发说明

**状态：Linux64 已实现每日更新原型及离线测试；MayKernel 自动联网下载未完成。** 不能据此声称双内核已验证或直接发布稳定版。

## Linux64 功能

- 使用固定 HTTPS Bing 首页图片元数据地址： https://www.bing.com/HPImageArchive.aspx?format=js&idx=0&n=1&mkt=zh-CN
- 元数据下载上限 64 KiB，JPEG 上限 12 MiB；证书与 HTTPS 主机名校验开启，拒绝重定向以及非 Bing 来源的图片。
- 用 libjpeg 解码并生成完整 1280×720 RGB565 图像，先写临时文件再同步和原子替换。渲染器每隔约 15 秒加载新缓存，不需要桌面重启。
- 首次启动、连接失败、证书验证失败或图片格式不受支持时，保留旧壁纸；没有缓存时显示原有黑灰渐变。
- 服务每 30 分钟检查一次 Bing 图片日期；失败后 60 秒重试。地区图片发布日期不一定等于本地零点。
- 默认 Live/RAM-only 只将图片放在 /run/mayos-wallpaper；只有显式选择并验证 MayOS 专用标记的 ext4 挂载点才进行磁盘缓存，绝不格式化其他磁盘。
- 辅助程序采用运行时系统 libcurl、libjpeg 及其许可证文件，构建器检测并打包依赖。

## MayKernel 当前限制

共享 GUI 已预留相同的壁纸绘制路径，但在 MayKernel 上还没有可信的公网 HTTPS、稳健时间/熵源、较大图片持久化等基础能力，因此不会把不安全的 HTTP 下载标注为自动更新。当前 MayKernel 仍保留原有渐变背景。

后续须对 MayKernel 的认证 TLS、接收字节上限、图片校验、持久缓存和断网回退逐项测试，才可开启每日联网获取。

## 版权和分发

Bing 照片由相应版权方拥有。MayOS 不会把 Bing 当日 JPEG 打进 ISO、GitHub Release、源码或宣传包；仅在终端用户设备上为个人壁纸用途下载。Microsoft 服务条款和单张照片的限制仍适用，公开再分发前必须单独审核许可。Bing 元数据路径属于非保证稳定的网页内部接口，可能更改。

## 验证

- 离线解码和安全保留：python3 scripts/bing_wallpaper_test.py
- QEMU 启动和编译测试：.github/workflows/linux-persistent-desktop.yml 与 .github/workflows/linux64-nic-compat.yml
- 仅通过离线测试不能证明已实测联网到 Bing；必须在实际 ISO 中获得同 SHA 的网络和壁纸日志再宣称 Linux64 在线更新通过。
