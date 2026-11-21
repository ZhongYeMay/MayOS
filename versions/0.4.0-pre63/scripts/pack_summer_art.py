#!/usr/bin/env python3
"""Prepare original WebP art as exact, unmodified decoded RGBA for MayKernel.

Only the game assets are transformed to a freestanding raw RGBA binary; no SDL,
browser, dynamic image libraries, or Linux ABI are used by the kernel itself.
All 12 original heroine poses and backgrounds are required, or build fails.
"""
import argparse
from pathlib import Path
from PIL import Image

NAMES = ("studio","coast","bookshop") + tuple(
    f"{route}-{pose}" for route in ("mio","nagi","shiori") for pose in range(3)
)
MAX_TOTAL_BYTES = 96 * 1024 * 1024

def write(site: Path, output: Path):
    source=site/"assets"/"retro"
    images=[]
    offset=0
    payload=output/"summer_art.bin"
    header=output/"summer_art.h"
    output.mkdir(parents=True,exist_ok=True)
    with payload.open("wb") as stream:
        for name in NAMES:
            file=source/f"{name}.webp"
            if not file.is_file():
                raise FileNotFoundError(f"Original game artwork missing: {file}")
            with Image.open(file) as image:
                # Preserve the exact RGBA pixel values of the source WebP decode.
                frame=image.convert("RGBA")
                width,height=frame.size
                if not (1<=width<=4096 and 1<=height<=4096):
                    raise ValueError("Invalid original artwork dimensions")
                raw=frame.tobytes()
            if offset+len(raw)>MAX_TOTAL_BYTES:
                raise ValueError("Original art exceeds the safe kernel asset size")
            images.append((name,offset,width,height))
            stream.write(raw)
            offset+=len(raw)
    with header.open("w",encoding="utf-8") as h:
        h.write("/* Generated from original decoded WebP pixels; do not edit. */\n")
        h.write("#ifndef MAYGAME_PACKED_ART_H\n#define MAYGAME_PACKED_ART_H\n")
        h.write("typedef struct { unsigned int offset,width,height; } MayGameArtMeta;\n")
        h.write("static const MayGameArtMeta maygame_original_art[12]={\n")
        for name,offset,width,height in images:
            h.write(f"    {{{offset}u,{width}u,{height}u}}, /* {name} */\n")
        h.write("};\n#endif\n")
    print(f"Packed {len(images)} complete original assets, {offset} bytes of lossless RGBA")
    print(f"Binary: {payload} | Manifest: {header}")

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--site",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args()
    write(args.site.resolve(),args.output.resolve())
