#!/usr/bin/env python3
"""Validate MayOS website navigation, download links, version and MP4 integrity."""
from html.parser import HTMLParser
from pathlib import Path
import hashlib
import json
import os
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parent
CURRENT = "0.4.0-pre2"
PREFIX = "MayOS-0.4.0-pre2-UEFI-"


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

assert {"index.html", "demos.html", "download.html"} <= set(docs)
for name in ("index.html", "download.html", "demos.html"):
    assert CURRENT in docs[name][1], (name, "stale release")
    assert "0.3.0-pre2" not in docs[name][1] or name == "demos.html", name

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
assert "MayOS-0.4.0-pre2-dual-boot.iso" in docs["download.html"][1]
assert "MayOS-0.4.0-pre2-MayKernel.iso" in docs["download.html"][1]
assert "v0.4.0-pre1" in docs["download.html"][1], "historic pre1 download omitted"

if not pending:
    for kernel in ("MayKernel", "Linux"):
        stem = PREFIX + kernel
        mp4 = ROOT / "media" / (stem + ".mp4")
        metadata = ROOT / "media" / (stem + ".json")
        assert mp4.stat().st_size > 10_000
        record = json.loads(metadata.read_text(encoding="utf-8"))
        assert record["mayos_release"] == CURRENT
        assert record["video"] == mp4.name
        assert record["kernel"] == ("custom" if kernel == "MayKernel" else "linux")
        assert record["frames"] >= 180 and record["duration_seconds"] >= 18
        assert hashlib.sha256(mp4.read_bytes()).hexdigest() == record["video_sha256"]
    print("PASS: MayOS 0.4.0-pre2 website navigation and real MP4 hashes")
elif os.environ.get("REQUIRE_PRE2_RECORDINGS") == "1":
    raise AssertionError(("Missing verified pre2 boot media", pending))
else:
    print("PASS: website and Release links; video publication pending:", pending)
