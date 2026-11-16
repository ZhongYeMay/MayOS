#!/usr/bin/env python3
"""Generate the MayOS updater catalog from authentic published GitHub releases.

Only ISO assets with GitHub-provided SHA-256 digests enter the updater feed.
A source-only release is visible on the website changelog, not installable.
No guessed sizes, hashes, or download URLs are ever generated.
"""
import argparse
import json
import re
import urllib.error
import urllib.request
from pathlib import Path

REPO = "ZhongYeMay/MayOS"
API = "https://api.github.com/repos/" + REPO + "/releases"
WEBSITE = Path(__file__).resolve().parents[1] / "website"

NOTES = {
    "v0.5.0-pre1": "Black/charcoal desktop; MayKernel64 and Linux64; MayFS integrity recovery; QEMU preview only.",
    "v0.4.1-pre2": "Strict reference UI; MayKernel64 and Linux64 QEMU validation.",
    "v0.4.1-pre1": "Initial native MayKernel ELF64 and Linux64 dual boot; QEMU validation.",
}
SHA = re.compile(r"sha256:([0-9a-f]{64})\Z")
ISO = re.compile(r"-(MayKernel-x86_64|dual-boot-x86_64)\.iso\Z")


def download_json(url, token):
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "MayOS-UpdateFeedGenerator/1.0",
        "X-GitHub-Api-Version": "2022-11-28",
    }
    if token:
        headers["Authorization"] = "Bearer " + token
    with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=15) as response:
        return json.load(response)


def build(releases):
    rows = []
    for release in releases:
        if release.get("draft") or not release.get("published_at"):
            continue
        for asset in release.get("assets", []):
            name = str(asset.get("name", ""))
            variant_match = ISO.search(name)
            digest_match = SHA.fullmatch(str(asset.get("digest") or ""))
            if not (variant_match and digest_match):
                continue
            url = str(asset.get("browser_download_url", ""))
            tag = str(release["tag_name"])
            expected = "https://github.com/" + REPO + "/releases/download/" + tag + "/" + name
            if url != expected:
                raise ValueError("Unexpected release asset URL: " + url)
            size = int(asset["size"])
            if size < 1024 * 1024 or size > 4 * 1024 * 1024 * 1024:
                raise ValueError("Unsafe ISO size: " + name)
            variant = "dual" if variant_match.group(1).startswith("dual") else "maykernel"
            rows.append(dict(
                tag=tag, name="MayOS " + tag.removeprefix("v"),
                variant=variant, arch="x86_64",
                kernel="MayKernel x86_64 + Linux x86_64" if variant == "dual" else "MayKernel x86_64",
                bytes=size, sha256=digest_match.group(1),
                published_at=release["published_at"], preview=bool(release["prerelease"]),
                asset_name=name, url=url, notes=release.get("body", "")[:6000]
            ))
    rows.sort(key=lambda x: (x["published_at"], x["variant"] == "maykernel"), reverse=True)
    return rows


def render(rows, date):
    data = dict(schema_version=1, repository=REPO,
                source="https://github.com/" + REPO + "/releases",
                generated_date=date, installer_supported=False,
                notice="Release metadata only. No self-installing ISO or transactional installer is provided.",
                items=rows)
    summary = lambda r: NOTES.get(r["tag"], "Experimental QEMU-tested MayOS preview.")
    text = ["MAYOS_UPDATE_FEED_V1",
            "# tag|variant|name|bytes|sha256|date|arch|kernel|url|summary"]
    for row in rows:
        vals = [row["tag"], row["variant"], row["name"], str(row["bytes"]),
                row["sha256"], row["published_at"][:10], row["arch"], row["kernel"],
                row["url"], summary(row)]
        if any("|" in value or "\n" in value for value in vals):
            raise ValueError("Invalid feed field")
        text.append("|".join(vals))
    return json.dumps(data, ensure_ascii=False, indent=2) + "\n", "\n".join(text) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="Compare against committed feed")
    parser.add_argument("--date", default=None, help="Snapshot date in UTC, YYYY-MM-DD")
    args = parser.parse_args()
    import datetime
    date = args.date or datetime.datetime.now(datetime.timezone.utc).date().isoformat()
    import os
    all_releases = []
    for page in range(1, 21):
        batch = download_json(API + f"?per_page=100&page={page}", os.environ.get("GH_TOKEN"))
        if not isinstance(batch, list):
            raise ValueError("Unexpected GitHub API response")
        all_releases.extend(batch)
        if len(batch) < 100:
            break
    rows = build(all_releases)
    if not rows:
        raise ValueError("No verified x86_64 ISO assets; refusing empty feed")
    payloads = render(rows, date)
    for filename, content in zip(("update-feed-v1.json", "update-feed-v1.txt"), payloads):
        target = WEBSITE / filename
        if args.check:
            if not target.is_file() or target.read_text(encoding="utf-8") != content:
                raise SystemExit(f"Stale update feed: {filename}")
        else:
            target.write_text(content, encoding="utf-8")
    print(f"PASS: {len(rows)} verified ISO records with GitHub SHA-256 digests")


if __name__ == "__main__":
    main()
