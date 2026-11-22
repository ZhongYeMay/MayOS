#!/usr/bin/env python3
"""Exercise MayOS language selection on a real, newly installed UEFI disk."""
import argparse
from contextlib import contextmanager
from pathlib import Path
import shutil
import tempfile

from dual_boot_smoke import BootVM
from install_mayos import SETUP_PROMPTS, install
from live_upgrade import digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", required=True, type=Path)
    parser.add_argument("--legacy-iso", type=Path)
    parser.add_argument("--dual-iso", type=Path)
    parser.add_argument("--ovmf-code", type=Path, default=Path("/usr/share/OVMF/OVMF_CODE_4M.fd"))
    parser.add_argument("--ovmf-vars", type=Path, default=Path("/usr/share/OVMF/OVMF_VARS_4M.fd"))
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="mayos-languages-") as directory:
        root = Path(directory)
        disk = root / "mayos.img"
        install(args.iso.resolve(), digest(args.iso), disk)

        @contextmanager
        def boot(tag, image=disk, media=None):
            variables = root / f"{tag}.fd"
            shutil.copyfile(args.ovmf_vars, variables)
            vm = BootVM(iso=media, disk=image, tag=tag, qemu="qemu-system-x86_64",
                        memory=512, timeout=55, ovmf_code=args.ovmf_code, ovmf_vars=variables)
            try:
                yield vm
            finally:
                vm.stop()

        def shell(vm, text, reply):
            offset = len(vm.text)
            vm.keyboard_command(text)
            vm.wait(reply, offset=offset)

        def language_settings(vm):
            vm.click(595, 674)
            vm.click(357, 526)

        with boot("multilingual-oobe") as vm:
            vm.wait(SETUP_PROMPTS[0])
            vm.key("ret")
            vm.wait(SETUP_PROMPTS[1])
            vm.shot("multilingual-oobe-picker")
            vm.key("down")
            vm.key("right")
            vm.key("ret")
            vm.wait("MayOS: first boot display language selected: ja-JP")
            vm.wait(SETUP_PROMPTS[2])
            vm.shot("multilingual-oobe-japanese")
            for prompt in SETUP_PROMPTS[3:]:
                vm.key("ret")
                vm.wait(prompt)
            vm.key("ret")
            vm.wait("MayOS: first boot setup completed")
            vm.wait("MayOS: desktop ready")
            vm.key("f1")
            shell(vm, "write keep.txt japanese-setup", "\nOK\n")
        print("PASS: keyboard language selection through real UEFI first boot", flush=True)

        with boot("multilingual-switch") as vm:
            vm.wait("MayOS: display language loaded: ja-JP")
            vm.wait("MayOS: desktop ready")
            language_settings(vm)
            vm.click(554, 413)  # French, fourth row, left
            vm.key("esc")
            offset = len(vm.text)
            vm.key("ret")
            assert "display language applied" not in vm.text[offset:]
            vm.click(550, 414)
            vm.shot("multilingual-settings-candidate")
            vm.key("ret")
            vm.wait("MayOS: display language applied: fr-FR")
            vm.shot("multilingual-settings-french")
        with boot("multilingual-cold") as vm:
            vm.wait("MayOS: display language loaded: fr-FR")
            vm.wait("MayOS: desktop ready")
            vm.key("f1")
            shell(vm, "cat keep.txt", "\njapanese-setup\n")
        print("PASS: cancel, apply, cold-boot persistence and intact user files", flush=True)

        if args.legacy_iso:
            old_disk = root / "old.img"
            install(args.legacy_iso.resolve(), digest(args.legacy_iso), old_disk)
            with boot("multilingual-legacy-seed", old_disk) as vm:
                for prompt in SETUP_PROMPTS:
                    vm.wait(prompt)
                    vm.key("ret")
                vm.wait("MayOS: desktop ready")
                vm.key("f1")
                shell(vm, "write old.txt prior-version", "\nOK\n")
            with boot("multilingual-legacy-read", old_disk, args.iso.resolve()) as vm:
                vm.wait("MayOS: desktop preferences applied")
                log = vm.wait("MayOS: desktop ready")
                assert "first boot language ready" not in log
                assert "display language loaded" not in log
                vm.key("f1")
                shell(vm, "cat old.txt", "\nprior-version\n")
            print("PASS: 0.5.1 preferences and files load without opening setup again", flush=True)

        with boot("multilingual-invalid") as vm:
            vm.wait("MayOS: desktop ready")
            vm.key("f1")
            shell(vm, "rm /MayOS-Language.txt", "\nMoved to Trash\n")
            shell(vm, "write /MayOS-Language.txt unrelated-data", "\nOK\n")
        with boot("multilingual-protected") as vm:
            log = vm.wait("MayOS: desktop ready")
            assert "display language loaded" not in log
            language_settings(vm)
            vm.key("right")
            vm.key("ret")
            vm.key("f1")
            shell(vm, "cat /MayOS-Language.txt", "\nunrelated-data\n")
            assert "display language applied" not in vm.text
        print("PASS: unrelated files at the settings path are preserved", flush=True)

        if args.dual_iso:
            before = digest(disk)
            with boot("multilingual-linux", media=args.dual_iso.resolve()) as vm:
                vm.wait(" - desktop (x86_64)")
                vm.key("down")
                vm.key("ret")
                vm.wait("MayOS: desktop ready")
                vm.wait("MayOS-Linux# ")
                language_settings(vm)
                for key in ("down", "down", "down", "right", "ret"):
                    vm.key(key)
                vm.wait("MayOS: display language applied: de-DE")
                vm.shot("multilingual-settings-linux-german")
            assert digest(disk) == before
            print("PASS: Linux64 language switching is session-only and leaves MayFS intact", flush=True)


if __name__ == "__main__":
    main()
