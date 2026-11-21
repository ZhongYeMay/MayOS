# 夏日未寄 (Natsu no Tayori) — MayOS native adaptation

Source of truth: https://github.com/ZhongYeMay/galgame
Web version: https://zhongyemay.github.io/galgame/
Target: MayOS MayKernel (32-bit freestanding x86) and MayOS Linux (64-bit static musl framebuffer desktop).

## Compatibility decision

The existing game is a web application, not a static HTML document. Its
`site/game.js` relies on DOM, localStorage, Web Audio, timers, CSS animations,
fullscreen, and image decoding. MayOS's built-in browser currently supports
limited HTTP text content, **not** a JavaScript/CSS/WebP browser engine.
Simply adding a URL, copying the HTML into an ISO, or linking to GitHub Pages
would not make the game playable on either kernel.

The correct shared solution is a native game module compiled into the MayOS
GUI on both targets, reusing the existing common `kernel/gui.c` frontend.

## Source and assets

- `site/story.js`: three adult-character routes and six endings; canonical
  dialogue and branching source.
- `site/game.js`: save/load, seen lines, settings, dialogue history,
  autoplay, skip-read, BGM logic.
- `site/style.css` and `site/index.html`: visual and interaction reference.
- `site/assets/retro/*.webp`: background and character bitmaps.

For reproducible builds, pin the galgame repository to a specific commit,
record the commit hash and asset hashes, and do not silently download moving
`main` during release builds. Check asset redistribution terms before
bundling. Never replace original game art with placeholders.

## Native port stages and acceptance criteria

1. **Content export**: create a deterministic exporter that reads story data
   from the pinned original source, emits a C-compatible dialogue graph and
   UTF-8 strings, and validates all branch destinations and six endings.
   Test that exported lines, routes, and choices match the web original.
2. **Common game module**: implement a kernel-independent story state
   machine (title, route selection, dialogue, choices, history, endings),
   input event API, save/load serialization and bounds checks. Host-side
   tests must exercise all six endings and save/resume at a choice.
3. **Rendering**: decode WebP art at build time to a framebuffer-friendly
   format, preserving the original images. Render backgrounds, sprites,
   choice buttons, dialogue and menus using the shared MayOS renderer.
   Implement 16:9 letterboxing and keyboard/mouse interactions.
4. **Storage**: MayKernel uses its native persistent disk/MayFS layer;
   Linux uses an explicitly writable persistent filesystem (the initramfs
   alone is volatile). Verify that saves survive reboot on both.
5. **Audio**: port original BGM to an available PCM output backend.
   If MayKernel has no audio driver, show an explicit unavailable state
   rather than a nonfunctional music toggle.
6. **Desktop integration**: add a Galgame launcher/app entry to both desktop
   variants. Avoid an external-browser dependency.
7. **CI / QEMU**: compile both targets, test route navigation, choices,
   save/reload, graphics and mouse/keyboard, capture actual QEMU screenshots
   and video. Publish a new release only after both boot and game tests pass.

## Current status

This document records the source audit and integration requirements. It is
**not** a playable native port. No MayOS ISO should be labeled as supporting
the Galgame until the above functionality has been implemented and tested.
