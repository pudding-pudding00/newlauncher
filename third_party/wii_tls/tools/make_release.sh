#!/bin/bash
# Builds the prebuilt Wii release package:
#   dist/libwiitls-<version>-wii/  and  dist/libwiitls-<version>-wii.zip
#
# Run in a devkitPro shell (DEVKITPRO/DEVKITPPC set, cmake, zip):
#   tools/make_release.sh
#
# A fresh build directory with the default options is used, so the package
# does not depend on whatever an existing build-wii was configured with. At
# the end the example is compiled and linked against nothing but the package,
# with the flags docs/usage.md tells users to use.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
: "${DEVKITPRO:?run in a devkitPro shell}"
: "${DEVKITPPC:?run in a devkitPro shell}"

VERSION=$(sed -n 's/^project(libwiitls VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
[ -n "$VERSION" ] || { echo "version not found in CMakeLists.txt" >&2; exit 1; }
NAME=libwiitls-$VERSION-wii
BUILD=build-release
OUT=dist/$NAME

FETCH=()
if [ -d build-host/_deps/mbedtls-src ]; then # reuse the fetched sources if present
    FETCH=(-DFETCHCONTENT_SOURCE_DIR_MBEDTLS="$ROOT/build-host/_deps/mbedtls-src")
fi

rm -rf "$BUILD" "$OUT" "dist/$NAME.zip"
cmake -S . -B "$BUILD" -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Wii.cmake" \
    -DWTLS_BUILD_HWTEST=OFF -DWTLS_BUILD_BENCH=OFF -DWTLS_BUILD_EXAMPLES=ON \
    "${FETCH[@]}" >/dev/null
cmake --build "$BUILD" -j8 >/dev/null # includes tools/check_ct.cmake

if [ ${#FETCH[@]} -gt 0 ]; then MBED="$ROOT/build-host/_deps/mbedtls-src"; else MBED="$BUILD/_deps/mbedtls-src"; fi
[ -d "$MBED/include/mbedtls" ] || { echo "Mbed TLS sources not found ($MBED)" >&2; exit 1; }

mkdir -p "$OUT/lib" "$OUT/include" "$OUT/example" "$OUT/docs" "$OUT/licenses"

# Libraries
cp "$BUILD/libwiitls.a" "$OUT/lib/"
cp "$BUILD/_deps/mbedtls-build/library/libmbedtls.a" \
   "$BUILD/_deps/mbedtls-build/library/libmbedx509.a" \
   "$BUILD/_deps/mbedtls-build/tf-psa-crypto/core/libtfpsacrypto.a" "$OUT/lib/"

# Headers: libwiitls, the public Mbed TLS / TF-PSA-Crypto headers (for the
# MBEDTLS_X509_BADCERT_* flags) and the configuration they were built with.
cp -r include/wiitls "$OUT/include/"
for d in "$MBED/include" "$MBED/tf-psa-crypto/include" \
         "$MBED/tf-psa-crypto/drivers/builtin/include" \
         "$BUILD/_deps/mbedtls-build/tf-psa-crypto/include"; do
    [ -d "$d" ] || continue
    (cd "$d" && find . -name '*.h') | while read -r h; do
        if [ -e "$OUT/include/$h" ] && ! cmp -s "$d/$h" "$OUT/include/$h"; then
            echo "header clash: $h" >&2; exit 1
        fi
        mkdir -p "$OUT/include/$(dirname "$h")"
        cp "$d/$h" "$OUT/include/$h"
    done
done
cp config/wtls_mbedtls_config.h config/wtls_crypto_config.h "$OUT/include/"

# Example: source, prebuilt DOL, a plain devkitPro Makefile
cp examples/https_get/main.c "$BUILD/examples/https_get/https_get.dol" "$OUT/example/"
cat > "$OUT/example/Makefile" <<'EOF'
# Builds https_get.dol against the prebuilt package (devkitPro shell).
WIITLS  ?= ..
CC      := $(DEVKITPPC)/bin/powerpc-eabi-gcc
CFLAGS  := -O2 -mrvl -mcpu=750 -meabi -mhard-float -DGEKKO -D__wii__ \
           -I$(WIITLS)/include -I$(DEVKITPRO)/libogc/include \
           -DMBEDTLS_CONFIG_FILE='"wtls_mbedtls_config.h"' \
           -DTF_PSA_CRYPTO_CONFIG_FILE='"wtls_crypto_config.h"'
LDFLAGS := -mrvl -mcpu=750 -meabi -mhard-float -L$(WIITLS)/lib -L$(DEVKITPRO)/libogc/lib/wii
LIBS    := -lwiitls -lmbedtls -lmbedx509 -ltfpsacrypto -lwiitls -lwiiuse -lbte -logc -lm

https_get.dol: https_get.elf
	$(DEVKITPRO)/tools/bin/elf2dol $< $@

https_get.elf: main.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) $(LIBS) -o $@

clean:
	rm -f https_get.elf https_get.dol
EOF

# Documentation and licences
cp README.md "$OUT/"
cp docs/usage.md docs/performance.md "$OUT/docs/"
cp LICENSE "$OUT/licenses/LICENSE-libwiitls.txt"
cp "$MBED/LICENSE" "$OUT/licenses/LICENSE-mbedtls.txt"
cat > "$OUT/licenses/NOTICE-ca-bundle.txt" <<'EOF'
libwiitls embeds the Mozilla CA certificate bundle (via curl's extract,
https://curl.se/docs/caextract.html). That data is licensed under the
Mozilla Public License 2.0: https://www.mozilla.org/MPL/2.0/
EOF
cat > "$OUT/BUILDINFO.txt" <<EOF
libwiitls $VERSION, prebuilt for the Wii (devkitPPC, libogc)
commit:   $(git rev-parse HEAD)$(git diff --quiet HEAD -- . ':!dist' || echo " (with local changes)")
built:    $(date -u +%Y-%m-%dT%H:%MZ)
compiler: $("$DEVKITPPC/bin/powerpc-eabi-gcc" --version | head -1)
options:  $(grep -E '^WTLS_(EVEREST|ACCEL|OPT|BN_ASM|P256M):' "$BUILD/CMakeCache.txt" | sed 's/:[A-Z]*=/=/' | tr '\n' ' ')
EOF

# Self-check: build the example from the package alone
make -s -C "$OUT/example" WIITLS="$ROOT/$OUT" https_get.elf
rm -f "$OUT/example/https_get.elf"
echo "package self-check: example links against the package"

(cd dist && rm -f "$NAME.zip" && cmake -E tar cf "$NAME.zip" --format=zip "$NAME")
echo "dist/$NAME.zip"
