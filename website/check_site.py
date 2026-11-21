#!/usr/bin/env python3
"""Validate MayOS website navigation, download links, version and MP4 integrity."""
from html.parser import HTMLParser
from pathlib import Path
import hashlib
import json
import os
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parent
CURRENT = "0.5.1-pre1"
MULTILINGUAL = "0.5.2-pre1"
CJK_PREVIEW = "0.5.2-pre2"
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

assert {"index.html", "demos.html", "download.html", "requirements.html"} <= set(docs)
for key in ("x86_64", "512 MiB", "80 MiB", "96 MiB", "Secure Boot", "0.5.1", "UEFI x64"):
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
assert f"MayOS-{CURRENT}-Installer-x86_64.iso" in docs["download.html"][1]
assert f"/download/v{CURRENT}/SHA256SUMS.txt" in docs["download.html"][1]
assert "installer-preview" in docs["demos.html"][0].ids
assert "multilingual-preview" in docs["changelog.html"][0].ids
assert "multilingual" in docs["demos.html"][0].ids
assert "cjk-input" in docs["demos.html"][0].ids
assert "cjk-installer" in docs["download.html"][0].ids
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

assert docs["demos.html"][0].videos == 2
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
assert tags[0] == f"v{CJK_PREVIEW}", "latest preview missing from the top of the history snapshot"
assert f"v{MULTILINGUAL}" in tags, "display-language preview missing from the history snapshot"
assert f"v{CURRENT}" in tags, "installer release missing from the history snapshot"
assert "v0.5.0-pre1" in tags and "v0.3.0" in tags, "historical release omitted"
for entry in history["releases"]:
    assert entry["name"] and entry["published_at"]
    assert entry["url"] == "https://github.com/ZhongYeMay/MayOS/releases/tag/" + entry["tag"]
    assert isinstance(entry["prerelease"], bool)
    assert isinstance(entry["body"], str)
print("PASS: all published release notes present, including prereleases; safe navigation")
