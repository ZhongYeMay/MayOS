#!/usr/bin/env python3
"""Exercise the DHCP hook without changing the host network."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    commands = temp / "commands.log"
    for name in ("ip", "ifconfig"):
        executable = temp / name
        executable.write_text(
            "#!/bin/sh\nprintf '%s\\n' \"$0 $*\" >> \"$MAYOS_COMMAND_LOG\"\n",
            encoding="utf-8",
        )
        executable.chmod(0o755)
    resolver = temp / "resolv.conf"
    environment = os.environ | {
        "PATH": f"{temp}:/usr/bin:/bin",
        "MAYOS_COMMAND_LOG": str(commands),
        "MAYOS_RESOLV_CONF": str(resolver),
        "interface": "eth0",
        "ip": "10.0.2.15",
        "subnet": "255.255.255.0",
        "router": "bad 10.0.2.2",
        "dns": "10.0.2.3 invalid;value 1.1.1.1",
    }
    subprocess.run(["sh", str(ROOT / "linux/dhcp-hook"), "bound"], env=environment, check=True)
    assert resolver.read_text(encoding="utf-8") == "nameserver 10.0.2.3\nnameserver 1.1.1.1\n"
    log = commands.read_text(encoding="utf-8")
    assert "ifconfig eth0 10.0.2.15 netmask 255.255.255.0 up" in log
    assert "ip route add default via 10.0.2.2 dev eth0" in log
    assert "invalid;value" not in log

    subprocess.run(["sh", str(ROOT / "linux/dhcp-hook"), "deconfig"], env=environment, check=True)
    assert resolver.read_text(encoding="utf-8") == ""

print("PASS: DHCP lease, route, DNS filtering and deconfiguration")
