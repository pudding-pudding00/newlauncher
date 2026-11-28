#!/bin/bash
# Creates a zip whose bytes depend only on the file names and contents (and
# SOURCE_DATE_EPOCH): sorted entries, normalised timestamps, no extra attributes.
#
#   SOURCE_DATE_EPOCH=<unix time> tools/deterministic_zip.sh <dir> <out.zip>
#
# <dir> is stored under its own name (relative to its parent), like `zip -r`.
set -euo pipefail
dir=${1:?usage: deterministic_zip.sh <dir> <out.zip>}
out=${2:?usage: deterministic_zip.sh <dir> <out.zip>}
: "${SOURCE_DATE_EPOCH:?set SOURCE_DATE_EPOCH}"
parent=$(cd "$(dirname "$dir")" && pwd)
name=$(basename "$dir")
absout=$(cd "$(dirname "$out")" && pwd)/$(basename "$out")
rm -f "$absout"
# GNU date takes -d "@N", BSD date (macOS) -r N
stamp=$(date -u -d "@$SOURCE_DATE_EPOCH" +%Y%m%d%H%M.%S 2>/dev/null || date -u -r "$SOURCE_DATE_EPOCH" +%Y%m%d%H%M.%S)
# zip stores DOS time (even seconds, local time): pin both the files and TZ.
export TZ=UTC
(cd "$parent" && find "$name" -exec touch -h -t "$stamp" {} + && \
    find "$name" -type f | LC_ALL=C sort | zip -X -q -@ "$absout")
