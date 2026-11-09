# MayKernel《夏日未寄》独立原生开发

这个模块直接在自研 MayKernel x86 32 位、PS/2、framebuffer 和 MayFS 上实现，不移植 SDL、Linux、浏览器、运行时兼容补丁，也不使用 WebView。

代码：
- `kernel/galgame.c`：自研游戏输入状态、场景占位显示、角色切换、存档草稿。
- `kernel/galgame.h`：与 MayOS 原生像素/字体绘图接口连接。
- `kernel/gui.c`：在桌面左侧增加游戏启动图标，分发鼠标和键盘。
- `scripts/record_galgame.py`：使用 QEMU QMP 截取真实客体 framebuffer 并录制视频。

## 当前可演示内容

按左侧「夏日未寄」图标可进入全屏应用。三个女主可以切换，玩家自己输入回复，不出现任何固定的对话选项。Enter 在 HTTPS 后端不存在时明确拒绝发送。Ctrl+S 保存输入草稿，Esc 返回桌面。

由于 MayKernel 尚未实现 TCP、TLS、HTTPS、JSON/API 客户端、中文输入法、游戏 WebP 资源加载器与音频驱动，该模块**还不能真正与 DeepSeek 对话**，也没有原版游戏完整素材与音乐。不会把静态台词伪装成 AI。

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

## 原生下一阶段

1. 为 MayKernel 实现独立可用的 TCP/IPv4 网卡驱动（需要 DHCP/DNS）、TLS、HTTP/1.1 和 JSON；保护运行时 API 凭据，避免写入 MayFS 或序列日志。
2. 将原版完整 WebP 素材在构建阶段做**无内容更改**的像素转换或在内核实现独立解码器，随后完成内核专用资源装载与人物动画。
3. 自研文本输入（含中文/日文输入）、音频驱动与合成引擎，以及大容量文件储存和图像资源加载。
4. 将模型主动生成的女主台词与自由填写的玩家回复连成真正回合，完成端到端 QEMU 验证。

本页仅说明当前仓库中确实实现及未实现的能力，不代表完整移植已完成。
