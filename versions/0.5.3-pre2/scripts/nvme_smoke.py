#!/usr/bin/env python3
"""Verify generic NVMe queues, NVM I/O, flush and cold persistence."""
from smoke import BUILD, VM, entries


def main():
    disk = BUILD / "test-nvme.img"
    with disk.open("wb") as output:
        output.truncate(8 * 1024 * 1024)
    vm = VM(disk, "test-nvme-first", machine="q35", storage="nvme")
    try:
        vm.wait("MayOS: NVMe generic namespace ready")
        vm.key("f1")
        vm.cmd("write nvme.txt standards-based-nvm")
        vm.cmd("cat nvme.txt")
        vm.wait("\nstandards-based-nvm\n")
        assert entries(disk)["nvme.txt"][3] == "standards-based-nvm"
    finally:
        vm.stop()
    vm = VM(disk, "test-nvme-reboot", machine="q35", storage="nvme")
    try:
        vm.wait("MayOS: NVMe generic namespace ready")
        vm.wait("MayOS: MayFS mounted")
        vm.key("f1")
        vm.cmd("cat nvme.txt")
        vm.wait("\nstandards-based-nvm\n")
    finally:
        vm.stop()
    print("PASS: generic NVMe queue I/O, flush and cold-reboot read")


if __name__ == "__main__":
    main()
