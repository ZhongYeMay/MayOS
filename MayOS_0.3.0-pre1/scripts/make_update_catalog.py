#!/usr/bin/env python3
"""Build-time, offline-only conversion of verified GitHub ISO catalog into C.
No network is needed when compiling MayKernel. Input is published repo data.
"""
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]
feed = (ROOT / "website/update-feed-v1.txt").read_text(encoding="utf-8").splitlines()
assert feed[0] == "MAYOS_UPDATE_FEED_V1", "Unexpected update catalog version"
records = []
for line in feed[1:]:
    if not line or line.startswith("#"):
        continue
    fields = line.split("|")
    assert len(fields) == 10, "Malformed feed row"
    tag, variant, name, size, digest, date, arch, kernel, url, summary = fields
    assert re.fullmatch(r"v\d+\.\d+\.\d+(?:-pre\d+)?", tag), tag
    assert variant in ("maykernel", "dual")
    assert arch == "x86_64"
    assert re.fullmatch(r"[0-9a-f]{64}", digest)
    assert int(size) >= 1_048_576 and int(size) <= 536_870_912
    assert re.fullmatch(r"\d{4}-\d{2}-\d{2}", date)
    assert url.startswith("https://github.com/ZhongYeMay/MayOS/releases/download/" + tag + "/")
    assert url.endswith(".iso")
    records.append((tag, variant, name, arch, kernel, date, url, digest, summary, size))
assert records, "Refusing an empty update catalog"
out = ROOT / "build/update_catalog.h"
out.parent.mkdir(exist_ok=True)
lines = [
    "/* Generated from website/update-feed-v1.txt; do not edit manually. */",
    "#ifndef MAYOS_UPDATE_CATALOG_H",
    "#define MAYOS_UPDATE_CATALOG_H",
    "#define MAYOS_UPDATE_CATALOG_COUNT " + str(len(records)) + "u",
    "static const MayUpdateCatalogRow mayos_update_catalog[MAYOS_UPDATE_CATALOG_COUNT] = {",
]
for row in records:
    strings = ", ".join(json.dumps(v, ensure_ascii=False) for v in row[:-1])
    lines.append(f"    {{{strings}, {int(row[-1])}u}},")
lines += ["};", "#endif /* MAYOS_UPDATE_CATALOG_H */", ""]
out.write_text("\n".join(lines), encoding="utf-8")
print(f"updates: {len(records)} authentic ISO records -> {out}")
