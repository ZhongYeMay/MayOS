#!/usr/bin/env python3
"""Fast unit regressions for the UEFI Linux desktop readiness gate."""
import unittest
from unittest.mock import Mock

from uefi_smoke import verify_linux_desktop


class UefiDesktopReadinessTests(unittest.TestCase):
    def test_missing_serial_backend_line_does_not_mask_healthy_guest(self):
        vm = Mock()
        # The backend line is deliberately absent from the serial transcript:
        # only the GUI marker and independent guest log/process checks count.
        vm.text = "MayOS Linux: ready\nMayOS: desktop ready\nMayOS-Linux# "
        verify_linux_desktop(vm)
        self.assertEqual(
            [call.args[0] for call in vm.wait.call_args_list],
            ["MayOS: desktop ready", "MayOS-Linux# "],
        )
        command, token = vm.shell_check.call_args.args
        self.assertEqual(token, "UEFI_BACKEND_READY")
        self.assertIn("/run/mayos-desktop.log", command)
        self.assertIn("MayOS Linux desktop: backend ready;", command)
        self.assertIn("/run/mayos-desktop.pid", command)
        self.assertIn("/proc/", command)

    def test_guest_log_or_process_failure_is_not_ignored(self):
        vm = Mock()
        vm.shell_check.side_effect = AssertionError("backend evidence missing")
        with self.assertRaisesRegex(AssertionError, "backend evidence missing"):
            verify_linux_desktop(vm)

    def test_guest_desktop_marker_still_required(self):
        vm = Mock()
        vm.wait.side_effect = TimeoutError("desktop never started")
        with self.assertRaisesRegex(TimeoutError, "desktop never started"):
            verify_linux_desktop(vm)
        vm.shell_check.assert_not_called()


if __name__ == "__main__":
    unittest.main()
