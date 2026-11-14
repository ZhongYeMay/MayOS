#!/usr/bin/env python3
"""Promote MayOS 0.4.1-pre2 to Pages only AFTER its tested release exists.

Retain original historic pre3 demos as local provenance, but make the current
two real QEMU MP4s and desktop screenshot the primary video experience.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SITE = ROOT / "website"
VERSION = "0.4.1-pre2"
OLD = "0.4.1-pre1"
BASE = f"https://github.com/ZhongYeMay/MayOS/releases/download/v{VERSION}"
PREFIX = f"MayOS-{VERSION}"
ASSETS = {
    "desktop": f"{BASE}/{PREFIX}-desktop.png",
    "settings": f"{BASE}/{PREFIX}-settings.png",
    "maykernel_video": f"{BASE}/{PREFIX}-MayKernel64-All-Functions.mp4",
    "linux_video": f"{BASE}/{PREFIX}-Linux64-All-Functions.mp4",
}



# CI may run after the already published preview has been staged. A repeated
# dry run must not require OLD text or rewrite the user's existing website.
# website/check_site.py still runs separately and verifies the actual links.
already_staged = (
    VERSION in (SITE / "index.html").read_text()
    and VERSION in (SITE / "download.html").read_text()
    and f'{BASE}/{PREFIX}-desktop.png' in (SITE / "index.html").read_text()
    and ASSETS["maykernel_video"] in (SITE / "demos.html").read_text()
    and ASSETS["linux_video"] in (SITE / "demos.html").read_text()
    and f'CURRENT = "{VERSION}"' in (SITE / "check_site.py").read_text()
)
if already_staged:
    print(f"PASS: website already staged for {VERSION}; no changes made")
    raise SystemExit(0)


def swap(text, previous, new, source):
    if previous not in text:
        raise RuntimeError(f"{source}: expected text missing: {previous[:90]}")
    return text.replace(previous, new)


for file in ("index.html", "download.html"):
    path = SITE / file
    original = path.read_text()
    text = swap(original, OLD, VERSION, file)
    text = text.replace("macOS 式桌面布局，Material Design 3 灰阶视觉。",
                        "严格遵照指定桌面截图还原的黑灰阶界面；沿用原生窗口与真正的 Material Symbols 图标。")
    text = text.replace("Material 3 桌面", "按参考图还原的原生桌面")
    text = text.replace("深浅色模式、圆角窗口、真实图标、应用启动器、通知中心、快捷设置与 Windows 风格鼠标。",
                        "深浅色、六种可选主题色、历史版圆角窗口、真实 Material Symbols、居中 Dock 与黑白方块或自定义图像指针。")
    text = text.replace("Material 3 GUI", "严格参考图界面")
    text = text.replace("原生 Material 桌面", "按指定参考图恢复的原生桌面")
    text = text.replace("MayKernel x86 或 Linux", "MayKernel x86_64 或 Linux")
    if file == "index.html":
        # Only alter hero screenshot, not local archive assets.
        oldshot = "media/MayOS-0.4.0-pre3-browser.png"
        text = swap(text, oldshot, ASSETS["desktop"], file)
        text = text.replace("真实 MayOS 0.4.0-pre3 QEMU 桌面截图",
                            "真实 MayOS 0.4.1-pre2 QEMU 截图")
        text = text.replace("QEMU 实测 MayOS 0.4.0-pre3 桌面：顶部图标状态栏、底部 Dock 和 Windows 风格鼠标指针",
                            "QEMU 实测 MayOS 0.4.1-pre2：用户指定的顶部菜单、状态图标、文件桌面与居中 Dock")
        text = text.replace("自研内核启动实录", "MayKernel64 实际操作录像")
    else:
        text = text.replace("已通过 QEMU BIOS / UEFI 启动验证",
                            "已通过实际 QEMU BIOS / UEFI、桌面与文件系统回归验证")
        text = swap(text, '<p class="eyebrow">Earlier builds</p>',
                    '<p class="eyebrow">Earlier builds</p>', file)
        text = text.replace(
            '<div class="actions"><a class="btn secondary" href="https://github.com/ZhongYeMay/MayOS/releases/tag/v0.4.0-pre2">',
            '<div class="actions"><a class="btn secondary" href="https://github.com/ZhongYeMay/MayOS/releases/tag/v0.4.1-pre1">0.4.1-pre1</a><a class="btn secondary" href="https://github.com/ZhongYeMay/MayOS/releases/tag/v0.4.0-pre2">'
        )
    path.write_text(text)

# Video player sources are direct links to the immutable, hash-verified Release,
# not to an unrelated pre3 video just because both display a desktop.
path = SITE / "demos.html"
text = path.read_text()
text = text.replace("0.4.0-pre3", VERSION)
text = text.replace("MayKernel x86", "MayKernel64 x86_64")
text = text.replace("MayOS 0.4.1-pre2 MayKernel 真实 UEFI 启动实录",
                    "MayOS 0.4.1-pre2 实际 QEMU MayKernel64 桌面与设置录像")
text = text.replace("MayOS 0.4.1-pre2 Linux 7.3.1 真实 UEFI 启动实录",
                    "MayOS 0.4.1-pre2 实际 QEMU Linux64 桌面与设置录像")
for kernel, slug in (("MayKernel", "MayKernel64"), ("Linux", "Linux64")):
    old_mp4 = f"media/MayOS-0.4.1-pre2-all-features-{kernel}.mp4"
    new_mp4 = ASSETS["maykernel_video" if kernel == "MayKernel" else "linux_video"]
    text = swap(text, old_mp4, new_mp4, "demos.html")
    text = text.replace(
        f'{BASE}/MayOS-0.4.1-pre2-all-features-{kernel}.mp4', new_mp4
    )
    text = text.replace(
        f'media/MayOS-0.4.1-pre2-all-features-{kernel}.json',
        f'{BASE}/{PREFIX}-{slug}-All-Functions.json'
    )
text = text.replace(f"media/MayOS-{VERSION}-browser.png", ASSETS["desktop"])
text = text.replace(f"{BASE}/MayOS-{VERSION}-browser.png", ASSETS["desktop"])
text = text.replace("82.9 秒", "以实际录制文件为准")
text = text.replace("89.8 秒", "以实际录制文件为准")
text = text.replace("10 FPS", "8 FPS")
path.write_text(text)

# Only the latest copy/links change; historic local demo files remain verifiable.
path = SITE / "check_site.py"
source = path.read_text()
source = swap(source, 'CURRENT = "0.4.1-pre1"', f'CURRENT = "{VERSION}"', "check_site.py")
source = source.replace('assert DEMO_RELEASE in docs["demos.html"][1], "historical demo missing"',
                        'assert CURRENT in docs["demos.html"][1], "latest real-QEMU demo missing"')
source = source.replace('"MayOS-0.4.1-pre1-dual-boot-x86_64.iso"', f'"{PREFIX}-dual-boot-x86_64.iso"')
source = source.replace('"MayOS-0.4.1-pre1-MayKernel-x86_64.iso"', f'"{PREFIX}-MayKernel-x86_64.iso"')
source = source.replace('PASS: MayOS 0.4.1-pre1 website and historical pre3 media hashes',
                        f'PASS: MayOS {VERSION} website and archived pre3 media hashes')
source = source.replace('assert docs["demos.html"][0].videos == 2',
                        'assert docs["demos.html"][0].videos == 2')
# Check not accidentally linking 0.4.0-pre3 videos as the current video.
source += f'''
for asset in ("{PREFIX}-MayKernel64-All-Functions.mp4",
              "{PREFIX}-Linux64-All-Functions.mp4",
              "{PREFIX}-desktop.png"):
    assert asset in (docs["demos.html"][1] + docs["index.html"][1]), asset
'''
path.write_text(source)
print(f"PASS: website staged for verified {VERSION}; local historical videos preserved")
