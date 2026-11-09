# 夏日未寄 / Summer Letters — MayOS Native Port

Original game: https://github.com/ZhongYeMay/galgame
Online version: https://zhongyemay.github.io/galgame/
MayOS project: https://github.com/ZhongYeMay/MayOS
Upstream source commit: fa34344a1cff46025e8d169f56411e290e9fafbd

## Shared MayKernel and Linux features

The native game works inside the MayOS framebuffer desktop; it does not depend on JavaScript, a web renderer, or a network connection at runtime.
All 97 original story nodes, three story routes, six endings, and all twelve AI-generated WebP background and character images are converted into native text and indexed images at build time.
Supports real mouse and keyboard interactions, branching dialogue, three save/load slots, backlog, autoplay, unlocked endings gallery, desktop launcher, and movable game window.

MayKernel/x86 stores game progress in MayFS on a writable QEMU disk and can restore it after a cold reboot.
MayOS Linux/x86_64 uses the same game application but its GUI filesystem is session-RAM only. Linux game saves do not survive reboot.

## Controls

Click the 夏日未寄 desktop icon or choose it in Applications. Space, Enter, or mouse click advances dialogue; 1-3 choose a branch.
Use S for the save-slot panel, L to load, B for backlog, A to toggle autoplay, M for title, and Escape to close an overlay or return to the title.

## Building and testing

Prerequisites: Python 3, Pillow, Node.js 22, Noto CJK/DejaVu fonts, GCC multilib, GRUB, xorriso, QEMU. The Linux ISO additionally needs musl, BusyBox, flex, bison, bc, and other Linux build dependencies.

From directory MayOS_0.3.0-pre1:

    make all
    make test-galgame
    make dual LINUX_VERSION=7.3.1 LINUX_JOBS=4
    make test-galgame-dual LINUX_VERSION=7.3.1

Built files: build/mayos.iso (MayKernel) and build/mayos-dual.iso (MayKernel and Linux choices). See GitHub Actions verification before considering any ISO validated.

## Build authenticity and constraints

scripts/galgame_assets.py fetches only the original pinned story and WebP images, checks their Git blob hashes, converts art to eight-bit indexed framebuffer images, and generates the game's native story arrays.
The generated game is entirely playable without an internet connection, even on MayKernel.

Audio/WebAudio background music is not implemented, nor is the original browser's HTML/CSS layout and detailed animation. The artwork is resampled/quantized. The optional mature epilogues are not presented; all six standard endings remain.
This is an experimental QEMU operating system; physical hardware has not been tested.
