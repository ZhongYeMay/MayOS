#!/usr/bin/env python3
"""Verify every MayOS version directory against its original Git tree.

The active development directory is the *only* writable source tree.
The historical directories directly reuse original published Git tree IDs.
"""
from pathlib import Path
import json
import subprocess

ROOT=Path(__file__).resolve().parents[1]
catalog=json.loads((ROOT/"versions/catalog.json").read_text())
assert catalog["schema_version"]==1
active=catalog["active_version"]
active_dir=ROOT/"versions"/active
assert (active_dir/"Makefile").is_file()
assert (active_dir/"kernel/version.h").is_file()
legacy=ROOT/catalog["legacy_symlink"]
assert legacy.is_symlink(),"Legacy build alias must be a symlink, not duplicated source"
assert legacy.resolve()==active_dir.resolve(),"Legacy build alias does not point to the active version"
assert (legacy/"Makefile").is_file()
assert (legacy/"scripts/installer_media_smoke.py").is_file()
expected={x["directory"]:x for x in catalog["released"]}
found={x.name for x in (ROOT/"versions").iterdir() if x.is_dir()}
assert found==set(expected)|{active}, ("unexpected/missing release directories", found)
for directory,item in expected.items():
    path=ROOT/"versions"/directory
    assert path.is_dir() and not path.is_symlink(),directory
    assert (path/"Makefile").is_file(),directory
    actual=subprocess.check_output(
        ["git","rev-parse",f"HEAD:versions/{directory}"],cwd=ROOT,text=True).strip()
    assert actual==item["source_tree_sha"],(
        f"Release {item['tag']} source tree changed",actual,item["source_tree_sha"])
    print("PASS: immutable release",item["tag"],actual[:12],flush=True)
print("PASS: distinct, historical version folders + active 0.5.1 source + compatible build alias")
