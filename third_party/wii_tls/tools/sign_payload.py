#!/usr/bin/env python3
"""Signs payloads for wtls_sig_verify_p256() with ECDSA P-256 / SHA-256 (needs the openssl CLI).

    sign_payload.py genkey   <key.pem>                 create a signing key (keep it offline!)
    sign_payload.py pubkey-c <key.pem> [name]          print the public key as a C array (65-byte point)
    sign_payload.py sign     <key.pem> <file> [--raw]  write <file>.sig (DER, or raw r||s with --raw)
"""
import subprocess
import sys


def openssl(*args, stdin=None):
    return subprocess.run(["openssl", *args], input=stdin, check=True, capture_output=True).stdout


def point(key):
    der = openssl("ec", "-in", key, "-pubout", "-outform", "DER")
    if len(der) != 91 or der[26] != 4:
        raise SystemExit("not a P-256 key")
    return der[26:]


def der_to_raw(sig):
    # SEQUENCE { INTEGER r, INTEGER s }, short-form lengths (always the case for P-256)
    assert sig[0] == 0x30
    i = 2 if sig[1] < 0x80 else 3
    vals = []
    for _ in range(2):
        assert sig[i] == 0x02
        n = sig[i + 1]
        vals.append(int.from_bytes(sig[i + 2:i + 2 + n], "big"))
        i += 2 + n
    return vals[0].to_bytes(32, "big") + vals[1].to_bytes(32, "big")


def main(argv):
    if len(argv) >= 3 and argv[1] == "genkey":
        openssl("ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", argv[2])
        print("wrote", argv[2], "- keep it offline and backed up")
    elif len(argv) >= 3 and argv[1] == "pubkey-c":
        name = argv[3] if len(argv) > 3 else "k_update_pubkey"
        p = point(argv[2])
        rows = ",\n".join("    " + ", ".join("0x%02x" % b for b in p[i:i + 8]) for i in range(0, len(p), 8))
        print("static const uint8_t %s[65] = {\n%s};" % (name, rows))
    elif len(argv) >= 4 and argv[1] == "sign":
        sig = openssl("dgst", "-sha256", "-sign", argv[2], argv[3])
        if "--raw" in argv:
            sig = der_to_raw(sig)
        with open(argv[3] + ".sig", "wb") as f:
            f.write(sig)
        print("wrote", argv[3] + ".sig", len(sig), "bytes")
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
