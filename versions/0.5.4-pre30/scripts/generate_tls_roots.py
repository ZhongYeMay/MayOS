#!/usr/bin/env python3
"""Generate BearSSL trust anchors from the pinned Mozilla CA extract."""
import argparse
import hashlib
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives.asymmetric import ec, rsa
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat

from generate_tls_x509_fixture import c_array

ROOT = Path(__file__).resolve().parents[1]
CURVES = {"secp256r1": "BR_EC_secp256r1", "secp384r1": "BR_EC_secp384r1",
          "secp521r1": "BR_EC_secp521r1"}


def unsigned(number):
    return number.to_bytes((number.bit_length() + 7) // 8, "big")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    pem = (ROOT / "assets/mozilla-ca.pem").read_bytes()
    expected = (ROOT / "assets/mozilla-ca.sha256").read_text().split()[0]
    if hashlib.sha256(pem).hexdigest() != expected:
        raise SystemExit("Mozilla CA bundle checksum mismatch")
    certs = x509.load_pem_x509_certificates(pem)
    data, anchors = [], []
    for index, cert in enumerate(certs):
        if not cert.extensions.get_extension_for_class(x509.BasicConstraints).value.ca:
            raise SystemExit("The root store contains a non-CA certificate")
        dn = f"mozilla_dn_{index}"
        public = f"mozilla_key_{index}"
        data.append(c_array(dn, cert.subject.public_bytes()))
        key = cert.public_key()
        if isinstance(key, rsa.RSAPublicKey):
            exponent = f"mozilla_exponent_{index}"
            numbers = key.public_numbers()
            data.extend((c_array(public, unsigned(numbers.n)), c_array(exponent, unsigned(numbers.e))))
            anchor_key = (f"BR_KEYTYPE_RSA, {{.rsa = {{{public}, sizeof({public}), "
                          f"{exponent}, sizeof({exponent})}}}}")
        elif isinstance(key, ec.EllipticCurvePublicKey) and key.curve.name in CURVES:
            data.append(c_array(public, key.public_bytes(Encoding.X962, PublicFormat.UncompressedPoint)))
            anchor_key = (f"BR_KEYTYPE_EC, {{.ec = {{{CURVES[key.curve.name]}, "
                          f"{public}, sizeof({public})}}}}")
        else:
            raise SystemExit(f"Unsupported root key at certificate {index}")
        anchors.append(f"    {{{{{dn}, sizeof({dn})}}, BR_X509_TA_CA, {{{anchor_key}}}}},")
    if not anchors:
        raise SystemExit("Empty CA bundle")
    args.output.write_text("/* Generated from Mozilla certificate data. */\n" + "".join(data) +
                           "static const br_x509_trust_anchor mozilla_anchors[] = {\n" +
                           "\n".join(anchors) + "\n};\n")
    print(f"Generated {len(anchors)} Mozilla CA anchors")


if __name__ == "__main__":
    main()
