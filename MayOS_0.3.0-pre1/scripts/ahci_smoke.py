#!/usr/bin/env python3
"""Verify the generic AHCI DMA backend and MayFS across a cold reboot."""
from smoke import BUILD, VM, entries


def main():
    disk = BUILD / "test-ahci.img"
    with disk.open("wb") as output:
        output.truncate(8 * 1024 * 1024)

    vm = VM(disk, "test-ahci-first", machine="q35", storage="ahci")
    try:
        vm.wait("MayOS: ACPI MCFG ranges=1")
        vm.wait("MayOS: AHCI generic SATA disk ready (LBA48 DMA)")
        vm.key("f1")
        vm.cmd("write ahci.txt standards-based-dma")
        vm.cmd("cat ahci.txt")
        vm.wait("\nstandards-based-dma\n")
        assert entries(disk)["ahci.txt"][3] == "standards-based-dma"
    finally:
        vm.stop()

    vm = VM(disk, "test-ahci-reboot", machine="q35", storage="ahci")
    try:
        vm.wait("MayOS: AHCI generic SATA disk ready (LBA48 DMA)")
        vm.wait("MayOS: MayFS mounted")
        vm.key("f1")
        vm.cmd("cat ahci.txt")
        vm.wait("\nstandards-based-dma\n")
    finally:
        vm.stop()
    print("PASS: generic AHCI LBA48 DMA write, flush and cold-reboot read")


if __name__ == "__main__":
    main()
