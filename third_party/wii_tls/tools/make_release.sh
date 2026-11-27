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
if [ -d build-host/_deps/zlib-src ]; then
    FETCH+=(-DFETCHCONTENT_SOURCE_DIR_ZLIB="$ROOT/build-host/_deps/zlib-src")
fi

rm -rf "$BUILD" "$OUT" "dist/$NAME.zip"
cmake -S . -B "$BUILD" -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Wii.cmake" \
    -DWTLS_BUILD_HWTEST=OFF -DWTLS_BUILD_BENCH=OFF -DWTLS_BUILD_EXAMPLES=ON \
    "${FETCH[@]}" >/dev/null
cmake --build "$BUILD" -j8 >/dev/null # includes tools/check_ct.cmake

if [ -d build-host/_deps/mbedtls-src ]; then MBED="$ROOT/build-host/_deps/mbedtls-src"; else MBED="$BUILD/_deps/mbedtls-src"; fi
[ -d "$MBED/include/mbedtls" ] || { echo "Mbed TLS sources not found ($MBED)" >&2; exit 1; }

mkdir -p "$OUT/example" "$OUT/docs" "$OUT/licenses"
cmake --install "$BUILD" --prefix "$ROOT/$OUT" >/dev/null
rm -f "$OUT/lib/libmbedcrypto.a" # CMake < 3.28 cannot EXCLUDE_FROM_ALL Mbed TLS's legacy alias
test -f "$OUT/include/wiitls/http.h" || { echo "include/wiitls/http.h missing from the package" >&2; exit 1; }
test -f "$OUT/lib/cmake/wiitls/wiitlsConfig.cmake" || { echo "wiitlsConfig.cmake missing from the package" >&2; exit 1; }

# Examples: source, prebuilt DOLs, a plain devkitPro Makefile
cp examples/https_get/main.c "$BUILD/examples/https_get/https_get.dol" "$OUT/example/"
mkdir -p "$OUT/example/upload_mtls_abort"
cp examples/upload_mtls_abort/main.c examples/upload_mtls_abort/CMakeLists.txt "$OUT/example/upload_mtls_abort/"
if [ -f "$BUILD/examples/upload_mtls_abort/upload_mtls_abort.dol" ]; then
    cp "$BUILD/examples/upload_mtls_abort/upload_mtls_abort.dol" "$OUT/example/upload_mtls_abort/"
fi
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
cp README.md CHANGELOG.md "$OUT/"
cp docs/usage.md docs/performance.md "$OUT/docs/"
cp LICENSE "$OUT/licenses/LICENSE-libwiitls.txt"
cp "$MBED/LICENSE" "$OUT/licenses/LICENSE-mbedtls.txt"
if grep -q '^WTLS_GZIP:BOOL=ON' "$BUILD/CMakeCache.txt"; then # zlib's inflate is compiled into libwiitls.a
    if [ -d build-host/_deps/zlib-src ]; then ZLIB="$ROOT/build-host/_deps/zlib-src"; else ZLIB="$BUILD/_deps/zlib-src"; fi
    cp "$ZLIB/LICENSE" "$OUT/licenses/LICENSE-zlib.txt"
fi
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
options:  $(grep -E '^WTLS_(EVEREST|ACCEL|OPT|BN_ASM|P256M|GZIP):' "$BUILD/CMakeCache.txt" | sed 's/:[A-Z]*=/=/' | tr '\n' ' ')
EOF

# Self-check: build the example via Makefile and via find_package(wiitls)
make -s -C "$OUT/example" WIITLS="$ROOT/$OUT" https_get.elf
rm -f "$OUT/example/https_get.elf"
CMAKE_CHK="$BUILD/consumer-check"
rm -rf "$CMAKE_CHK"
mkdir -p "$CMAKE_CHK"
cat > "$CMAKE_CHK/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.20)
project(wtls_pkg_check C)
find_package(wiitls $VERSION REQUIRED)
add_executable(pkg_check main.c)
target_link_libraries(pkg_check PRIVATE wiitls::wiitls)
ogc_create_dol(pkg_check)
EOF
cp examples/https_get/main.c "$CMAKE_CHK/main.c"
cmake -S "$CMAKE_CHK" -B "$CMAKE_CHK/build" -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Wii.cmake" \
    -DCMAKE_PREFIX_PATH="$ROOT/$OUT" >/dev/null
cmake --build "$CMAKE_CHK/build" -j4 >/dev/null
test -f "$CMAKE_CHK/build/pkg_check.dol"
rm -rf "$CMAKE_CHK"
echo "package self-check: Makefile and find_package(wiitls) link against the package"

(cd dist && rm -f "$NAME.zip" && cmake -E tar cf "$NAME.zip" --format=zip "$NAME")
echo "dist/$NAME.zip"
