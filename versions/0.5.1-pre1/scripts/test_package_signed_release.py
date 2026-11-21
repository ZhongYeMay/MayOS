#!/usr/bin/env python3
"""Owner-key test performed locally in GitHub Actions with throwaway keys.
Never store or ship the test private key or claim the test key is MayOS's root.
"""
from __future__ import annotations

from datetime import date
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile

from package_signed_release import manifest_bytes
from verify_signed_release import verified_metadata


HERE = Path(__file__).resolve().parent
TOOL = HERE / "package_signed_release.py"


def run(*args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run([*args], text=True, capture_output=True, check=check)


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="mayos-ed25519-offline-ci-") as base:
        root = Path(base)
        key = root / "private-key-DO-NOT-SHIP.pem"
        pubkey = root / "public-key.pem"
        image = root / "MayOS-test-image.bin"
        out = root / "published-files"
        image.write_bytes(b"abc")
        run("openssl", "genpkey", "-algorithm", "Ed25519", "-out", str(key))
        run("openssl", "pkey", "-in", str(key), "-pubout", "-out", str(pubkey))
        produced = run(
            sys.executable, str(TOOL),
            "--image", str(image), "--private-key", str(key),
            "--output-dir", str(out),
            "--version", "0.5.0-pre3",
            "--date", "2026-11-19",
            "--eta-minutes", "7",
            "--changelog", "Offline signed test",
        )
        assert "PASS: exact Ed25519-signed" in produced.stdout
        assert sorted(path.name for path in out.iterdir()) == ["Update.may", "Update.sig"]
        content = (out / "Update.may").read_bytes()
        sig = (out / "Update.sig").read_bytes()
        assert len(sig) == 64
        assert content == manifest_bytes(
            image=image, version="0.5.0-pre3", released="2026-11-19",
            eta=7, changelog="Offline signed test",
        )
        assert b"sha256=" + hashlib.sha256(b"abc").hexdigest().encode() in content
        assert b"size_bytes=3\n" in content
        assert len(content) < 2048
        assert not any(p.suffix == ".pem" for p in out.iterdir())
        assert "private-key" not in content.decode()
        # Independent OpenSSL Ed25519 verification, using the matching PUBLIC key.
        run("openssl", "pkeyutl", "-verify", "-rawin", "-pubin",
            "-inkey", str(pubkey), "-in", str(out / "Update.may"),
            "-sigfile", str(out / "Update.sig"))
        # Independent release verifier must use separately trusted key and
        # the exact hash of the actual artifact, not just a valid signature.
        assert verified_metadata(
            manifest=out / "Update.may", signature=out / "Update.sig",
            image=image, public_key=pubkey,
        ) == content
        # Key from the same untrusted package must never be accepted as root.
        bundled_key = out / "bundled.pub.pem"
        bundled_key.write_bytes(pubkey.read_bytes())
        try:
            verified_metadata(manifest=out / "Update.may",
                              signature=out / "Update.sig",
                              image=image, public_key=bundled_key)
        except ValueError:
            pass
        else:
            raise AssertionError("accepted untrusted bundled public key")
        bundled_key.unlink()
        # Wrong signature fails cryptographic validation.
        corrupt = root / "tampered.sig"
        corrupt.write_bytes(bytes([sig[0] ^ 1]) + sig[1:])
        bad = run("openssl", "pkeyutl", "-verify", "-rawin", "-pubin",
                  "-inkey", str(pubkey), "-in", str(out / "Update.may"),
                  "-sigfile", str(corrupt), check=False)
        assert bad.returncode != 0
        try:
            verified_metadata(manifest=out / "Update.may", signature=corrupt,
                              image=image, public_key=pubkey)
        except ValueError:
            pass
        else:
            raise AssertionError("verifier accepted modified detached signature")
        # Changing even one byte of manifest invalidates signature.
        changed = root / "tampered.may"
        changed.write_bytes(content.replace(b"eta_minutes=7\n", b"eta_minutes=8\n"))
        bad = run("openssl", "pkeyutl", "-verify", "-rawin", "-pubin",
                  "-inkey", str(pubkey), "-in", str(changed),
                  "-sigfile", str(out / "Update.sig"), check=False)
        assert bad.returncode != 0
        # Image modification breaks SHA-256 commitment inside signed manifest.
        image.write_bytes(b"abd")
        assert hashlib.sha256(image.read_bytes()).hexdigest().encode() not in content
        try:
            verified_metadata(manifest=out / "Update.may",
                              signature=out / "Update.sig",
                              image=image, public_key=pubkey)
        except ValueError:
            pass
        else:
            raise AssertionError("verifier accepted modified image with genuine signed metadata")
        # Signed release directory cannot be reused and mixed with old files.
        rejected = run(
            sys.executable, str(TOOL), "--image", str(image),
            "--private-key", str(key), "--output-dir", str(out),
            "--version", "0.5.0-pre3", "--date", "2026-11-19",
            "--eta-minutes", "7", "--changelog", "Offline signed test", check=False)
        assert rejected.returncode != 0
        # Invalid metadata must be rejected before any signing.
        for kwargs in [
            dict(version="1.0", released="2026-11-19", changelog="OK", eta=5),
            dict(version="1.0.0", released="2026-02-30", changelog="OK", eta=5),
            dict(version="1.0.0", released="2026-11-19", changelog="newline\ninjected", eta=5),
            dict(version="1.0.0", released="2026-11-19", changelog="OK", eta=1441),
        ]:
            try:
                manifest_bytes(image=image, **kwargs)
            except ValueError:
                pass
            else:
                raise AssertionError(f"accepted malformed release metadata {kwargs}")
        print("PASS: offline Ed25519 Release packaging; signed bytes, image digest, tamper rejection")
        print("PASS: separate read-only verifier rejects untrusted key, signature and ISO tampering")
        print("PASS: private key kept out of published files; invalid manifest and stale output rejected")
        print("NOTICE: test image=3 bytes; real ISO streaming install remains unimplemented")


if __name__ == "__main__":
    main()
