#!/usr/bin/env python3
"""CI-ONLY disposable RSA private key -> native BearSSL TLS server fixture.

Outputs an ephemeral test-only C header in build/, never in git. It contains
RSA CRT private components and test entropy, deliberately NOT an official
release signing key. This header must NEVER be used by production MayKernel.
"""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import subprocess
from generate_tls_x509_fixture import tlv, c_array


def rsa_crt_from_pem(path: Path) -> tuple[int,list[bytes]]:
    raw=subprocess.run(
        ["openssl","rsa","-traditional","-in",str(path),"-outform","DER"],
        check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
    ).stdout
    tag,_,start,end=tlv(raw,0)
    if tag!=0x30 or end!=len(raw):
        raise ValueError("test private key is not canonical DER RSAPrivateKey")
    values=[]
    pos=start
    for i in range(9):
        kind,_,vstart,vend=tlv(raw,pos)
        if kind!=0x02 or vend<=vstart:
            raise ValueError("RSA key field not positive integer")
        data=raw[vstart:vend]
        if data[0]&0x80:
            raise ValueError("negative RSA integer")
        if data==b"\x00":
            if i!=0:
                raise ValueError("unexpected zero RSA key component")
        elif data[0]==0:
            if len(data)<2 or data[1]<0x80:
                raise ValueError("nonminimal RSA integer")
            data=data[1:]
        values.append(data)
        pos=vend
    if pos!=end:
        raise ValueError("unsupported RSA multi-prime/private-key extras")
    version,modulus,exponent,_,p,q,dp,dq,iq=values
    if version!=b"\x00" or len(modulus)!=256 or modulus[0]<0x80 or \
       exponent!=b"\x01\x00\x01":
        raise ValueError("requires single-prime-pair RSA-2048, e=65537")
    for item in (p,q,dp,dq,iq):
        if not 1<=len(item)<=128:
            raise ValueError("invalid RSA CRT factor length")
    return len(modulus)*8,[p,q,dp,dq,iq]


def main() -> None:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--leaf-key",required=True,type=Path)
    parser.add_argument("--output",required=True,type=Path)
    args=parser.parse_args()
    bits,components=rsa_crt_from_pem(args.leaf_key)
    names=["mayos_test_tls_p","mayos_test_tls_q","mayos_test_tls_dp",
           "mayos_test_tls_dq","mayos_test_tls_iq"]
    header="/* GENERATED TEST-ONLY RSA CRT KEY, NOT A PRODUCTION CREDENTIAL. */\n"
    header+="#ifndef MAYOS_TEST_TLS_SERVER_SECRET_H\n#define MAYOS_TEST_TLS_SERVER_SECRET_H\n"
    header+=f"#define MAYOS_TEST_TLS_RSA_BITS {bits}u\n"
    for name,value in zip(names,components):
        header+=c_array(name,value)
    # Random test-only seed is baked into the temporary guest ISO.
    # Real TLS updates must use a health-checked DRBG from trusted entropy.
    header+=c_array("mayos_test_tls_client_seed",os.urandom(48))
    header+=c_array("mayos_test_tls_server_seed",os.urandom(48))
    header+="#endif\n"
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(header,encoding="ascii")
    print("PASS: temporary RSA test server key generated in build/ (NO official key)")
    print("NOTICE: test TLS seed/header must NOT be distributed as a production trust source")


if __name__=="__main__":
    main()
