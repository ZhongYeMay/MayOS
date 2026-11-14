#!/usr/bin/env python3
"""Original 48px supersampled antialiased MayOS Dock icons.

Generated offline using Pillow, then composited by the native framebuffer.
No Apple icons, UI screenshots, fonts or network access are used at runtime.
"""
from pathlib import Path
from PIL import Image, ImageDraw
import math
SIZE, SCALE = 48, 4
OUT = Path(__file__).resolve().parents[1] / "build" / "dock_icons.h"

def raster(render):
    im = Image.new("L", (SIZE*SCALE, SIZE*SCALE))
    draw = ImageDraw.Draw(im)
    def pt(x,y): return round(x*SCALE),round(y*SCALE)
    def line(points,w=2.5):
        points=[pt(x,y) for x,y in points]
        draw.line(points,fill=255,width=round(w*SCALE),joint="curve")
        r=round(w*SCALE/2)
        for x,y in points:
            draw.ellipse((x-r,y-r,x+r,y+r),fill=255)
    def box(x,y,xx,yy,r,w=2.4):
        draw.rounded_rectangle((*pt(x,y),*pt(xx,yy)),radius=round(r*SCALE),outline=255,width=round(w*SCALE))
    def ring(x,y,r,w=2.4):
        draw.ellipse((*pt(x-r,y-r),*pt(x+r,y+r)),outline=255,width=round(w*SCALE))
    render(line,box,ring)
    bitmap=im.resize((SIZE,SIZE),Image.Resampling.LANCZOS).tobytes()
    assert len(bitmap)==2304 and sum(bitmap)>7500
    return bitmap

def folder(line,box,ring):
    box(5,17,43,39,5,2.6)
    line([(7,17),(9,12),(21,12),(25,17)],2.6)
    line([(8,22),(40,22)],1.5)

def settings(line,box,ring):
    vertices=[]
    for n in range(48):
        angle=-math.pi/2+n*math.tau/48
        radius=18.5 if n%6 in (1,2,3) else 14.8
        vertices.append((24+math.cos(angle)*radius,24+math.sin(angle)*radius))
    line(vertices+[vertices[0]],2.25)
    ring(24,24,8.1,2.6)
    ring(24,24,2,1.4)

def terminal(line,box,ring):
    line([(10,14),(20,24),(10,34)],3)
    line([(24,34),(39,34)],3)

def trash(line,box,ring):
    line([(9,14),(39,14)],2.6)
    line([(19,10),(29,10)],2.6)
    box(13,17,35,40,4,2.6)
    line([(21,21),(21,34)],1.8)
    line([(27,21),(27,34)],1.8)

if __name__=="__main__":
    icons=[raster(fn) for fn in (folder,settings,terminal,trash)]
    OUT.parent.mkdir(parents=True,exist_ok=True)
    with OUT.open("w",encoding="utf-8") as stream:
        stream.write("/* 4 original antialiased MayOS Dock glyphs; 48px alpha only. */\n")
        stream.write("static const unsigned char mayos_dock_alpha[4][2304] = {\n")
        for glyph in icons:
            stream.write("{"+",".join(map(str,glyph))+"},\n")
        stream.write("};\n")
    print("PASS: generated 4 high-resolution Dock glyphs",flush=True)
