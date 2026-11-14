# MayOS 官网 · GitHub Pages

## 2026-11-14 · 高级深色 UI 开发预览
- 首页 `#design-preview` 以**静态 HTML/CSS** 显示新视觉标准：黑色/炭灰、细描边、抗视觉杂讯、圆角信息卡、左侧设置导航与悬浮 Dock；自动适配移动屏幕。
- 网站默认深色，但保留原有浅色主题切换与可访问性支持。
- **版本界线：** `0.5.0-pre1-dev` 是 PR #12 中**未发布**的开发版本，概念视觉不能当作实机截图。页面继续保留 `0.4.1-pre2` 的真实 QEMU 历史截图与 ISO/MP4 下载链接。
- 原生系统界面仍以实际 MayKernel QEMU 回归和截图为最终验收依据。网站不声称新版 Linux 双内核或实体机稳定性已验证。

根据 2026-11-07 提供的草图实现三页静态站点：
- `index.html`：MayOS 字标、双内核介绍、开发进度。
- `download.html`：源码与历史启动包、SHA-256 与复制按钮。
- `demos.html`：历史 MayKernel / Linux 启动录像。

共享 `style.css` 和 `site.js`，包含移动端布局、可持久化深浅主题、键盘焦点及复制失败提示。所有站内路径使用相对地址，兼容 `/MayOS/` 项目站点。

运行 `python3 website/check_site.py` 检查全部页面；本地预览：`python3 -m http.server 8080`，访问 `/website/`。

部署工作流：`.github/workflows/website-pages.yml`。GitHub Pages Source 使用 GitHub Actions，main 的网站提交触发部署；访问 https://zhongyemay.github.io/MayOS/ 。站点可用性以部署结果为准。

源码快照不含新版 ISO。网站不将设计草图中的 0.0.4、ARM、互联网支持和 UEFI IA32 宣称为已发布能力。历史附件须核实可访问后才能列为下载。
