#!/usr/bin/env python3
"""Reproducibly port 夏日未寄 story and AI-generated artwork to both MayOS kernels.

The pin and git-blob hashes prevent accidental source drift. Runtime never uses
a JS browser/CDN and the native release contains no WebP decoder dependency.
"""
import hashlib
import json
import subprocess
from pathlib import Path
from urllib.request import urlopen

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build"
CACHE = ROOT / "assets" / "galgame-cache"
COMMIT = "fa34344a1cff46025e8d169f56411e290e9fafbd"
ROOT_URL = f"https://raw.githubusercontent.com/ZhongYeMay/galgame/{COMMIT}/site/"
HASHES = {
    "story.js": "3d36fe369c59fcbfa806b75c8a12a3849b2c33a5",
    "assets/retro/bookshop.webp": "69ed280f1c9185600a8aab258ba0a8d8d422de6d",
    "assets/retro/coast.webp": "71e6b888a0574bc94a41980c5a9f9a14cf4dc5a6",
    "assets/retro/mio-0.webp": "edf2600210d90674230abdd121db99c6807f61c8",
    "assets/retro/mio-1.webp": "a983253fbd041656785227a956e389a7c761c2ab",
    "assets/retro/mio-2.webp": "31ee0b6a2ea906aadc9b1c9732eba5a8a494cd54",
    "assets/retro/nagi-0.webp": "8617be51203ddc08b4ec3ed5e79dc23b978da5cb",
    "assets/retro/nagi-1.webp": "490686cd08b903f17636b8464094483ca1d99baf",
    "assets/retro/nagi-2.webp": "4bdf1879579be79f904f30ef7afb07798651fd52",
    "assets/retro/shiori-0.webp": "e424ac6446b128dfd8417737ef81bb7ba9330fb3",
    "assets/retro/shiori-1.webp": "ba17f717dc1a45a4f0d9794a2d7bfbb0a2e93377",
    "assets/retro/shiori-2.webp": "a44238fc64ccf75e627b803102670e251343cc7a",
    "assets/retro/studio.webp": "9908534b21f7981b69dca4f053a5072608e227ec",
}
IMAGES = ["studio", "coast", "bookshop"] + [
    f"{person}-{expression}" for person in ("mio", "nagi", "shiori") for expression in range(3)
]
SCENES = {"studio": 0, "coast": 1, "bookshop": 2}
CHARACTERS = {"mio": 0, "nagi": 1, "shiori": 2}


def checked_file(path):
    target = CACHE / path
    if not target.is_file():
        target.parent.mkdir(parents=True, exist_ok=True)
        with urlopen(ROOT_URL + path, timeout=50) as result:
            data = result.read()
        target.write_bytes(data)
    data = target.read_bytes()
    sha = hashlib.sha1(f"blob {len(data)}\0".encode() + data).hexdigest()
    expected = HASHES[path]
    if sha != expected:
        raise RuntimeError(f"Source mismatch {path}: {sha} != {expected}")
    return target


def literal(value):
    return json.dumps(str(value or ""), ensure_ascii=False)


def compile_story():
    source = checked_file("story.js")
    script = r"""
    const fs = require("fs"), vm = require("vm");
    const source = fs.readFileSync(process.argv[1], "utf8");
    const context = vm.createContext({});
    vm.runInContext(source + "\n;globalThis.exported = {CHARACTERS, STORY, ENDINGS};",
                    context, {timeout: 2000});
    process.stdout.write(JSON.stringify(context.exported));
    """
    content = subprocess.check_output(["node", "-e", script, str(source)], text=True)
    data = json.loads(content)
    story = data["STORY"]
    assert len(story) > 60, "Story extraction unexpectedly incomplete"
    for node in story.values():
        if node.get("next") and node["next"] not in story:
            raise ValueError("Broken next node: " + node["next"])
        for choice in node.get("choices", []):
            if choice[1] not in story:
                raise ValueError("Broken route choice: " + choice[1])
    entries = []
    for key, n in story.items():
        choices = n.get("choices", [])[:3]
        labels = [literal(i[0]) for i in choices] + ['""'] * (3 - len(choices))
        targets = [literal(i[1]) for i in choices] + ['""'] * (3 - len(choices))
        props = [
            literal(key), literal(n.get("who") or ""),
            literal(n.get("text") or ""), literal(n.get("next") or ""),
            str(SCENES.get(n.get("bg"), 0)), str(n.get("pose", 0)),
            str(int(bool(n.get("ending")))), str(len(choices)),
            "{" + ", ".join(labels) + "}",
            "{" + ", ".join(targets) + "}",
            literal(n.get("title") or ""), literal(n.get("kind") or ""),
        ]
        entries.append("  {" + ", ".join(props) + "},")
    header = [
        "/* Generated from pinned original 夏日未寄 story. Do not edit. */",
        "typedef struct {",
        " const char *id, *speaker, *dialogue, *next;",
        " int scene, pose, ending, choices;",
        " const char *labels[3], *targets[3], *title, *kind;",
        "} GNStory;",
        "static const GNStory gn_story[] = {",
        *entries,
        "};",
        f"#define GN_STORY_COUNT {len(entries)}",
        "static const char *gn_routes[] = {" +
            ", ".join(literal(data["CHARACTERS"][key]["title"]) for key in CHARACTERS) + "};",
        "static const char *gn_names[] = {" +
            ", ".join(literal(data["CHARACTERS"][key]["name"]) for key in CHARACTERS) + "};",
        "static const char *gn_places[] = {" +
            ", ".join(literal(data["CHARACTERS"][key]["place"]) for key in CHARACTERS) + "};",
    ]
    (OUT / "galgame_story.h").write_text("\n".join(header) + "\n", encoding="utf-8")
    (OUT / "galgame_manifest.json").write_text(
        json.dumps({"upstream_commit": COMMIT, "title": "夏日未寄 — 夏の便り",
                    "story_nodes": len(entries), "scenes": list(SCENES),
                    "sprites": 9, "story_git_blob_sha1": HASHES["story.js"]},
                   ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"MayOS visual novel: {len(entries)} authentic story nodes")


def cbytes(data):
    # Break up C string literals for bounded line length and compiler throughput.
    return "\n".join('"' + "".join(f"\\x{v:02x}" for v in data[i:i + 96]) + '"'
                     for i in range(0, len(data), 96))


def compile_images():
    dest = OUT / "galgame_assets.h"
    with dest.open("w", encoding="ascii") as f:
        f.write("/* Generated pinned AI-generated WebP art; 8-bit indexed palette. */\n")
        f.write("typedef struct {int w, h; const unsigned char *data; const unsigned int *pal;} GNImage;\n")
        for idx, name in enumerate(IMAGES):
            source = checked_file("assets/retro/" + name + ".webp")
            im = Image.open(source).convert("RGBA")
            scene = idx < 3
            width, height = (854, 470) if scene else (342, 470)
            im.thumbnail((width, height), Image.Resampling.LANCZOS)
            if scene:
                bg = Image.new("RGB", (width, height), (35, 35, 37))
                bg.paste(im, ((width - im.width) // 2, (height - im.height) // 2), im)
                pal = bg.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
                raw = pal.tobytes()
                color_count = 256
                colors = pal.getpalette()[:256 * 3]
            else:
                # Transparent palette index 0; no alpha-blending requirement at runtime.
                layer = Image.new("RGBA", (width, height), (0, 0, 0, 0))
                layer.paste(im, ((width - im.width) // 2, height - im.height), im)
                rgb = Image.new("RGB", layer.size, (24, 23, 27))
                rgb.paste(layer, mask=layer.getchannel("A"))
                pal = rgb.quantize(colors=255, method=Image.Quantize.MEDIANCUT)
                alpha = layer.getchannel("A").tobytes()
                raw = bytes(index + 1 if alpha[i] >= 75 else 0
                            for i, index in enumerate(pal.tobytes()))
                color_count = 255
                colors = [0, 0, 0] + pal.getpalette()[:255 * 3]
            palette = [
                (colors[i * 3] << 16) | (colors[i * 3 + 1] << 8) | colors[i * 3 + 2]
                for i in range(256)
            ]
            f.write(f"static const unsigned char gn_pixels_{idx}[] =\n")
            f.write(cbytes(raw) + ";\n")
            f.write(f"static const unsigned int gn_palette_{idx}[256] = {{")
            f.write(", ".join(f"0x{v:06x}" for v in palette))
            f.write("};\n")
            print(f"  {name}: {len(raw)} indexed pixels")
        f.write("static const GNImage gn_art[] = {\n")
        for idx in range(len(IMAGES)):
            w, h = ((854, 470) if idx < 3 else (342, 470))
            f.write(f"  {{{w},{h},gn_pixels_{idx},gn_palette_{idx}}},\n")
        f.write("};\n")


def main():
    OUT.mkdir(exist_ok=True)
    compile_story()
    compile_images()


if __name__ == "__main__":
    main()
