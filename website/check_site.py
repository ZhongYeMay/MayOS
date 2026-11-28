#!/usr/bin/env python3
"""Validate MayOS website navigation, download links, version and MP4 integrity."""
from html.parser import HTMLParser
from pathlib import Path
import hashlib
import json
import os
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parent
CURRENT = "0.5.1-pre1"  # historical release regression checks
OFFICIAL = "0.5.2"
OFFICIAL_ISO = "MayOS-0.5.2-DualKernel-Live-Installer-x86_64.iso"
MULTILINGUAL = "0.5.2-pre1"
CJK_PREVIEW = "0.5.2-pre2"
HARDWARE_PREVIEW = "0.5.2-pre3"
POINTER_PREVIEW = "0.5.2-pre4"
HISTORIC_DEMOS = "0.4.1-pre2"
DEMO_RELEASE = "0.4.0-pre3"
PREFIX = "MayOS-0.4.0-pre3-all-features-"


class SiteParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.title = False
        self.lang = None
        self.ids = set()
        self.urls = []
        self.images_without_alt = []
        self.videos = 0

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == "html":
            self.lang = attrs.get("lang")
        if tag == "title":
            self.title = True
        if attrs.get("id"):
            assert attrs["id"] not in self.ids, ("duplicate id", attrs["id"])
            self.ids.add(attrs["id"])
        if tag in {"a", "source", "img", "script", "link"}:
            url = attrs.get("href") or attrs.get("src")
            if url:
                self.urls.append(url)
        if tag == "img" and not attrs.get("alt"):
            self.images_without_alt.append(attrs.get("src"))
        if tag == "video":
            self.videos += 1
            assert "controls" in attrs, "Video player must be interactive"


docs = {}
for page in ROOT.glob("*.html"):
    parsed = SiteParser()
    source = page.read_text(encoding="utf-8")
    parsed.feed(source)
    assert parsed.lang == "zh-CN" and parsed.title, page.name
    assert not parsed.images_without_alt, (page.name, parsed.images_without_alt)
    assert 'href="#"' not in source and "href='#'" not in source
    assert "javascript:" not in source.lower()
    docs[page.name] = (parsed, source)

assert {"index.html", "demos.html", "download.html", "requirements.html", "deployment.html"} <= set(docs)
assert 'href="deployment.html"' in docs["download.html"][1]
assert 'id="latest-dual-installer"' in docs["download.html"][1]
assert f'/releases/download/v{OFFICIAL}/{OFFICIAL_ISO}' in docs['download.html'][1]
assert f'/releases/download/v{OFFICIAL}/{OFFICIAL_ISO}' in docs['deployment.html'][1]
assert OFFICIAL_ISO in docs['deployment.html'][1]
assert 'download.html#latest-dual-installer' in docs['index.html'][1]
assert "MayOS Live 0.5.2" in docs["deployment.html"][1]
assert 'id="linux-persistence-054"' in docs["requirements.html"][1]
assert '0.5.4-dev' in docs["requirements.html"][1]
assert '44991291384' in docs["requirements.html"][1]
assert '不包含在正式版 0.5.2 ISO' in docs["requirements.html"][1]
assert "安装版本" in docs["deployment.html"][1] or "安装" in docs["deployment.html"][1]
assert "QEMU" in docs["deployment.html"][1]
assert "Secure Boot" in docs["deployment.html"][1]

for key in ("x86_64", "512 MiB", "80 MiB", "96 MiB", "Secure Boot", "0.5.2", "UEFI x64", "Live"):
    assert key in docs["requirements.html"][1], ("hardware requirements missing", key)
for page in ("index.html", "download.html"):
    assert 'href="requirements.html"' in docs[page][1], ("missing hardware navigation", page)
for name in ("index.html", "download.html"):
    assert CURRENT in docs[name][1], (name, "stale release")
    assert "0.3.0-pre2" not in docs[name][1], name
assert "0.5.0-pre1-dev" in docs["index.html"][1], "development UI preview missing"
assert 'id="design-preview"' in docs["index.html"][1], "preview anchor missing"
assert "不是 QEMU 实测画面" in docs["index.html"][1], "design preview must not be passed off as QEMU"
assert "0.4.1-pre2" in docs["download.html"][1], "historic release download not retained"
assert "MayOS Live" in docs["index.html"][1], "official Live hero missing"
assert f'/releases/tag/v{OFFICIAL}' in docs["changelog.html"][1], "official notes missing"
assert 'href="requirements.html#live"' in docs["index.html"][1], "Live requirements link missing"
assert 'id="live"' in docs["requirements.html"][1], "Live section missing"
assert 'a394e27c608e09d5dd1848b288cf540541319841b022e9da60448e3a2a4bba91' in docs["download.html"][1], "published official SHA missing"

assert f"MayOS-{CURRENT}-Installer-x86_64.iso" in docs["download.html"][1]
assert f"/download/v{CURRENT}/SHA256SUMS.txt" in docs["download.html"][1]
assert "installer-preview" in docs["demos.html"][0].ids
assert "multilingual-preview" in docs["changelog.html"][0].ids
assert "multilingual" in docs["demos.html"][0].ids
assert "cjk-input" in docs["demos.html"][0].ids
assert "cjk-installer" in docs["download.html"][0].ids
assert "hardware-preview" in docs["requirements.html"][0].ids
assert "modern-hardware" in docs["demos.html"][0].ids
assert "pointer-preview" in docs["download.html"][0].ids
assert "windows-pointers" in docs["demos.html"][0].ids
assert f"/releases/tag/v{MULTILINGUAL}" in docs["download.html"][1]
assert f"/releases/tag/v{CURRENT}" in docs["index.html"][1]
assert HISTORIC_DEMOS in docs["demos.html"][1], "historic QEMU demo missing"

pending = []
for name, (doc, _) in docs.items():
    for url in doc.urls:
        if url.startswith(("http://", "https://", "data:", "mailto:")):
            continue
        path, sep, anchor = url.partition("#")
        if path.startswith("//"):
            continue
        target = ROOT / path if path else ROOT / name
        if path and not target.is_file():
            if path.startswith("media/" + PREFIX) and path.endswith((".mp4", ".json")):
                pending.append(str(target.relative_to(ROOT)))
                continue
            raise AssertionError(("broken link", name, url))
        if anchor and path.endswith(".html") or (anchor and not path):
            dest_doc = docs.get(path or name)
            assert dest_doc and anchor in dest_doc[0].ids, ("broken anchor", name, url)

assert docs["demos.html"][0].videos == 3
assert "MayOS-0.4.1-pre2-dual-boot-x86_64.iso" in docs["download.html"][1]
assert "MayOS-0.4.1-pre2-MayKernel-x86_64.iso" in docs["download.html"][1]
assert "v0.4.0-pre1" in docs["download.html"][1], "historic pre1 download omitted"

if not pending:
    for kernel in ("MayKernel", "Linux"):
        stem = PREFIX + kernel
        mp4 = ROOT / "media" / (stem + ".mp4")
        metadata = ROOT / "media" / (stem + ".json")
        assert mp4.stat().st_size > 10_000
        record = json.loads(metadata.read_text(encoding="utf-8"))
        assert record["mayos_release"] == DEMO_RELEASE
        assert record["video"] == mp4.name
        assert record["kernel"] == ("custom" if kernel == "MayKernel" else "linux")
        assert record["frames"] >= 180 and record["duration_seconds"] >= 18
        assert hashlib.sha256(mp4.read_bytes()).hexdigest() == record["video_sha256"]
    print("PASS: MayOS 0.4.1-pre2 website and archived pre3 media hashes")
elif os.environ.get("REQUIRE_PRE2_RECORDINGS") == "1":
    raise AssertionError(("Missing verified pre2 boot media", pending))
else:
    print("PASS: website and Release links; video publication pending:", pending)

for asset in ("MayOS-0.4.1-pre2-MayKernel64-All-Functions.mp4",
              "MayOS-0.4.1-pre2-Linux64-All-Functions.mp4",
              "MayOS-0.4.1-pre2-desktop.png"):
    assert asset in (docs["demos.html"][1] + docs["index.html"][1]), asset

# Changelog page is navigable from every existing page, including prereleases.
assert "changelog.html" in docs
for name in ("index.html", "download.html", "demos.html"):
    assert 'href="changelog.html"' in docs[name][1], (name, "missing release notes navigation")
assert 'id="release-list"' in docs["changelog.html"][1]
assert 'id="log-search"' in docs["changelog.html"][1]
assert 'data-filter="preview"' in docs["changelog.html"][1]
assert 'src="changelog.js"' in docs["changelog.html"][1]
history = json.loads((ROOT / "release-history.json").read_text(encoding="utf-8"))
assert history["schema_version"] == 1
assert history["repository"] == "ZhongYeMay/MayOS"
assert len(history["releases"]) >= 10
tags = [r["tag"] for r in history["releases"]]
assert len(tags) == len(set(tags)), "duplicate release tag in snapshot"
assert tags[0] == f"v{OFFICIAL}", "official v0.5.2 release missing from history snapshot"
assert history["releases"][0]["prerelease"] is False
assert history["releases"][0]["assets_count"] == 4
assert f"v{HARDWARE_PREVIEW}" in tags, "hardware preview missing from the history snapshot"
assert f"v{CJK_PREVIEW}" in tags, "input method preview missing from the history snapshot"
assert f"v{MULTILINGUAL}" in tags, "display-language preview missing from the history snapshot"
assert f"v{CURRENT}" in tags, "installer release missing from the history snapshot"
assert "v0.5.0-pre1" in tags and "v0.3.0" in tags, "historical release omitted"
for entry in history["releases"]:
    assert entry["name"] and entry["published_at"]
    assert entry["url"] == "https://github.com/ZhongYeMay/MayOS/releases/tag/" + entry["tag"]
    assert isinstance(entry["prerelease"], bool)
    assert isinstance(entry["body"], str)
print("PASS: all published release notes present, including prereleases; safe navigation")

from build_latest_feed import latest_feed

assert (ROOT / "update-latest-v1.txt").read_text(encoding="ascii") == latest_feed(
    (ROOT / "update-feed-v1.txt").read_text(encoding="utf-8")
), "Regenerate update-latest-v1.txt after editing the update feed"
print("PASS: latest update catalog is complete and fits the client transport")

assert (ROOT / "update-latest-beta-v1.txt").read_text(encoding="ascii") == latest_feed(
    (ROOT / "update-feed-v1.txt").read_text(encoding="utf-8"), include_beta=True
), "Regenerate update-latest-beta-v1.txt after editing the update feed"
