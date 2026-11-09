# MayKernel《夏日未寄》独立原生开发

这个模块直接在自研 MayKernel x86 32 位、PS/2、framebuffer 和 MayFS 上实现，不移植 SDL、Linux、浏览器、运行时兼容补丁，也不使用 WebView。

代码：
- `kernel/galgame.c`：自研游戏输入状态、场景占位显示、角色切换、存档草稿。
- `kernel/galgame.h`：与 MayOS 原生像素/字体绘图接口连接。
- `kernel/gui.c`：在桌面左侧增加游戏启动图标，分发鼠标和键盘。
- `scripts/record_galgame.py`：使用 QEMU QMP 截取真实客体 framebuffer 并录制视频。

## 当前可演示内容

按左侧「夏日未寄」图标可进入全屏应用。三个女主可以切换，玩家自己输入回复，不出现任何固定的对话选项。Enter 在 HTTPS 后端不存在时明确拒绝发送。Ctrl+S 保存输入草稿，Esc 返回桌面。

由于 MayKernel 尚未实现 TCP、TLS、HTTPS、JSON/API 客户端、中文输入法与音频驱动，该模块**还不能真正与 DeepSeek 对话**或播放音频。

图像素材现已加入一条完全自研的构建路径：`scripts/pack_summer_art.py` 将原版 3 张背景与 9 张人物 WebP 解码为 **逐像素一致的 RGBA**，再通过 GNU ELF32 链接作为数据嵌入 MayKernel；`kernel/gui.c` 内的原生 C framebuffer 绘制函数自行处理图片缩放与 alpha 混合。运行时完全不用 SDL、WebP 库、Linux 补丁或 WebView。未提供原始素材时保留明确的资源缺失提示，不伪装完整运行。

在仓库根目录同时检出原版 Galgame 后，可这样构建带全部原始画面的 MayKernel ISO：

```bash
make SUMMER_SITE=../source-galgame/site
```

`SUMMER_SITE` 应指向含有 `assets/retro/*.webp` 的完整原版 site 目录，缺任意一张画面都会使打包失败。此方案在视觉上保留原始 RGBA 像素，但不是对 WebP 文件的原样存储——资源编码改变以适应自研内核。不会把静态台词伪装成 AI。

Linux 原生 SDL2 + DeepSeek 版本保存在 `ZhongYeMay/galgame` 的 `mayos-game-package` 分支，**不是** MayKernel 代码的移植来源。

## 在支持 QEMU 的 Linux 主机实录

```bash
make
python3 scripts/record_galgame.py \
  --iso build/mayos.iso \
  --output build/recordings/MayKernel-Summer-Native-QEMU.mp4 \
  --source-commit "$(git rev-parse HEAD)"
```

此脚本从开机到返回桌面的每一帧都从真实 QEMU framebuffer 读取，记录源码/ISO/视频哈希。只有运行成功且生成 MP4 和 JSON 元数据后才算录制完成。

录屏证明范围仅限 **MayKernel 原生 GUI、文本输入、存档快捷键、角色选择与返回桌面**；不证明 AI 网络、素材、音乐和完整剧情可用。自动化工作流位于 `.github/workflows/maykernel-game-qemu.yml`，构建完成后下载测试视频和日志。

## QEMU 视频说明

`.github/workflows/maykernel-game-qemu.yml` 的自动化测试会同时检出 Galgame 的原始素材分支、以 `SUMMER_SITE` 构建嵌入原画的内核 ISO，随后通过真实 QEMU/QMP 帧采集录制 MP4。工作流源码提交不等于已成功录屏；必须看到成功的工作流执行、MP4 文件及 JSON 哈希记录后才能确认。

## 原生下一阶段

1. 为 MayKernel 实现独立可用的 TCP/IPv4 网卡驱动（需要 DHCP/DNS）、TLS、HTTP/1.1 和 JSON；保护运行时 API 凭据，避免写入 MayFS 或序列日志。
2. 实现画面渐变、人物姿态动画、更多文本演出，以及持久化资产存储和内核专用资源管理（已实现的嵌入式 RGBA 静态资产仍需性能验收）。
3. 自研文本输入（含中文/日文输入）、音频驱动与合成引擎，以及大容量文件储存和图像资源加载。
4. 将模型主动生成的女主台词与自由填写的玩家回复连成真正回合，完成端到端 QEMU 验证。

本页仅说明当前仓库中确实实现及未实现的能力，不代表完整移植已完成。
