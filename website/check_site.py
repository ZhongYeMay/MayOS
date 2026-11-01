#!/usr/bin/env python3
"""Fast, dependency-free checks against accidental misleading website edits."""
from html.parser import HTMLParser
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent
html = (ROOT / "index.html").read_text(encoding="utf-8")

class LinkParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.links = []
        self.ids = set()
        self.title = False
        self.has_lang = False
    def handle_starttag(self, tag, attrs):
        attributes = dict(attrs)
        if tag == "html":
            self.has_lang = attributes.get("lang") == "zh-CN"
        if "id" in attributes:
            self.ids.add(attributes["id"])
        if tag == "a":
            self.links.append(attributes.get("href", ""))
        if tag == "title":
            self.title = True

parser = LinkParser()
parser.feed(html)
assert parser.has_lang and parser.title
assert len(parser.links) >= 8
assert all(not url.startswith("javascript:") for url in parser.links)
assert all(url[1:] in parser.ids for url in parser.links if url.startswith("#")), "Broken in-page link"
required = (
    "Fastfetch", "Ubuntu", "MayKernel", "0.3.0-pre1",
    "MayOS Team 2023 - 2026.", "This version of MayOS is for test use only.",
    "开发快照", "Linux 双启动 ISO", "XRGB8888", "v0.3.0-pre1-source.1",
)
assert all(text in html for text in required), "Missing project disclosure"
assert "MayOS_0.1_Eng" not in html
assert "MayOS_0.2_Eng" not in html
assert 'href="#"' not in html and "href='#'" not in html, "Empty link target"
print("PASS: MayOS website HTML, navigation, disclosures and release boundaries")
