#!/usr/bin/env python3
"""Find and apply new releases of the pinned dependencies (used by maintenance.yml).

  update_dep.py current <dep>                    pinned version
  update_dep.py check <dep> < releases.json      newer release, or nothing
  update_dep.py url <dep> <version>              source tarball URL
  update_dep.py apply <dep> <version> --sha256 H rewrite the pins in the tree
"""
import argparse
import json
import os
import re
import sys

VERSION = r"\d+(?:\.\d+){1,3}"

# "pins": (file, pattern) pairs; group 1 is kept, group 2 is replaced.
DEPS = {
    "mbedtls": {
        "tag": r"(?:mbedtls-|v)?(%s)" % VERSION,
        "allowed": r"4\.1\.\d+",  # the LTS branch CMakeLists.txt insists on
        "url": "https://github.com/Mbed-TLS/mbedtls/releases/download/"
               "mbedtls-{v}/mbedtls-{v}.tar.bz2",
        "pins": [("CMakeLists.txt", r"(# Mbed TLS )(%s)(?= LTS \()" % VERSION)],
    },
    "zlib": {
        "tag": r"v?(%s)" % VERSION,
        "allowed": VERSION,
        "url": "https://github.com/madler/zlib/releases/download/"
               "v{v}/zlib-{v}.tar.gz",
        "pins": [("README.md", r"(\[zlib\]\(https://zlib\.net/\) )(%s)(?= inflate)" % VERSION)],
    },
}


class Refused(Exception):
    pass


def key(version):
    parts = [int(p) for p in version.split(".")]
    return parts + [0] * (4 - len(parts))


def read(root, name):
    with open(os.path.join(root, name), encoding="utf-8", newline="") as f:
        return f.read()


def url_pattern(dep):
    """Regex for the dependency's URL line and the URL_HASH line below it."""
    url = re.escape(DEPS[dep]["url"]).replace(re.escape("{v}"), "(%s)" % VERSION)
    return r"(URL )%s(\s+URL_HASH SHA256=)[0-9a-f]{64}" % url


def current(root, dep):
    m = re.search(url_pattern(dep), read(root, "CMakeLists.txt"))
    if not m:
        raise Refused("CMakeLists.txt: no pinned URL for %s" % dep)
    return m.group(2)


def check(root, dep, releases):
    if not isinstance(releases, list):
        raise Refused("expected a list of releases, got: %.200s" % json.dumps(releases))
    found = []
    for release in releases:
        if release.get("draft") or release.get("prerelease"):
            continue
        m = re.fullmatch(DEPS[dep]["tag"], release.get("tag_name") or "")
        if m and re.fullmatch(DEPS[dep]["allowed"], m.group(1)):
            found.append(m.group(1))
    newest = max(found, key=key, default=None)
    if newest and key(newest) > key(current(root, dep)):
        return newest
    return None


def substitute(text, name, pattern, replacement):
    text, count = re.subn(pattern, replacement, text)
    if count != 1:
        raise Refused("%s: expected one match for %s, found %d" % (name, pattern, count))
    return text


def apply(root, dep, version, sha256):
    spec = DEPS[dep]
    if not re.fullmatch(spec["allowed"], version):
        raise Refused("version %r is not allowed for %s" % (version, dep))
    if not re.fullmatch(r"[0-9a-f]{64}", sha256):
        raise Refused("not a SHA-256: %r" % sha256)
    old = current(root, dep)
    if key(version) <= key(old):
        raise Refused("%s %s is not newer than %s" % (dep, version, old))

    url = spec["url"].format(v=version)
    files = {"CMakeLists.txt": substitute(
        read(root, "CMakeLists.txt"), "CMakeLists.txt", url_pattern(dep),
        lambda m: m.group(1) + url + m.group(m.lastindex) + sha256)}
    for name, pattern in spec["pins"]:
        text = files.get(name) or read(root, name)
        files[name] = substitute(text, name, pattern, lambda m: m.group(1) + version)
    # Written only once every pin was found, so a failure leaves the tree alone.
    for name, text in files.items():
        with open(os.path.join(root, name), "w", encoding="utf-8", newline="") as f:
            f.write(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    sub = parser.add_subparsers(dest="command", required=True)
    for command in ("current", "check", "url", "apply"):
        p = sub.add_parser(command)
        p.add_argument("dep", choices=sorted(DEPS))
        if command in ("url", "apply"):
            p.add_argument("version")
        if command == "apply":
            p.add_argument("--sha256", required=True)
    args = parser.parse_args()

    try:
        if args.command == "current":
            print(current(args.root, args.dep))
        elif args.command == "check":
            newer = check(args.root, args.dep, json.load(sys.stdin))
            if newer:
                print(newer)
        elif args.command == "url":
            print(DEPS[args.dep]["url"].format(v=args.version))
        else:
            apply(args.root, args.dep, args.version, args.sha256)
    except (Refused, OSError, ValueError) as e:
        print("update_dep: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
