#!/usr/bin/env python3
"""Writes an SPDX 2.3 JSON SBOM for libwiitls from the repository itself.

    python3 tools/gen_sbom.py [--out sbom.spdx.json] [--check]

Sources: the project version and the FetchContent URL/SHA-256 pairs in
CMakeLists.txt (Mbed TLS, zlib) and data/cacert.pem.sha256 (the embedded CA
bundle). The document is a function of the repository contents and
SOURCE_DATE_EPOCH, so rebuilding it gives the same bytes. --check only
verifies that the inputs parse (used by the CI test).
"""
import argparse
import datetime
import json
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as f:
        return f.read()


def project_version(cmake):
    m = re.search(r"^project\(libwiitls VERSION ([0-9.]+)", cmake, re.M)
    if not m:
        raise SystemExit("gen_sbom: project version not found in CMakeLists.txt")
    return m.group(1)


def fetched(cmake):
    """{name: (url, sha256)} for every FetchContent_Declare with URL + URL_HASH."""
    out = {}
    for m in re.finditer(r"FetchContent_Declare\((\w+)\s+URL\s+(\S+)\s+URL_HASH SHA256=([0-9a-f]{64})", cmake):
        out[m.group(1)] = (m.group(2), m.group(3))
    return out


def dep_version(name, url):
    m = re.search(r"%s[-_]v?([0-9][0-9.]*[0-9])" % re.escape(name), url)
    return m.group(1) if m else "unknown"


LICENSES = {"mbedtls": "Apache-2.0", "zlib": "Zlib"}
HOMEPAGES = {"mbedtls": "https://github.com/Mbed-TLS/mbedtls", "zlib": "https://zlib.net/"}


def build():
    cmake = read("CMakeLists.txt")
    version = project_version(cmake)
    deps = fetched(cmake)
    for need in ("mbedtls", "zlib"):
        if need not in deps:
            raise SystemExit("gen_sbom: FetchContent_Declare(%s) with URL_HASH not found" % need)
    cacert_sha = read("data/cacert.pem.sha256").split()[0]
    epoch = int(os.environ.get("SOURCE_DATE_EPOCH", "0"))
    created = datetime.datetime.fromtimestamp(epoch, datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    packages = [{
        "SPDXID": "SPDXRef-libwiitls",
        "name": "libwiitls",
        "versionInfo": version,
        "downloadLocation": "https://github.com/pudding-pudding00/libwiitls",
        "filesAnalyzed": False,
        "licenseConcluded": "Apache-2.0",
        "licenseDeclared": "Apache-2.0",
        "copyrightText": "NOASSERTION",
    }]
    rels = [{"spdxElementId": "SPDXRef-DOCUMENT", "relationshipType": "DESCRIBES", "relatedSpdxElement": "SPDXRef-libwiitls"}]
    for name in ("mbedtls", "zlib"):
        url, sha = deps[name]
        packages.append({
            "SPDXID": "SPDXRef-%s" % name,
            "name": name,
            "versionInfo": dep_version(name, url),
            "downloadLocation": url,
            "homepage": HOMEPAGES[name],
            "filesAnalyzed": False,
            "checksums": [{"algorithm": "SHA256", "checksumValue": sha}],
            "licenseConcluded": LICENSES[name],
            "licenseDeclared": LICENSES[name],
            "copyrightText": "NOASSERTION",
        })
        rels.append({"spdxElementId": "SPDXRef-libwiitls", "relationshipType": "DEPENDS_ON",
                     "relatedSpdxElement": "SPDXRef-%s" % name})
    packages.append({
        "SPDXID": "SPDXRef-cacert",
        "name": "mozilla-ca-bundle",
        "versionInfo": "data/cacert.pem",
        "downloadLocation": "https://curl.se/ca/cacert.pem",
        "filesAnalyzed": False,
        "checksums": [{"algorithm": "SHA256", "checksumValue": cacert_sha}],
        "licenseConcluded": "MPL-2.0",
        "licenseDeclared": "MPL-2.0",
        "copyrightText": "NOASSERTION",
        "comment": "Embedded in the library as src/gen/ca_bundle.c (tools/gen_ca_bundle.py); optional (WTLS_EMBED_CA).",
    })
    rels.append({"spdxElementId": "SPDXRef-libwiitls", "relationshipType": "CONTAINS", "relatedSpdxElement": "SPDXRef-cacert"})
    return {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": "libwiitls-%s" % version,
        "documentNamespace": "https://github.com/pudding-pudding00/libwiitls/sbom/%s/%s" % (version, cacert_sha[:16]),
        "creationInfo": {"created": created, "creators": ["Tool: libwiitls-tools/gen_sbom.py"]},
        "packages": packages,
        "relationships": rels,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="sbom.spdx.json")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    doc = build()
    text = json.dumps(doc, indent=2, sort_keys=True) + "\n"
    if a.check:
        print("gen_sbom: ok (%d packages)" % len(doc["packages"]))
        return 0
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote", a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
