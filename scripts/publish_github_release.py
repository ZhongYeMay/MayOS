#!/usr/bin/env python3
"""Verify pinned MayOS assets, then publish a new GitHub prerelease with gh."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def safe_path(value):
    require(isinstance(value, str) and bool(re.fullmatch(r"[A-Za-z0-9._/-]+", value)),
            f"Invalid manifest path: {value!r}")
    parts = PurePosixPath(value).parts
    require(parts and not value.startswith("/") and ".." not in parts,
            f"Unsafe manifest path: {value!r}")
    return value


def load_manifest(path):
    manifest = json.loads(path.read_text(encoding="utf-8"))
    require(manifest["schema_version"] == 1, "Unsupported manifest schema")
    require(manifest["repository"] == "ZhongYeMay/MayOS", "Unexpected repository")
    prerelease = manifest.get("prerelease", True)
    require(type(prerelease) is bool, "prerelease must be a boolean")
    tag_pattern = r"v[0-9]+\.[0-9]+\.[0-9]+-pre[1-9][0-9]*" if prerelease else r"v[0-9]+\.[0-9]+\.[0-9]+"
    require(bool(re.fullmatch(tag_pattern, manifest["tag"])),
            "Release tag does not match its prerelease channel")
    manifest["prerelease"] = prerelease
    origin = urllib.parse.urlsplit(manifest["origin"])
    require(origin.scheme == "https" and origin.hostname and not origin.username
            and not origin.password and origin.path in ("", "/")
            and not origin.query and not origin.fragment, "Expected an HTTPS origin")
    safe_path(manifest["notes_file"])
    names = set()
    for asset in manifest["assets"]:
        name = safe_path(asset["file"])
        require("/" not in name and name not in names, "Duplicate or unsafe asset name")
        names.add(name)
        specs = [asset] + asset.get("parts", [])
        for spec in specs:
            require(type(spec["size"]) is int and spec["size"] > 0, "Invalid byte size")
            require(bool(re.fullmatch(r"[0-9a-f]{64}", spec["sha256"])), "Invalid SHA256")
            if "path" in spec:
                safe_path(spec["path"])
        if "parts" in asset:
            require(asset["parts"] and "path" not in asset, "Invalid multipart asset")
            require(sum(p["size"] for p in asset["parts"]) == asset["size"],
                    "Part sizes do not add up to the complete asset")
            require(all("path" in p for p in asset["parts"]), "Missing part path")
        else:
            require("path" in asset, "Missing download path")
    require(len(names) == 6 and manifest["checksum_file"] in names,
            "Expected six assets including the checksum file")
    return manifest


class SameOriginRedirect(urllib.request.HTTPRedirectHandler):
    def __init__(self, origin):
        self.origin = urllib.parse.urlsplit(origin)[:2]

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        require(urllib.parse.urlsplit(newurl)[:2] == self.origin,
                "Refusing a cross-origin or non-HTTPS download redirect")
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def collect_assets(manifest, output, local_site=None):
    """Use identical byte verification for HTTPS downloads and an offline site tree."""
    output.mkdir(parents=True, exist_ok=True)
    opener = urllib.request.build_opener(SameOriginRedirect(manifest["origin"]))
    for asset in manifest["assets"]:
        total, combined = 0, hashlib.sha256()
        fd, temporary = tempfile.mkstemp(prefix=asset["file"] + ".", suffix=".tmp", dir=output)
        try:
            with os.fdopen(fd, "wb") as destination:
                for part in asset.get("parts", [asset]):
                    url = urllib.parse.urljoin(manifest["origin"].rstrip("/") + "/", part["path"])
                    print(f"Verify {part['path']}", flush=True)
                    try:
                        source = ((local_site / part["path"]).open("rb") if local_site
                                  else opener.open(url, timeout=60))
                        count, digest = 0, hashlib.sha256()
                        with source:
                            while block := source.read(1024 * 1024):
                                count += len(block)
                                require(count <= part["size"], f"Oversized download: {part['path']}")
                                digest.update(block)
                                combined.update(block)
                                destination.write(block)
                        require(count == part["size"] and digest.hexdigest() == part["sha256"],
                                f"Size/SHA256 mismatch: {part['path']}")
                        total += count
                    except urllib.error.HTTPError as error:
                        raise RuntimeError(
                            f"Download failed before release creation: HTTP {error.code} for {url}. "
                            "The public site rejected an ordinary HTTPS request. "
                            "Fix download access or use an authorized asset-upload method; "
                            "this script does not bypass the site's access policy."
                        ) from error
                    except urllib.error.URLError as error:
                        raise RuntimeError(f"Download failed before release creation: {url}: {error.reason}") from error
            require(total == asset["size"] and combined.hexdigest() == asset["sha256"],
                    f"Complete asset size/SHA256 mismatch: {asset['file']}")
            os.replace(temporary, output / asset["file"])
        finally:
            Path(temporary).unlink(missing_ok=True)
    checksums = {}
    for line in (output / manifest["checksum_file"]).read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  ([A-Za-z0-9._-]+)", line)
        require(match and match[2] not in checksums, "Malformed or duplicate checksum entry")
        checksums[match[2]] = match[1]
    expected = {a["file"]: a["sha256"] for a in manifest["assets"]
                if a["file"] != manifest["checksum_file"]}
    require(checksums == expected, "SHA256SUMS does not match the pinned release manifest")
    print("Verified all six release assets, including every source part.", flush=True)


def gh(*args):
    result = subprocess.run(["gh", *args], text=True, capture_output=True)
    require(result.returncode == 0, f"GitHub CLI failed ({args[0]}): {result.stderr.strip()}")
    return result.stdout.strip()


def releases(repository):
    pages = json.loads(gh("api", f"repos/{repository}/releases?per_page=100", "--paginate", "--slurp"))
    return [release for page in pages for release in page]


def ensure_unused(manifest):
    repository, tag = manifest["repository"], manifest["tag"]
    require(not any(r["tag_name"] == tag for r in releases(repository)),
            f"Release {tag} already exists (including drafts); refusing to overwrite it")
    refs = json.loads(gh("api", f"repos/{repository}/git/matching-refs/tags/{tag}"))
    require(not any(r["ref"] == f"refs/tags/{tag}" for r in refs),
            f"Tag {tag} already exists; refusing to reuse or move it")


def publish(manifest, output, notes, target):
    require(bool(re.fullmatch(r"[0-9a-f]{40}", target or "")), "Publishing requires a full target commit SHA")
    require(os.environ.get("GH_TOKEN"), "Publishing requires GH_TOKEN")
    repository, tag = manifest["repository"], manifest["tag"]
    require(os.environ.get("GITHUB_REPOSITORY", repository) == repository, "Wrong workflow repository")
    ensure_unused(manifest)
    create_flags = ["--draft"]
    if manifest["prerelease"]:
        create_flags.extend(("--prerelease", "--latest=false"))
    else:
        create_flags.append("--latest")
    gh("release", "create", tag, "--repo", repository, "--target", target,
       "--title", manifest["title"], "--notes-file", str(notes), *create_flags)
    try:
        created = [r for r in releases(repository) if r["tag_name"] == tag]
        require(len(created) == 1 and created[0]["draft"], "Cannot identify the new draft release")
        release_id = created[0]["id"]
        gh("release", "upload", tag, "--repo", repository,
           *(str(output / a["file"]) for a in manifest["assets"]))
        release = json.loads(gh("api", f"repos/{repository}/releases/{release_id}"))
        expected = {a["file"]: a for a in manifest["assets"]}
        require(release["draft"] and release["prerelease"] == manifest["prerelease"]
                and release["tag_name"] == tag,
                "Draft release metadata changed during upload")
        require(len(release["assets"]) == len(expected)
                and {a["name"] for a in release["assets"]} == set(expected), "Wrong uploaded asset set")
        for asset in release["assets"]:
            pinned = expected.get(asset["name"])
            require(pinned and asset["size"] == pinned["size"] and asset["state"] == "uploaded",
                    f"Uploaded asset mismatch: {asset['name']}")
            if asset.get("digest"):
                require(asset["digest"] == "sha256:" + pinned["sha256"], "GitHub asset digest mismatch")
        publish_flags = (["--prerelease", "--latest=false"] if manifest["prerelease"]
                         else ["--prerelease=false", "--latest"])
        gh("release", "edit", tag, "--repo", repository, "--draft=false", *publish_flags)
        release = json.loads(gh("api", f"repos/{repository}/releases/{release_id}"))
        require(not release["draft"] and release["prerelease"] == manifest["prerelease"],
                "Publication status could not be confirmed")
        print("Published " + release["html_url"], flush=True)
    except Exception:
        print("Publication did not complete cleanly. Inspect the new release/draft before retrying; "
              "this script never overwrites or deletes it.", file=sys.stderr)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--local-site", type=Path, help="Offline verification from an existing dist directory; cannot publish")
    parser.add_argument("--publish", action="store_true")
    parser.add_argument("--target", help="Full commit SHA for the new release tag")
    args = parser.parse_args()
    require(not (args.publish and args.local_site), "Offline mode cannot publish")
    manifest = load_manifest(args.manifest)
    notes = Path(__file__).resolve().parents[1] / manifest["notes_file"]
    require(notes.is_file(), f"Missing release notes: {notes}")
    if args.publish:
        require(os.environ.get("GH_TOKEN"), "Publishing requires GH_TOKEN")
        ensure_unused(manifest)  # API/auth errors are fatal, never mistaken for absence.
    collect_assets(manifest, args.output, args.local_site)
    if args.publish:
        publish(manifest, args.output, notes, args.target)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, ValueError, KeyError, TypeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
