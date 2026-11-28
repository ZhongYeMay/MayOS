#!/usr/bin/env python3
"""Provision a fresh UEFI MayOS Linux disk or manage its A/B upgrade."""
import argparse
from contextlib import nullcontext
import fcntl
import getpass
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import sys
import subprocess
import tempfile
import uuid

MIB = 1024 * 1024
SECTOR = 512
SLOT_SIZE = 1536 * MIB
DATA_SIZE = 1024 * MIB
ESP_SIZE = 64 * MIB
ESP_TYPE = "c12a7328-f81f-11d2-ba4b-00a0c93ec93b"
LINUX_TYPE = "0fc63daf-8483-4772-8e79-3d69d8477de4"
NAMES = ("MAYOS-EFI", "MAYOS-A", "MAYOS-B", "MAYOS-HOME")
TOTAL = (3 * MIB + ESP_SIZE + 2 * SLOT_SIZE + DATA_SIZE)
ENV_PATH = "mayos-linux"
BUILD_VERSION = re.compile(r"(\d+)\.(\d+)\.(\d+)(?:-(pre|beta\.)(\d+))?")


def run(*args, **kwargs):
    return subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def output(*args):
    return run(*args, text=True, stdout=subprocess.PIPE).stdout.strip()


def version(path):
    value = path.read_text().strip()
    match = BUILD_VERSION.fullmatch(value)
    if not match:
        raise ValueError("invalid MayOS Linux release identifier")
    major, minor, patch, kind, number = match.groups()
    return tuple(map(int, (major, minor, patch))) + (("pre", "beta.", None).index(kind), int(number or 0))


def verified(bundle, expected, *, compressed=True):
    if not re.fullmatch("[0-9a-f]{64}", expected):
        raise ValueError("expected a lowercase SHA-256 from the release")
    fd = os.open(bundle, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
    try:
        info = os.fstat(fd)
        valid_size = 0 < info.st_size < SLOT_SIZE if compressed else info.st_size == SLOT_SIZE
        if not stat.S_ISREG(info.st_mode) or not valid_size:
            raise ValueError("unexpected system image type or size")
        with os.fdopen(os.dup(fd), "rb") as stream:
            if hashlib.file_digest(stream, "sha256").hexdigest() != expected:
                raise ValueError("system image SHA-256 mismatch")
        os.lseek(fd, 0, os.SEEK_SET)
        return fd
    except BaseException:
        os.close(fd)
        raise


class VerifiedStream:
    def __init__(self, stream):
        self.stream = stream
        self.hash = hashlib.sha256()

    def read(self, count=-1):
        data = self.stream.read(count)
        self.hash.update(data)
        return data


def copy_slot(payload, destination, expected, offset=0, *, sparse=False, compressed=True):
    """Write exactly one complete slot and verify its readback."""
    hasher = hashlib.sha256()
    count = 0
    zero = bytes(MIB)
    with os.fdopen(os.dup(payload), "rb") as stream:
        checked = VerifiedStream(stream)
        with (gzip.GzipFile(fileobj=checked, mode="rb") if compressed else nullcontext(checked)) as image:
            destination.seek(offset)
            for block in iter(lambda: image.read(MIB), b""):
                count += len(block)
                if count > SLOT_SIZE:
                    raise ValueError("system image exceeds the root slot size")
                hasher.update(block)
                if sparse and block == zero[:len(block)]:
                    destination.seek(len(block), os.SEEK_CUR)
                else:
                    destination.write(block)
        if count != SLOT_SIZE or checked.hash.hexdigest() != expected:
            raise ValueError("incorrect size or changed system image during write")
    destination.flush()
    os.fsync(destination.fileno())
    destination.seek(offset)
    actual = hashlib.sha256()
    for _ in range(SLOT_SIZE // MIB):
        block = destination.read(MIB)
        if len(block) != MIB:
            raise OSError("short read while verifying root slot")
        actual.update(block)
    if actual.digest() != hasher.digest():
        raise OSError("root slot readback failed")
    print("MayOS Linux: root image written and verified", flush=True)


def boot_config(ids, themed=False):
    disk, esp, a, b, home = (str(v) for v in ids)
    serial_number = ids[1].hex[:8].upper()
    fat_uuid = f"{serial_number[:4]}-{serial_number[4:]}"
    theme = 'source ($esp)/boot/grub/menu.cfg' if themed else ''
    return f"""insmod part_gpt
insmod fat
insmod ext2
insmod regexp
insmod loadenv
serial --unit=0 --speed=115200
terminal_input console serial
terminal_output console serial
search --fs-uuid --set=esp {fat_uuid}
set root=$esp
set timeout_style=menu
set timeout=5
{theme}
set chosen=A
regexp --set=1:drive '^(.*),gpt1$' "$esp"
load_env -f ($esp)/{ENV_PATH}/active.env active_slot
if [ "$active_slot" = B ]; then set chosen=B; fi
load_env -f ($esp)/{ENV_PATH}/trial.env trial_slot attempted_slot
if [ "$trial_slot" = A -o "$trial_slot" = B ]; then
    set attempted_slot=$trial_slot
    set trial_slot=
    if save_env -f ($esp)/{ENV_PATH}/trial.env attempted_slot trial_slot; then
        set chosen=$attempted_slot
    fi
fi
set default=$chosen
menuentry 'MayOS Linux64 - system A' --id A {{
    linux ($drive,gpt2)/boot/mayos-linux root=PARTUUID={a} rw quiet rootwait console=tty0 console=ttyS0,115200 mayos.disk={disk} mayos.esp={esp} mayos.data={home} mayos.slot=A
    initrd ($drive,gpt2)/boot/mayos-initrd
}}
menuentry 'MayOS Linux64 - system B' --id B {{
    linux ($drive,gpt3)/boot/mayos-linux root=PARTUUID={b} rw quiet rootwait console=tty0 console=ttyS0,115200 mayos.disk={disk} mayos.esp={esp} mayos.data={home} mayos.slot=B
    initrd ($drive,gpt3)/boot/mayos-initrd
}}
"""


def create_env(path, **values):
    run("grub-editenv", path, "create")
    if values:
        run("grub-editenv", path, "set", *(f"{key}={value}" for key, value in values.items()))
    if path.stat().st_size != 1024:
        raise ValueError("invalid GRUB environment block")


def stage_boot_theme(tree):
    project = Path(__file__).resolve().parent.parent.parent
    if not ((project / "boot/grub/menu.cfg").is_file() and
            (project / "scripts/dual_iso.py").is_file()):
        return False
    sys.path.insert(0, str(project / "scripts"))
    from dual_iso import stage_menu
    grub = tree / "boot" / "grub"
    grub.mkdir(parents=True)
    stage_menu(grub)
    return True


def build_esp(work, ids):
    tree = work / "esp"
    tables = tree / ENV_PATH
    boot = tree / "EFI" / "BOOT"
    tables.mkdir(parents=True)
    boot.mkdir(parents=True)
    (tables / f"disk-{ids[0]}").write_text(f"{ids[0]}\n")
    config = work / "grub.cfg"
    themed = stage_boot_theme(tree)
    config.write_text(boot_config(ids, themed))
    create_env(tables / "active.env", active_slot="A")
    create_env(tables / "trial.env")
    modules = "part_gpt fat ext2 search_fs_uuid regexp loadenv linux serial"
    if themed:
        modules += " all_video gfxterm gfxmenu png cpuid test"
    run("grub-mkstandalone", "-O", "x86_64-efi", "--locales=", "--fonts=",
        f"--modules={modules}",
        "-o", boot / "BOOTX64.EFI", f"boot/grub/grub.cfg={config}")
    image = work / "esp.img"
    with image.open("xb") as stream:
        stream.truncate(ESP_SIZE)
    run("mkfs.fat", "-F", "32", "-i", ids[1].hex[:8], "-n", "MAYOSEFI", image, stdout=subprocess.DEVNULL)
    paths = [tree / "EFI", tables]
    if themed:
        paths.extend([tree / "boot", tree / "licenses"])
    run("mcopy", "-i", image, "-s", *paths, "::/")
    return image


def admin_hash():
    first = getpass.getpass("Set the local MayOS administrator password: ")
    second = getpass.getpass("Confirm the administrator password: ")
    if len(first) < 10 or first != second or "\n" in first:
        raise ValueError("use at least ten characters and enter the same password twice")
    return run("openssl", "passwd", "-6", "-stdin", text=True, input=first + "\n",
               stdout=subprocess.PIPE).stdout.strip()


def data_image(work, password):
    tree = work / "data"
    state = tree / ".mayos-system"
    state.mkdir(parents=True, mode=0o700)
    if password:
        (state / "admin.hash").write_text(password + "\n")
        (state / "admin.hash").chmod(0o600)
    path = work / "home.img"
    run("mke2fs", "-q", "-t", "ext4", "-d", tree, "-L", "MayOSData", path, f"{DATA_SIZE // MIB}M")
    for inode in ("/.mayos-system", "/.mayos-system/admin.hash") if password else ("/.mayos-system",):
        for field in ("uid", "gid"):
            run("debugfs", "-w", "-R", f"set_inode_field {inode} {field} 0", path,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return path


def copy_small(source, destination, offset):
    destination.seek(offset)
    zero = bytes(MIB)
    with source.open("rb") as stream:
        for block in iter(lambda: stream.read(MIB), b""):
            if block == zero[:len(block)]:
                destination.seek(len(block), os.SEEK_CUR)
            else:
                destination.write(block)


def partition_image(path, capacity, ids):
    starts = []
    start = MIB
    table = ["label: gpt", f"label-id: {ids[0]}", "unit: sectors", ""]
    for name, length, guid, kind in zip(NAMES, (ESP_SIZE, SLOT_SIZE, SLOT_SIZE, DATA_SIZE),
                                         ids[1:], (ESP_TYPE, LINUX_TYPE, LINUX_TYPE, LINUX_TYPE)):
        table.append(f"start={start // SECTOR},size={length // SECTOR},type={kind},uuid={guid},name={name}")
        starts.append(start)
        start += length
    with path.open("r+b") as image:
        image.truncate(capacity)
    run("sfdisk", path, input="\n".join(table) + "\n", text=True, stdout=subprocess.DEVNULL)
    run("sfdisk", "--verify", path, stdout=subprocess.DEVNULL)
    return starts


def media_esp(work, ids, media):
    volume = work / "esp.img"
    with volume.open("xb") as image:
        image.truncate(ESP_SIZE)
    run("mkfs.fat", "-F", "32", "-i", ids[1].hex[:8], "-n", "MAYOSEFI",
        volume, stdout=subprocess.DEVNULL)
    mount = work / "esp"
    mount.mkdir()
    run("mount", "-t", "vfat", "-o",
        "loop,nosuid,nodev,noexec,iocharset=iso8859-1,codepage=437,quiet", volume, mount)
    try:
        shutil.copytree(media / "disk-efi", mount / "EFI")
        shutil.copytree(media / "boot" / "grub" / "fonts", mount / "boot" / "grub" / "fonts")
        shutil.copytree(media / "boot" / "grub" / "themes", mount / "boot" / "grub" / "themes")
        shutil.copyfile(media / "boot" / "grub" / "menu.cfg", mount / "boot" / "grub" / "menu.cfg")
        tables = mount / ENV_PATH
        tables.mkdir()
        (tables / f"disk-{ids[0]}").write_text(f"{ids[0]}\n")
        (tables / "grub.cfg").write_text(boot_config(ids, themed=True))
        create_env(tables / "active.env", active_slot="A")
        create_env(tables / "trial.env")
    finally:
        run("umount", mount)
    return volume


def empty_target(name, confirmed):
    if os.geteuid() != 0:
        raise ValueError("writing a physical disk requires local administrator access")
    target = Path(name).resolve(strict=True)
    if str(target) != confirmed or target.parent != Path("/dev"):
        raise ValueError("confirm the exact resolved whole-disk device path")
    if output("lsblk", "-nr", "-o", "TYPE", target) != "disk":
        raise ValueError("destination must be a whole disk with no partitions")
    if output("blockdev", "--getss", target) != str(SECTOR):
        raise ValueError("the preview requires 512-byte logical sectors")
    capacity = int(output("blockdev", "--getsize64", target))
    if capacity < TOTAL:
        raise ValueError("destination needs at least 4.1 GiB")
    return target, capacity


def ensure_empty(disk, target):
    if not stat.S_ISBLK(os.fstat(disk.fileno()).st_mode):
        raise ValueError("destination is not a block device")
    print(f"Scanning {target} for existing data before installation", flush=True)
    zero = bytes(4 * MIB)
    for block in iter(lambda: disk.read(len(zero)), b""):
        if block != zero[:len(block)]:
            raise ValueError("destination has existing data; left untouched")


def verify_partition(source, disk, offset):
    disk.seek(offset)
    with source.open("rb") as image:
        for block in iter(lambda: image.read(MIB), b""):
            if disk.read(len(block)) != block:
                raise OSError(f"{source.name} readback failed")


def install_media(args):
    target, capacity = empty_target(args.target, args.confirm)
    payload = verified(args.bundle, args.sha256, compressed=False)
    ids = [uuid.uuid4() for _ in range(5)]
    try:
        fd = os.open(target, os.O_RDWR | os.O_EXCL | os.O_CLOEXEC)
        with os.fdopen(fd, "r+b") as disk, tempfile.TemporaryDirectory() as scratch:
            ensure_empty(disk, target)
            password = admin_hash()
            work = Path(scratch)
            table = work / "gpt.img"
            table.touch()
            starts = partition_image(table, capacity, ids)
            esp = media_esp(work, ids, args.media)
            home = data_image(work, password)
            print("Writing and verifying the MayOS Linux64 system", flush=True)
            copy_slot(payload, disk, args.sha256, starts[1], sparse=True, compressed=False)
            copy_small(esp, disk, starts[0])
            copy_small(home, disk, starts[3])
            disk.flush()
            os.fsync(fd)
            verify_partition(esp, disk, starts[0])
            verify_partition(home, disk, starts[3])
            # Publish the partition table after the contents have been verified.
            with table.open("rb") as image:
                front = image.read(MIB)
                image.seek(-MIB, os.SEEK_END)
                back = image.read(MIB)
            disk.seek(-MIB, os.SEEK_END)
            disk.write(back)
            disk.seek(0)
            disk.write(front)
            disk.flush()
            os.fsync(fd)
            for offset, block in ((0, front), (capacity - MIB, back)):
                disk.seek(offset)
                if disk.read(len(block)) != block:
                    raise OSError("GPT readback failed")
    finally:
        os.close(payload)
    run("sfdisk", "--verify", target, stdout=subprocess.DEVNULL)
    print(f"MayOS Linux installed on {target}", flush=True)


def provision(args):
    path = Path(args.output).absolute()
    if path.exists() or path.is_symlink():
        raise ValueError("output already exists; choose a new disk image")
    payload = verified(args.bundle, args.sha256)
    password = None if args.locked_admin else admin_hash()
    ids = [uuid.uuid4() for _ in range(5)]
    flags = os.O_RDWR | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC
    created = False
    try:
        with tempfile.TemporaryDirectory(dir=path.parent) as scratch:
            work = Path(scratch)
            esp = build_esp(work, ids)
            home = data_image(work, password)
            descriptor = os.open(path, flags, 0o600)
            created = True
            with os.fdopen(descriptor, "r+b") as disk:
                starts = partition_image(path, TOTAL, ids)
                copy_small(esp, disk, starts[0])
                copy_slot(payload, disk, args.sha256, starts[1], sparse=True)
                copy_small(home, disk, starts[3])
                disk.flush()
                os.fsync(disk.fileno())
            run("sfdisk", "--verify", path, stdout=subprocess.DEVNULL)
    except BaseException:
        if created:
            path.unlink(missing_ok=True)
        raise
    finally:
        os.close(payload)
    print(f"Created bootable MayOS Linux disk: {path}", flush=True)


def deploy(args):
    target, capacity = empty_target(args.target, args.confirm)
    source = Path(args.image)
    with source.open("rb") as image:
        info = os.fstat(image.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_size != TOTAL or capacity < TOTAL:
            raise ValueError("invalid installer image or insufficient disk capacity")
        table = json.loads(output("sfdisk", "--json", source))["partitiontable"]
        if table["label"] != "gpt" or tuple(row["name"] for row in table["partitions"]) != NAMES:
            raise ValueError("image is not a MayOS Linux installer disk")
        if hashlib.file_digest(image, "sha256").hexdigest() != args.sha256:
            raise ValueError("installer disk SHA-256 mismatch")
        fd = os.open(target, os.O_RDWR | os.O_EXCL | os.O_CLOEXEC)
        with os.fdopen(fd, "r+b") as disk:
            ensure_empty(disk, target)
            disk.seek(0)
            image.seek(0)
            actual = hashlib.sha256()
            for block in iter(lambda: image.read(MIB), b""):
                actual.update(block)
                if not block.strip(b"\0"):
                    disk.seek(len(block), os.SEEK_CUR)
                else:
                    disk.write(block)
            disk.flush()
            os.fsync(fd)
            disk.seek(0)
            readback = hashlib.sha256()
            remaining = TOTAL
            while remaining:
                block = disk.read(min(MIB, remaining))
                if not block:
                    raise OSError("short read while verifying installed disk")
                readback.update(block)
                remaining -= len(block)
            if actual.hexdigest() != args.sha256 or readback.digest() != actual.digest():
                raise OSError("installer disk readback failed")
    if capacity != TOTAL:
        run("sfdisk", "--relocate", "gpt-bak-std", target)
    run("sfdisk", "--verify", target)
    print(f"MayOS Linux installed on {target}", flush=True)


def cmdline():
    values = {}
    for word in Path("/proc/cmdline").read_text().split():
        key, equal, value = word.partition("=")
        if equal and key in {"root", "mayos.disk", "mayos.slot", "mayos.esp", "mayos.data"}:
            if key in values:
                raise ValueError("duplicate installed boot option")
            values[key] = value
    return values


def part(uuid_text):
    return (Path("/dev/disk/by-partuuid") / str(uuid.UUID(uuid_text))).resolve(strict=True)


def installed():
    values = cmdline()
    required = {"root", "mayos.disk", "mayos.slot", "mayos.esp", "mayos.data"}
    if not required <= values.keys() or not values["root"].startswith("PARTUUID="):
        raise ValueError("boot from the installed MayOS Linux disk")
    slot = values["mayos.slot"]
    if slot not in ("A", "B"):
        raise ValueError("invalid installed slot")
    disk_id = str(uuid.UUID(values["mayos.disk"]))
    root = part(values["root"].split("=", 1)[1])
    esp = part(values["mayos.esp"])
    data = part(values["mayos.data"])
    if root.stat().st_rdev != Path("/").stat().st_dev or data.stat().st_rdev != Path("/home").stat().st_dev:
        raise ValueError("boot options do not identify the mounted root and user-data partitions")
    parent = output("lsblk", "-dnro", "PKNAME", root)
    if not re.fullmatch(r"[a-zA-Z0-9_-]+", parent):
        raise ValueError("unsupported boot-disk parent")
    disk = Path("/dev") / parent
    table = json.loads(output("sfdisk", "--json", disk))["partitiontable"]
    partitions = table["partitions"]
    if (table["label"] != "gpt" or table["id"].lower() != disk_id or table["sectorsize"] != SECTOR
            or tuple(row.get("name") for row in partitions) != NAMES):
        raise ValueError("installed MayOS Linux GPT layout does not match")
    paths = [Path(row["node"]).resolve(strict=True) for row in partitions]
    root_index = 1 if slot == "A" else 2
    if (paths[0], paths[root_index], paths[3]) != (esp, root, data):
        raise ValueError("installed partition identities do not match")
    spare = paths[3 - root_index]
    if output("blockdev", "--getsize64", spare) != str(SLOT_SIZE):
        raise ValueError("unexpected inactive root slot size")
    return disk_id, slot, esp, spare


def grub_env(directory, name):
    path = directory / f"{name}.env"
    if not path.is_file() or path.is_symlink() or path.stat().st_size != 1024:
        raise ValueError("invalid GRUB slot state")
    pairs = output("grub-editenv", path, "list")
    state = dict(line.split("=", 1) for line in pairs.splitlines())
    if any(key not in {"active_slot", "trial_slot", "attempted_slot"} or value not in {"", "A", "B"}
           for key, value in state.items()):
        raise ValueError("invalid GRUB slot assignment")
    return state


def replace_env(directory, name, **values):
    temporary = directory / f"{name}.new"
    temporary.unlink(missing_ok=True)
    create_env(temporary, **values)
    with temporary.open("rb") as stream:
        os.fsync(stream.fileno())
    os.replace(temporary, directory / f"{name}.env")
    fd = os.open(directory, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def upgrade(args):
    if os.geteuid() != 0:
        raise ValueError("system upgrades require local administrator access")
    with open("/run/mayos-linux-update.lock", "a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        disk, slot, esp, spare = installed()
        with tempfile.TemporaryDirectory(prefix="mayos-linux-", dir="/run") as scratch:
            mount = Path(scratch)
            settings = mount / ENV_PATH
            run("mount", "-t", "vfat", "-o", "nodev,nosuid,noexec,umask=077", esp, mount)
            try:
                if (settings / f"disk-{disk}").read_text().strip() != disk:
                    raise ValueError("boot disk marker mismatch")
                active = grub_env(settings, "active").get("active_slot")
                trial = grub_env(settings, "trial")
                if active not in {"A", "B"}:
                    raise ValueError("no confirmed slot")
                if args.action == "status":
                    print(f"Running: {slot}  Confirmed: {active}  Pending: {trial.get('trial_slot') or '-'}  Attempted: {trial.get('attempted_slot') or '-'}")
                elif args.action == "confirm":
                    if slot == active or trial.get("trial_slot") or trial.get("attempted_slot") != slot:
                        raise ValueError("confirm from the started trial slot")
                    replace_env(settings, "active", active_slot=slot)
                    replace_env(settings, "trial")
                    print("MayOS Linux: trial confirmed", flush=True)
                elif args.action == "discard":
                    if slot != active:
                        raise ValueError("restart into the confirmed slot before discarding")
                    replace_env(settings, "trial")
                    print("MayOS Linux: trial discarded", flush=True)
                else:
                    if slot != active or any(trial.values()):
                        raise ValueError("boot confirmed slot and resolve pending trial first")
                    payload = verified(args.bundle, args.sha256)
                    try:
                        fd = os.open(spare, os.O_RDWR | os.O_EXCL | os.O_CLOEXEC)
                        with os.fdopen(fd, "r+b") as target:
                            copy_slot(payload, target, args.sha256)
                    finally:
                        os.close(payload)
                    with tempfile.TemporaryDirectory(prefix="mayos-candidate-", dir="/run") as directory:
                        candidate = Path(directory)
                        run("mount", "-t", "ext4", "-o", "ro,noload,nodev,nosuid,noexec", spare, candidate)
                        try:
                            release = candidate / "etc/mayos-linux-release"
                            if version(release) <= version(Path("/etc/mayos-linux-release")):
                                raise ValueError("installed update must be newer")
                            for name in ("boot/mayos-linux", "boot/mayos-initrd", "usr/bin/mayos-desktop"):
                                if not (candidate / name).is_file():
                                    raise ValueError("candidate is missing a required boot file")
                        finally:
                            run("umount", candidate)
                    replace_env(settings, "trial", trial_slot="B" if slot == "A" else "A")
                    print("MayOS Linux: trial queued; reboot to try the new slot", flush=True)
            finally:
                run("umount", mount)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest="action", required=True)
    create = actions.add_parser("create", help="install to a new raw UEFI disk image")
    create.add_argument("--output", required=True)
    create.add_argument("--locked-admin", action="store_true", help="lock local administration for build tests")
    target = actions.add_parser("deploy", help="install a prepared image onto an explicitly selected blank disk")
    target.add_argument("--image", required=True)
    target.add_argument("--target", required=True)
    target.add_argument("--confirm", required=True)
    target.add_argument("--sha256", required=True)
    media = actions.add_parser("install-media", help="install the booted Linux64 ISO onto an empty disk")
    media.add_argument("--media", type=Path, required=True)
    media.add_argument("--target", required=True)
    media.add_argument("--confirm", required=True)
    for name in ("status", "confirm", "discard", "stage"):
        actions.add_parser(name)
    for command in (create, media, actions.choices["stage"]):
        command.add_argument("--bundle", type=Path, required=True)
        command.add_argument("--sha256", required=True)
    args = parser.parse_args()
    try:
        {"create": provision, "deploy": deploy, "install-media": install_media}.get(args.action, upgrade)(args)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"MayOS Linux: {error}\n")


if __name__ == "__main__":
    main()
