#!/usr/bin/env python3
"""Map the fixed 1024x768 desktop reference to the real GRUB framebuffer.

MayKernel presents its original logical 4:3 canvas with aspect-preserving
letterboxing on 16:9 firmware modes. Pixel tests must sample that viewport,
not assume QEMU still captures 1024x768 after the boot-mode change.
"""

LOGICAL_WIDTH, LOGICAL_HEIGHT = 1024, 768
SUPPORTED_OUTPUTS = {(1024, 768), (1280, 720)}


def reference_pixel(image, x, y):
    """Return the output pixel covering a logical MayOS desktop coordinate."""
    width, height = image.size
    assert (width, height) in SUPPORTED_OUTPUTS, (
        f"Unexpected firmware framebuffer {width}x{height}; "
        "review GRUB mode policy and the UI reference before accepting it"
    )
    assert 0 <= x < LOGICAL_WIDTH and 0 <= y < LOGICAL_HEIGHT
    viewport_width = width
    viewport_height = viewport_width * LOGICAL_HEIGHT // LOGICAL_WIDTH
    if viewport_height > height:
        viewport_height = height
        viewport_width = viewport_height * LOGICAL_WIDTH // LOGICAL_HEIGHT
    left = (width - viewport_width) // 2
    top = (height - viewport_height) // 2
    physical_x = left + (x * viewport_width + LOGICAL_WIDTH - 1) // LOGICAL_WIDTH
    physical_y = top + (y * viewport_height + LOGICAL_HEIGHT - 1) // LOGICAL_HEIGHT
    return image.getpixel((physical_x, physical_y))


def verify_letterboxing(image):
    """Widescreen output must preserve the approved 4:3 proportions."""
    width, height = image.size
    assert (width, height) in SUPPORTED_OUTPUTS, image.size
    if width * LOGICAL_HEIGHT > height * LOGICAL_WIDTH:
        assert image.getpixel((0, height // 2)) == (0, 0, 0)
        assert image.getpixel((width - 1, height // 2)) == (0, 0, 0)
