#!/usr/bin/env python3
"""Compile the supplied photo into RGB565 pixels for the freestanding kernel.

This is asset format conversion, preserving the full photo's aspect ratio.
No JPEG decoder or external image service is required at runtime.
"""
from pathlib import Path
from PIL import Image,ImageOps
root=Path(__file__).resolve().parents[1]
with Image.open(root/'assets/easter-cat.jpg') as original:
    photo=ImageOps.exif_transpose(original).convert('RGB')
    photo.thumbnail((360,480),Image.Resampling.LANCZOS)
    width,height=photo.size
    raw=photo.tobytes()
    values=[((r>>3)<<11)|((g>>2)<<5)|(b>>3) for r,g,b in zip(raw[0::3],raw[1::3],raw[2::3])]
out=root/'build/cat_image.h';out.parent.mkdir(exist_ok=True)
with out.open('w') as f:
    f.write('/* Generated from assets/easter-cat.jpg by scripts/photo.py. */\n')
    f.write(f'#define CAT_W {width}\n#define CAT_H {height}\n')
    f.write('static const unsigned short cat_rgb565[CAT_W*CAT_H]={\n')
    for i in range(0,len(values),24):f.write(','.join(f'0x{v:04x}' for v in values[i:i+24])+',\n')
    f.write('};\n')
print(f'photo: {width} x {height}, RGB565 -> {out}')
