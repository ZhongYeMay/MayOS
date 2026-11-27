"""Build the small x86_64 update catalog from the published text feed."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent


def release_key(tag):
    match = re.fullmatch(r"v?(\d+)\.(\d+)\.(\d+)(?:-pre(\d+))?", tag)
    if match is None:
        return None
    major, minor, patch, preview = match.groups()
    return int(major), int(minor), int(patch), preview is None, int(preview or 0)


def latest_feed(feed):
    newest = {}
    for line in feed.splitlines()[2:]:
        fields = line.split("|", 9)
        if len(fields) != 10 or fields[6] != "x86_64":
            continue
        version = release_key(fields[0])
        variant = fields[1]
        if version is None or variant not in ("maykernel", "dual", "linux"):
            continue
        target = "maykernel" if variant == "maykernel" else "linux"
        if target not in newest or version > newest[target][0]:
            if len(line.encode("ascii")) >= 512:
                raise ValueError(f"Release line too long: {fields[0]}")
            newest[target] = (version, line)
    if newest.keys() != {"maykernel", "linux"}:
        raise ValueError("Missing a MayKernel or Linux release")
    catalog = "\n".join(("MAYOS_UPDATE_FEED_V1", newest["maykernel"][1],
                         newest["linux"][1], "MAYOS_UPDATE_FEED_END", ""))
    if len(catalog.encode("ascii")) > 1700:
        raise ValueError("Latest feed exceeds the smallest fetch budget")
    return catalog


if __name__ == "__main__":
    (ROOT / "update-latest-v1.txt").write_text(
        latest_feed((ROOT / "update-feed-v1.txt").read_text()), encoding="ascii")
