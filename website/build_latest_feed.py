"""Build the small x86_64 update catalog from the published text feed."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent


def release_key(tag):
    match = re.fullmatch(r"v?(\d+)\.(\d+)\.(\d+)(?:(-pre|-beta\.)(\d+))?", tag)
    if match is None:
        return None
    major, minor, patch, channel, number = match.groups()
    return int(major), int(minor), int(patch), ("-pre", "-beta.", None).index(channel), int(number or 0)


def latest_feed(feed, *, include_beta=False):
    newest = {}
    for line in feed.splitlines()[2:]:
        fields = line.split("|", 9)
        if len(fields) != 10 or fields[6] != "x86_64":
            continue
        version = release_key(fields[0])
        if not include_beta and "-beta." in fields[0]:
            continue
        variant = fields[1]
        if version is None or variant not in ("maykernel", "dual", "linux", "maykernel-install"):
            continue
        target = "linux" if variant == "dual" else variant
        if target not in newest or version > newest[target][0]:
            if len(line.encode("ascii")) >= 512:
                raise ValueError(f"Release line too long: {fields[0]}")
            newest[target] = (version, line)
    if not {"maykernel", "linux"} <= newest.keys():
        raise ValueError("Missing a MayKernel or Linux release")
    variants = ("maykernel", "linux", "maykernel-install")
    rows = [newest[variant][1] for variant in variants if variant in newest]
    catalog = "\n".join(("MAYOS_UPDATE_FEED_V1", *rows, "MAYOS_UPDATE_FEED_END", ""))
    if len(catalog.encode("ascii")) > 1700:
        raise ValueError("Latest feed exceeds the smallest fetch budget")
    return catalog


if __name__ == "__main__":
    feed = (ROOT / "update-feed-v1.txt").read_text(encoding="utf-8")
    (ROOT / "update-latest-v1.txt").write_text(latest_feed(feed), encoding="ascii")
    (ROOT / "update-latest-beta-v1.txt").write_text(
        latest_feed(feed, include_beta=True), encoding="ascii")
