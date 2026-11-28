#!/usr/bin/env python3
"""Check DHCP and native DNS on a subnet different from the TCP fixtures."""
from pathlib import Path
import shutil
import tempfile

from dual_boot_smoke import BootVM, BUILD


def main():
    qemu = shutil.which("qemu-system-x86_64")
    if not qemu:
        raise RuntimeError("QEMU is required")
    with tempfile.TemporaryDirectory(prefix="mayos-routed-dns-") as temporary:
        work = Path(temporary)
        disk = work / "isolated.img"
        with disk.open("xb") as stream:
            stream.truncate(8 * 1024 * 1024)
        for adapter in ("e1000", "rtl8139"):
            launcher = work / adapter
            config = f"user,model={adapter},net=172.31.44.0/24,host=172.31.44.9,dns=172.31.44.53,dhcpstart=172.31.44.60"
            launcher.write_text(f"""#!/usr/bin/env python3
import os
import sys
arguments = sys.argv[1:]
arguments[arguments.index('-nic') + 1] = {config!r}
os.execv({qemu!r}, [{qemu!r}] + arguments)
""")
            launcher.chmod(0o755)
            vm = BootVM(iso=BUILD / "mayos.iso", disk=disk, tag=f"routed-dns-{adapter}",
                        qemu=str(launcher), memory=512, timeout=45)
            try:
                vm.wait("MayOS: desktop ready")
                vm.wait("MayOS: DHCP ACK accepted; lease acquired 172.31.44.60")
                vm.key("f1")
                offset = len(vm.text)
                vm.keyboard_command("net resolve localhost")
                vm.wait("MayOS: DHCP DNS A query sent to leased DNS server", offset=offset)
                log = vm.wait("MayOS: DHCP DNS server returned a matched A record", offset=offset)
                assert "Ethernet ARP reply received from QEMU gateway 10.0.2.2" not in log
                print(f"PASS: {adapter} resolves through the DHCP resolver on 172.31.44.0/24", flush=True)
            finally:
                vm.stop()


if __name__ == "__main__":
    main()
