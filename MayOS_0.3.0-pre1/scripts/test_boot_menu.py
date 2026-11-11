#!/usr/bin/env python3
"""Keep the normal and emergency GRUB menu entries compatible."""
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BOOT = ROOT / "boot/grub"


class GrubRecoveryMenuTests(unittest.TestCase):
    def test_safe_entry_is_last_in_both_images(self):
        for filename in ("grub.cfg", "dual.cfg.in"):
            with self.subTest(filename=filename):
                config = (BOOT / filename).read_text()
                self.assertEqual(config.count("--id mayos-safe"), 1)
                entry = config.split("--id mayos-safe {", 1)[1].split("}", 1)[0]
                self.assertIn("set gfxmode=640x480x32,800x600x16,auto", entry)
                self.assertIn("multiboot /boot/mayos.elf", entry)
                # GRUB's multiboot loader resets the framebuffer request.
                self.assertGreater(
                    entry.rfind("set gfxpayload=$gfxmode"),
                    entry.index("multiboot /boot/mayos.elf"),
                )
                self.assertNotIn("menuentry", config.split("--id mayos-safe {", 1)[1].split("}", 1)[1])

    def test_default_entry_and_long_mode_gate_are_unchanged(self):
        dual = (BOOT / "dual.cfg.in").read_text()
        self.assertIn("set default=mayos", dual)
        self.assertLess(dual.index("--id mayos {"), dual.index("--id mayos-safe {"))
        self.assertLess(dual.index("if cpuid -l; then"), dual.index("--id linux {"))
        self.assertLess(dual.index("\nfi\n"), dual.index("--id mayos-safe {"))
        self.assertIn("Linux @LINUX_VERSION@ - rescue shell", dual)

    def test_serial_recovery_available_on_standalone(self):
        standalone = (BOOT / "grub.cfg").read_text()
        self.assertIn("serial --unit=0 --speed=115200", standalone)
        self.assertIn("terminal_input console serial", standalone)
        self.assertIn("terminal_output console serial", standalone)

    def test_mode_matrix_restores_standalone_iso(self):
        script = (ROOT / "scripts/boot_compat_test.py").read_text()
        self.assertIn('r"^set gfxmode=.*$"', script)
        self.assertIn("count=1, flags=re.MULTILINE", script)
        self.assertIn('subprocess.run(["make", "build/mayos.iso"]', script)


if __name__ == "__main__":
    unittest.main()
