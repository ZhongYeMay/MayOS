# MayOS 0.5.3-dev · 自动显示模式开发版

**未发布的开发源码**。基于已验证的 MayOS v0.5.2，保留原来的黑灰色桌面、MayOS Live、双内核和安装器基础。GitHub 正式版本 `versions/0.5.2/` 保持只读，不随此开发目录修改。

## 本阶段功能

- GRUB 优先使用固件的 `gfxmode=auto`，不再固定优先 1280×720；启动后保留真实选中的 GOP/VBE 视频模式。
- Linux64 从已连接 DRM 接口的 `modes` 列表读取优先显示模式，支持最宽或最高边不超过 4096 像素的全部兼容宽高比（含 4K UHD/DCI、16:10、4:3、21:9 和竖屏），仅在对应 framebuffer 驱动允许时切换；拒绝时回退原固件模式。
- MayKernel64 从 GRUB/固件选择的视频模式读取实际宽高，通过相同比例缩放到物理 framebuffer；**没有声明已具备完整的原生 EDID/GOP 模式重新配置驱动**。
- Fit/Fill 布局和触摸屏绝对坐标使用同一比例几何算法，避免超宽屏和竖屏点击错位。

**边界：** 当前桌面的逻辑画布仍是 1280×720；更高清晰度的原生 HiDPI 字体、真正的动态布局和显示器热插拔属于后续阶段。实体显卡、EDID、USB-C/DP/HDMI 兼容性尚未全面实测。不能宣称所有显示器已全部通过认证。

## 验证

```sh
gcc -std=gnu11 -O2 -Wall -Wextra -Werror scripts/display_auto_test.c -o /tmp/display-auto-test
/tmp/display-auto-test
gcc -std=gnu11 -O2 -Wall -Wextra -Werror scripts/framebuffer_test.c -o /tmp/framebuffer-test
/tmp/framebuffer-test
make all
python3 scripts/smoke.py
```

本开发目录与所有历史版本目录平级，不嵌套进 `MayOS_0.3.0-pre1`。
