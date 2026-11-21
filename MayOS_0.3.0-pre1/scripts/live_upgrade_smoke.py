#!/usr/bin/env python3
"""Exercise the A/B launcher against two published MayKernel ISOs."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from dual_boot_smoke import BUILD, BootVM
from live_upgrade import digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("old", type=Path)
    parser.add_argument("new", type=Path)
    args = parser.parse_args()
    old, new = args.old.resolve(), args.new.resolve()
    qemu = "qemu-system-x86_64"
    BUILD.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mayos-upgrade-") as temp:
        root = Path(temp)
        disk, slots = root / "data.img", root / "slots"
        with disk.open("xb") as handle:
            handle.truncate(8 * 1024 * 1024)
        launcher = [sys.executable, str(Path(__file__).with_name("live_upgrade.py")),
                    "--directory", str(slots)]

        def shell_boot(image, label, commands):
            vm = BootVM(iso=image, disk=disk, tag=label, qemu=qemu,
                        memory=256, timeout=60)
            try:
                vm.wait("MayOS: desktop ready")
                vm.key("f1")
                for command, result in commands:
                    vm.keyboard_command(command)
                    vm.wait(result)
            finally:
                vm.stop()

        def selected():
            return json.loads(subprocess.check_output([*launcher, "status"]))

        shell_boot(old, "live-upgrade-seed", [
            ("write keep.txt old-data", "\nOK\n"),
            ("theme light", "\n"),
        ])
        original = digest(disk)
        subprocess.run([*launcher, "init", "--disk", str(disk), "--iso", str(old),
                        "--version", "0.5.0-pre1", "--sha256", digest(old)], check=True)
        before = selected()

        def upgrade(version, sha, timeout="20"):
            return subprocess.run([*launcher, "upgrade", "--iso", str(new),
                                   "--version", version, "--sha256", sha,
                                   "--timeout", timeout], capture_output=True, text=True)

        bad = upgrade("0.5.0-pre2", "0" * 64)
        assert bad.returncode and "SHA-256 does not match" in bad.stderr, bad
        assert selected() == before and digest(disk) == original
        wrong_version = upgrade("0.5.0-pre99", digest(new))
        assert wrong_version.returncode and "Trial boot failed" in wrong_version.stderr, wrong_version
        assert "MayOS: desktop ready" in (slots / "last-trial.log").read_text()
        assert selected() == before and digest(disk) == original
        print("PASS: invalid digest and failed trial preserve active ISO and every disk byte")

        ok = upgrade("0.5.0-pre2", digest(new), "60")
        assert ok.returncode == 0, ok
        assert selected()["active"]["slot"] == "B" and digest(disk) == original
        shell_boot(slots / "B.iso", "live-upgrade-new", [
            ("cat keep.txt", "\nold-data\n"),
            ("write new.txt new-data", "\nOK\n"),
        ])
        after = digest(disk)
        subprocess.run([*launcher, "rollback"], check=True)
        assert selected()["active"]["slot"] == "A" and digest(disk) == after
        shell_boot(slots / "A.iso", "live-upgrade-rollback", [
            ("cat keep.txt", "\nold-data\n"),
            ("cat new.txt", "\nnew-data\n"),
        ])
        print("PASS: upgrade pre1 to pre2 and roll back with both versions' files intact")


if __name__ == "__main__":
    main()
