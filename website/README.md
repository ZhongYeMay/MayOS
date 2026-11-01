# MayOS 官网源文件

本目录是 2026-11-01 起维护的 MayOS 官方网站静态源码，入口为 `index.html`。它介绍当前仓库开发进度与历史可用构建，且**不会把未发布的 ISO 标成可用下载**。

## 本次界面

- 采用与 MayOS 桌面一致的深紫灰、Ubuntu 橙色、简洁窗口与 Dock。
- 响应式设计、语义化页面结构、键盘焦点、减少动态效果。
- 桌面卡片是 **CSS 示意图**，不是 QEMU 真实截图。
- 现有 `0.3.0-pre1` 链接明确标记为历史构建；更新源代码不自动更新镜像。
- 最新源码统一链接至 `ZhongYeMay/MayOS` 的 `main` 分支。

## 当前源码预发布

最新源码预发布为 [v0.3.0-pre1-source.1](https://github.com/ZhongYeMay/MayOS/releases/tag/v0.3.0-pre1-source.1)，包含 Ubuntu 风格、Fastfetch 与 Linux framebuffer 性能优化的源码和 SHA-256 校验。它**不是新版可启动 ISO**。网站下载区将源码与旧版测试镜像分开标注。

## 部署

`website/index.html` 为单文件静态站点，可部署到任意静态网页主机。仓库含 GitHub Pages Action（`.github/workflows/website-pages.yml`）；使用前可能需要在 **Settings → Pages → Build and deployment → Source** 选择 GitHub Actions。仅当该 Action 的部署步骤成功时，才能使用 Pages 地址。

既有网站 `https://mayos-official.business-tea-9633.chatgpt.site/` 位于不同的托管服务，单纯向 GitHub 提交网页**不会**自动覆盖其内容；要更新该域名，还需用原站点管理环境重新部署本目录页面。

更新链接或发布信息时，先运行 `python3 website/check_site.py`，并核对发布版本、ISO SHA-256、QEMU 启动实测及许可附件。

MayOS Team 2023 - 2026.
