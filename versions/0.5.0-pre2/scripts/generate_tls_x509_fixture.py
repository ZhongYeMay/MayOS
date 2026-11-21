#!/usr/bin/env python3
"""Generate a test-only BearSSL CA anchor and leaf DER from OpenSSL PEM certs.

No secret keys are parsed, exposed or emitted. ASN.1 parsing below is a
strict, build-time decoder for our ephemeral RSA fixture, NOT a TLS parser.
This file is NEVER invoked in production MayKernel builds.
"""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
from pathlib import Path
import subprocess


def der_from_pem(path: Path) -> bytes:
    result=subprocess.run(
        ["openssl","x509","-in",str(path),"-outform","DER"],
        stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True,
    )
    return result.stdout


def tlv(data: bytes, offset: int):
    if offset+2>len(data):
        raise ValueError("ASN.1 TLV outside certificate")
    begin=offset
    tag=data[offset]
    octet=data[offset+1]
    offset+=2
    if octet&0x80:
        count=octet&0x7f
        if count==0 or count>4 or offset+count>len(data):
            raise ValueError("indefinite or oversized ASN.1 length")
        if data[offset]==0:
            raise ValueError("non-minimal ASN.1 length")
        n=int.from_bytes(data[offset:offset+count],"big")
        if n<128:
            raise ValueError("non-minimal long-form DER")
        offset+=count
    else:
        n=octet
    end=offset+n
    if end>len(data):
        raise ValueError("ASN.1 length beyond certificate")
    return tag,begin,offset,end


def root_name_and_rsa_key(der: bytes):
    kind,_,start,end=tlv(der,0)
    if kind!=0x30 or end!=len(der):
        raise ValueError("expected X.509 Certificate SEQUENCE")
    kind,_,start,end=tlv(der,start)
    if kind!=0x30:
        raise ValueError("expected TBSCertificate SEQUENCE")
    pos=start
    if der[pos]==0xa0:
        _,_,_,pos=tlv(der,pos)  # optional explicit version
    for expected in [0x02,0x30,0x30,0x30]:
        tag,_,_,pos=tlv(der,pos)  # serial, algorithm, issuer, validity
        if tag!=expected:
            raise ValueError("unexpected TBSCertificate field")
    tag,subject_start,_,pos=tlv(der,pos)
    if tag!=0x30:
        raise ValueError("root Subject is not DER Name")
    subject_der=der[subject_start:pos]
    tag,_,spki_start,spki_end=tlv(der,pos)
    if tag!=0x30:
        raise ValueError("no SubjectPublicKeyInfo")
    alg_tag,_,alg_start,alg_end=tlv(der,spki_start)
    if alg_tag!=0x30:
        raise ValueError("invalid SPKI algorithm")
    oid_tag,_,oid_start,oid_end=tlv(der,alg_start)
    if oid_tag!=0x06 or der[oid_start:oid_end]!=bytes.fromhex("2a864886f70d010101"):
        raise ValueError("only rsaEncryption SPKI supported by this CI fixture")
    tag,_,bit_start,bit_end=tlv(der,alg_end)
    if tag!=0x03 or bit_start>=bit_end or der[bit_start]!=0:
        raise ValueError("malformed RSA BIT STRING")
    public=der[bit_start+1:bit_end]
    tag,_,rsa_start,rsa_end=tlv(public,0)
    if tag!=0x30 or rsa_end!=len(public):
        raise ValueError("malformed DER RSAPublicKey SEQUENCE")
    tag,_,n_start,n_end=tlv(public,rsa_start)
    if tag!=0x02:
        raise ValueError("RSA modulus not INTEGER")
    tag,_,e_start,e_end=tlv(public,n_end)
    if tag!=0x02 or e_end!=rsa_end:
        raise ValueError("RSA exponent not INTEGER")
    modulus=public[n_start:n_end]
    if modulus.startswith(b"\0"):
        modulus=modulus[1:]
    exponent=public[e_start:e_end]
    if len(modulus)!=256 or exponent!=b"\x01\x00\x01":
        raise ValueError("expected ephemeral RSA-2048 public key, exponent 65537")
    if len(subject_der)>512:
        raise ValueError("excessive test anchor DN")
    return subject_der,modulus,exponent


def c_array(name: str, data: bytes) -> str:
    hexes=["0x"+f"{byte:02x}" for byte in data]
    lines=[
        "    "+", ".join(hexes[i:i+12])+","
        for i in range(0,len(hexes),12)
    ]
    return f"static unsigned char {name}[{len(data)}] = {{\n"+\
           "\n".join(lines)+"\n};\n"


def main() -> None:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--ca",required=True,type=Path)
    p.add_argument("--leaf",required=True,type=Path)
    p.add_argument("--output",required=True,type=Path)
    args=p.parse_args()
    subprocess.run(
        ["openssl","verify","-CAfile",str(args.ca),str(args.leaf)],
        stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True,
    )
    root=der_from_pem(args.ca)
    leaf=der_from_pem(args.leaf)
    dn,n,e=root_name_and_rsa_key(root)
    if not 300<len(leaf)<=8192:
        raise ValueError("test leaf DER size outside safe bounds")
    now=datetime.now(timezone.utc)
    # BearSSL measures days since Jan 1 of Gregorian year 0 (a leap year);
    # Python ordinal starts with Jan 1 of year 1 = day 1.
    days=now.date().toordinal()+365
    seconds=now.hour*3600+now.minute*60+now.second
    contents=(
        "/* CI-generated TEST-ONLY X.509 CA and leaf. NO production trust. */\n"
        "#ifndef MAYOS_TLS_TEST_X509_FIXTURE_H\n"
        "#define MAYOS_TLS_TEST_X509_FIXTURE_H\n"
        +f"#define MAYOS_TLS_FIXTURE_DAYS {days}u\n"
        +f"#define MAYOS_TLS_FIXTURE_SECONDS {seconds}u\n"
        +c_array("mayos_test_ca_dn",dn)
        +c_array("mayos_test_ca_rsa_modulus",n)
        +c_array("mayos_test_ca_rsa_exponent",e)
        +c_array("mayos_test_leaf_der",leaf)
        +"#endif\n"
    )
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(contents,encoding="ascii")
    print(f"PASS: test-only RSA-2048 CA, {len(leaf)}-byte signed leaf DER and bounded DN compiled as C arrays")


if __name__=="__main__":
    main()
