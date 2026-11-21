# MayKernel《夏日未寄》原生离线版

此版本只针对 **MayOS 自研 MayKernel x86 32 位**。游戏的窗口绘图、RGBA alpha 合成、键盘事件、剧情状态、路线选择与 MayFS 保存均由自研 freestanding C 编写；不运行 SDL、Linux ABI、WebView，也不移植其他平台的游戏引擎。

**按当前产品要求，MayKernel 版不接入 DeepSeek，也不需要互联网。** Linux 版的 AI 自由对话由另一套原生程序负责，两边不混用运行时实现。

## 组件

- `kernel/galgame.c`：原生剧情状态机、自由文本输入、原版剧情推进、三个角色路线、MayFS 进度读写。
- `kernel/galgame.h`：MayKernel 绘图、文字与键鼠回调。
- `kernel/gui.c`：桌面侧栏原生启动入口与 framebuffer 软件合成。
- `scripts/pack_summer_art.py`：构建时从原始 WebP 解码三张背景、九张立绘的原始像素为 RGBA 数据。
- `scripts/pack_summer_story.js`：构建时从受信任的原版 `site/story.js` 导出**所有原始剧情节点及六个结局**到编译期 C 数据结构。MayKernel 运行时不执行 JavaScript。
- `scripts/record_galgame.py`：QMP 截取真实 QEMU framebuffer，生成 H.264 MP4 视频及 SHA-256 元数据。

## 玩法现状

在 MayKernel 桌面点击「夏日未寄」入口。上方点击女主名字或用 ↑/↓ 切换故事线，按键直接自由填写自己的回答（不是选择题），按 Enter 提交并继续下一段**原版离线剧情**，Ctrl+S 保存进度到 MayFS，Esc 返回桌面。再次启动会尝试恢复合法的游戏存档。

由于离线模式没有语言模型，女主的台词来自原版故事，**不会以 AI 方式动态生成**。部分原版选择节点暂时通过玩家输入中表明“稍后 / 慢慢”的词语触发另一条路线；这只是开发中的可重复分支规则，不能当作自然语言理解。当前 PS/2 输入仅支持 ASCII，中文及日文输入法未实现。场景静态显示原始素材；完整音频、动画、全部故事按钮与可用性测试尚待实现。

## 原版素材构建

需要在 MayOS 仓库的同级目录检出 `ZhongYeMay/galgame` 的 `mayos-game-package` 分支，确保完整的 `site/` 原版资源存在。

```sh
cd MayOS/MayOS_0.3.0-pre1
make SUMMER_SITE=../source-galgame/site
python3 scripts/record_galgame.py \
  --iso build/mayos.iso \
  --output build/recordings/MayKernel-Summer-Native-QEMU.mp4 \
  --source-commit "$(git rev-parse HEAD)"
```

请根据实际路径设置 `SUMMER_SITE`。生成器要求 12 张 WebP 图片和合法的原版剧情脚本；缺少文件会阻止完整资源打包。构建时解码为 RGBA **不会改动原版图像源文件或剧情文本**，但编码形式不再是 WebP，这属于针对自研内核的数据表示转换。

## QEMU 验证与边界

`.github/workflows/maykernel-game-qemu.yml` 会从 GitHub 自动检出原版资源、生成 native x86 ISO，并运行 QEMU 录制真实画面。自动测试产物位于 GitHub Actions 的 `maykernel-native-galgame-qemu-evidence` artifact 中，包含 MP4、JSON、QEMU 串口及录制日志。

2026-11-10，修复 Makefile 默认目标后，提交 `955a41a0` 的自动运行 `42838909692` 已通过 ISO 构建、QEMU 启动及视频录制，生成 19.3 秒的 H.264 1024×768 视频。真实截图已核实为原版电台、海边背景与相应人物立绘的 framebuffer 渲染，不是合成展示。**该次运行尚不包括随后新增的全量原版剧情推进功能**，后者必须单独验证。

此阶段仍缺少音乐/音效、自研音频驱动、中文/日文输入法、完整菜单、六结局完整自动回归，以及静态资源变更后的更多场景验证。**MayKernel 不计划接入 DeepSeek**。

测试流程应以对应提交的 GitHub Actions 运行与实际视频文件为准，不能只因源码存在就声称已运行成功。
