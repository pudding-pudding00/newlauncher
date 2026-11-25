cmake_minimum_required(VERSION 3.20)
project(libwiitls VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON) # libogc headers need GNU extensions

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Platform selection
if(NINTENDO_WII)
    set(WTLS_PLATFORM_DEFAULT ogc)
    set(WTLS_BUILD_TESTS_DEFAULT OFF)
else()
    set(WTLS_PLATFORM_DEFAULT host)
    set(WTLS_BUILD_TESTS_DEFAULT ON)
endif()
set(WTLS_PLATFORM ${WTLS_PLATFORM_DEFAULT} CACHE STRING "Platform layer: ogc or host")
set_property(CACHE WTLS_PLATFORM PROPERTY STRINGS ogc host)
option(WTLS_BUILD_TESTS "Build libwiitls tests" ${WTLS_BUILD_TESTS_DEFAULT})

# ---------------------------------------------------------------------------
# Mbed TLS 4.1.1 LTS (release tarball contains all generated files: no Python needed)
# Crypto tuning knobs (Task 11, measured with bench/). They reach the Mbed TLS /
# TF-PSA-Crypto configs as WTLS_CFG_* defines, so they apply to every target
# below, including the fetched Mbed TLS.
option(WTLS_BN_ASM "PPC32 bignum assembly (MBEDTLS_HAVE_ASM)" ON)
option(WTLS_P256M "p256-m driver for P-256 (fails to build with 4.1.1 + PK)" OFF)
option(WTLS_EVEREST "X25519 via Everest/HACL* (MBEDTLS_ECDH_VARIANT_EVEREST_ENABLED)" ON)
option(WTLS_ACCEL "X25519 with 32-bit limbs (src/accel), hooked into Everest" ON)
option(WTLS_ACCEL_P256 "P-256 verify with 32-bit limbs (src/accel), hooked into the builtin driver" ON)
option(WTLS_THREAD_CHECK "Debug single-thread affinity checks (always ON in Debug builds)" OFF)
option(WTLS_BUILD_FUZZ "Build Clang libFuzzer harnesses in tests/fuzz" OFF)
set(WTLS_OPT O3 CACHE STRING "Optimisation level for library and Mbed TLS: O2 or O3")
set_property(CACHE WTLS_OPT PROPERTY STRINGS O2 O3)
if(NOT WTLS_OPT MATCHES "^O[23]$")
    message(FATAL_ERROR "WTLS_OPT must be O2 or O3, got '${WTLS_OPT}'")
endif()
if(NOT WTLS_BN_ASM)
    add_compile_definitions(WTLS_CFG_NO_BN_ASM)
endif()
if(WTLS_P256M)
    # Checked: tf_psa_crypto_check_config.h #errors ("Unsupported partial support
    # for ECC curves acceleration") because PK needs P-384/X25519 builtin as well.
    message(FATAL_ERROR "WTLS_P256M=ON does not build with Mbed TLS 4.1.1: p256-m "
            "would accelerate only P-256, and partial ECC acceleration together with "
            "PK is rejected (docs/benchmarks.md).")
endif()
if(WTLS_EVEREST)
    add_compile_definitions(WTLS_CFG_EVEREST)
endif()
if(WTLS_ACCEL)
    if(NOT WTLS_EVEREST)
        message(FATAL_ERROR "WTLS_ACCEL=ON needs WTLS_EVEREST=ON: it replaces Everest's X25519")
    endif()
    add_compile_definitions(WTLS_ACCEL_X25519)
endif()
if(WTLS_ACCEL_P256)
    add_compile_definitions(WTLS_ACCEL_P256)
endif()
if(WTLS_THREAD_CHECK OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(WTLS_THREAD_CHECK=1)
endif()
add_compile_options(-${WTLS_OPT}) # after CMAKE_C_FLAGS_<CONFIG>: the last -O wins (Mbed TLS: below)

include(FetchContent)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(GEN_FILES OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
set(INSTALL_MBEDTLS_HEADERS OFF CACHE BOOL "" FORCE)
set(INSTALL_TF_PSA_CRYPTO_HEADERS OFF CACHE BOOL "" FORCE)
set(MBEDTLS_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/wtls_mbedtls_config.h" CACHE FILEPATH "" FORCE)
set(TF_PSA_CRYPTO_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/wtls_crypto_config.h" CACHE FILEPATH "" FORCE)
set(_wtls_mbedtls_extra_args)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    list(APPEND _wtls_mbedtls_extra_args EXCLUDE_FROM_ALL)
endif()
FetchContent_Declare(mbedtls
    URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-4.1.1/mbedtls-4.1.1.tar.bz2
    URL_HASH SHA256=3359a349e23db3d5536fcee032ae7b2ecbfc08972fab643089b5cbf2a375c98c
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    ${_wtls_mbedtls_extra_args}
)
FetchContent_MakeAvailable(mbedtls)

# Verify the fetched Mbed TLS source matches the pinned 4.1.x LTS branch.
file(READ "${mbedtls_SOURCE_DIR}/include/mbedtls/build_info.h" _wtls_mb_info)
if(NOT _wtls_mb_info MATCHES "#define MBEDTLS_VERSION_MAJOR[ \t]+4" OR
   NOT _wtls_mb_info MATCHES "#define MBEDTLS_VERSION_MINOR[ \t]+1")
    message(FATAL_ERROR "Expected Mbed TLS 4.1.x LTS in ${mbedtls_SOURCE_DIR}")
endif()

# gzip Content-Encoding: the inflate half of zlib, compiled with Z_PREFIX into
# libwiitls itself (no -lz for applications, no clash with an application's zlib).
option(WTLS_GZIP "gzip Content-Encoding in the HTTP client (zlib inflate, built in)" ON)
if(WTLS_GZIP)
    FetchContent_Declare(zlib
        URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
        URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR _wtls_no_cmake # sources only: zlib's own CMakeLists is not used
    )
    FetchContent_MakeAvailable(zlib)
    add_library(wtls_zlib OBJECT
        ${zlib_SOURCE_DIR}/adler32.c
        ${zlib_SOURCE_DIR}/crc32.c
        ${zlib_SOURCE_DIR}/inffast.c
        ${zlib_SOURCE_DIR}/inflate.c
        ${zlib_SOURCE_DIR}/inftrees.c
        ${zlib_SOURCE_DIR}/zutil.c)
    target_include_directories(wtls_zlib PUBLIC ${zlib_SOURCE_DIR})
    target_compile_definitions(wtls_zlib PUBLIC Z_PREFIX)
    target_compile_options(wtls_zlib PRIVATE -w)
endif()
# Mbed TLS adds its own $<$<CONFIG:Release>:-O2> per target, after the global
# options: repeat WTLS_OPT on each target so it comes last and wins.
foreach(t mbedtls mbedx509 tfpsacrypto builtin everest p256-m platform utilities extras pqcp)
    if(TARGET ${t})
        target_compile_options(${t} PRIVATE "SHELL:-${WTLS_OPT}") # SHELL: escapes de-duplication against the global -O
    endif()
endforeach()
if(WTLS_EVEREST)
    # Everest's generic big-endian htole64 swaps only the two 32-bit halves: it
    # calls htobe32 (a no-op on big-endian) where it needs htole32, so X25519
    # returns wrong shared secrets on the Wii (tests/common/crypto_tests.c).
    # Fixed in the fetched source at configure time; idempotent.
    set(_wtls_endian
        ${mbedtls_SOURCE_DIR}/tf-psa-crypto/drivers/everest/include/tf-psa-crypto/private/everest/kremlin/c_endianness.h)
    file(READ ${_wtls_endian} _wtls_src)
    string(FIND "${_wtls_src}" "__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__" _wtls_be)
    string(FIND "${_wtls_src}" "__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__" _wtls_le)
    if(_wtls_be LESS 0 OR _wtls_le LESS _wtls_be)
        message(FATAL_ERROR "WTLS_EVEREST: unexpected layout of ${_wtls_endian}")
    endif()
    string(SUBSTRING "${_wtls_src}" 0 ${_wtls_be} _wtls_head)
    math(EXPR _wtls_len "${_wtls_le} - ${_wtls_be}")
    string(SUBSTRING "${_wtls_src}" ${_wtls_be} ${_wtls_len} _wtls_branch)
    string(SUBSTRING "${_wtls_src}" ${_wtls_le} -1 _wtls_tail)
    string(REPLACE "__low = htobe32(" "__low = htole32(" _wtls_branch "${_wtls_branch}")
    string(REPLACE "__high = htobe32(" "__high = htole32(" _wtls_branch "${_wtls_branch}")
    set(_wtls_fixed "${_wtls_head}${_wtls_branch}${_wtls_tail}")
    if(NOT _wtls_fixed STREQUAL _wtls_src)
        file(WRITE ${_wtls_endian} "${_wtls_fixed}")
    endif()

    # PSA uses Everest only for the X25519 agreement. The public key for the
    # TLS 1.3 key share (psa_export_public_key of the fresh key pair) is
    # computed by the generic ECP code instead: on the Wii that costs more
    # than keygen and agreement together (docs/benchmarks.md). Route it
    # through Everest as well. Pinned by the RFC 7748 vectors in
    # tests/common/crypto_tests.c (host and Wii). Idempotent.
    set(_wtls_ecp ${mbedtls_SOURCE_DIR}/tf-psa-crypto/drivers/builtin/src/psa_crypto_ecp.c)
    file(READ ${_wtls_ecp} _wtls_src)
    set(_wtls_anchor "psa_status_t mbedtls_psa_ecp_export_public_key(
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer, size_t key_buffer_size,
    uint8_t *data, size_t data_size, size_t *data_length)
{
    psa_status_t status = PSA_ERROR_CORRUPTION_DETECTED;
    mbedtls_ecp_keypair *ecp = NULL;
")
    set(_wtls_marker "/* libwiitls: X25519 public key via Everest */")
    string(FIND "${_wtls_src}" "${_wtls_marker}" _wtls_done)
    if(_wtls_done LESS 0)
        string(FIND "${_wtls_src}" "${_wtls_anchor}" _wtls_at)
        if(_wtls_at LESS 0)
            message(FATAL_ERROR "WTLS_EVEREST: unexpected layout of ${_wtls_ecp}")
        endif()
        string(REPLACE "${_wtls_anchor}" "${_wtls_anchor}
    ${_wtls_marker}
#if defined(MBEDTLS_ECDH_VARIANT_EVEREST_ENABLED)
    if (attributes->type == PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY) &&
        attributes->bits == 255) {
        static const uint8_t base[MBEDTLS_X25519_KEY_SIZE_BYTES] = { 9 };
        if (key_buffer_size != MBEDTLS_X25519_KEY_SIZE_BYTES) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        if (data_size < MBEDTLS_X25519_KEY_SIZE_BYTES) {
            return PSA_ERROR_BUFFER_TOO_SMALL;
        }
        mbedtls_x25519_scalarmult(data, key_buffer, base); /* clamps the scalar */
        *data_length = MBEDTLS_X25519_KEY_SIZE_BYTES;
        return PSA_SUCCESS;
    }
#endif
" _wtls_src "${_wtls_src}")
        file(WRITE ${_wtls_ecp} "${_wtls_src}")
    endif()

    # With WTLS_ACCEL, Everest's one X25519 entry point (used by the agreement
    # and, via the patch above, by the public key export) calls the 32-bit-limb
    # implementation in src/accel instead of HACL*'s 51-bit limbs, which need
    # emulated 128-bit products on PPC32. Selected by the WTLS_ACCEL_X25519
    # define, so builds with and without it can share the fetched source.
    # Pinned by tests/common/crypto_tests.c. Idempotent.
    set(_wtls_x25519 ${mbedtls_SOURCE_DIR}/tf-psa-crypto/drivers/everest/library/x25519.c)
    file(READ ${_wtls_x25519} _wtls_src)
    set(_wtls_marker "/* libwiitls: X25519 via src/accel */")
    string(FIND "${_wtls_src}" "${_wtls_marker}" _wtls_done)
    if(_wtls_done LESS 0)
        set(_wtls_fn "void mbedtls_x25519_scalarmult(")
        string(FIND "${_wtls_src}" "${_wtls_fn}" _wtls_at_fn)
        string(FIND "${_wtls_src}" "    Hacl_Curve25519_crypto_scalarmult(out, cp_scalar, cp_point);" _wtls_at_call)
        if(_wtls_at_fn LESS 0 OR _wtls_at_call LESS 0)
            message(FATAL_ERROR "WTLS_ACCEL: unexpected layout of ${_wtls_x25519}")
        endif()
        string(REPLACE "${_wtls_fn}" "${_wtls_marker}
void wtls_x25519(uint8_t *out, const uint8_t *scalar, const uint8_t *point);

${_wtls_fn}" _wtls_src "${_wtls_src}")
        string(REPLACE "    Hacl_Curve25519_crypto_scalarmult(out, cp_scalar, cp_point);" "#if defined(WTLS_ACCEL_X25519)
    wtls_x25519(out, cp_scalar, cp_point);
#else
    Hacl_Curve25519_crypto_scalarmult(out, cp_scalar, cp_point);
#endif" _wtls_src "${_wtls_src}")
        file(WRITE ${_wtls_x25519} "${_wtls_src}")
    endif()
endif()

if(WTLS_ACCEL_P256)
    # ECDSA P-256 verify with 8x32-bit limbs (src/accel/p256_verify.c): Shamir's
    # trick with interleaved wNAF against a generated fixed-base table, public
    # inputs only (variable time is fine). The hook sits at the top of
    # mbedtls_psa_ecdsa_verify_hash and covers exactly what TLS does: an
    # uncompressed 65-byte P-256 public key, a 64-byte raw r||s signature and
    # an ECDSA algorithm; everything else (key pairs, compressed points, other
    # curves, other families) falls through to the builtin code. It also skips
    # the mbedtls_ecp_keypair heap allocation and the mbedtls_mpi parsing.
    # Pinned by tests/common/crypto_tests.c. Idempotent.
    set(_wtls_ecpv ${mbedtls_SOURCE_DIR}/tf-psa-crypto/drivers/builtin/src/psa_crypto_ecp.c)
    file(READ ${_wtls_ecpv} _wtls_src)
    set(_wtls_marker "/* libwiitls: P-256 verify via src/accel */")
    string(FIND "${_wtls_src}" "${_wtls_marker}" _wtls_done)
    if(_wtls_done LESS 0)
        set(_wtls_anchor "psa_status_t mbedtls_psa_ecdsa_verify_hash(
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer, size_t key_buffer_size,
    psa_algorithm_t alg, const uint8_t *hash, size_t hash_length,
    const uint8_t *signature, size_t signature_length)
{
    psa_status_t status = PSA_ERROR_CORRUPTION_DETECTED;
    mbedtls_ecp_keypair *ecp = NULL;
    size_t curve_bytes;
    mbedtls_mpi r, s;
")
        string(FIND "${_wtls_src}" "${_wtls_anchor}" _wtls_at)
        if(_wtls_at LESS 0)
            message(FATAL_ERROR "WTLS_ACCEL_P256: unexpected layout of ${_wtls_ecpv}")
        endif()
        string(REPLACE "${_wtls_anchor}" "${_wtls_anchor}    ${_wtls_marker}
#if defined(WTLS_ACCEL_P256)
    extern psa_status_t wtls_p256_verify(const uint8_t *pub, const uint8_t *hash,
                                         size_t hash_len, const uint8_t *sig);
    if (attributes->bits == 256 &&
        attributes->type == PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1) &&
        PSA_ALG_IS_ECDSA(alg) &&
        key_buffer_size == 65 && key_buffer[0] == 0x04 &&
        signature_length == 64 && hash_length > 0) {
        return wtls_p256_verify(key_buffer, hash, hash_length, signature);
    }
#endif
" _wtls_src "${_wtls_src}")
        file(WRITE ${_wtls_ecpv} "${_wtls_src}")
    endif()
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    foreach(t mbedtls mbedx509 tfpsacrypto)
        target_compile_definitions(${t} PUBLIC WTLS_DEBUG)
    endforeach()
endif()

# ---------------------------------------------------------------------------
# Embedded Mozilla CA bundle: regenerated from data/cacert.pem when Python 3 is
# available (SHA-256 checked), otherwise the checked-in src/gen/ca_bundle.c is used.
find_package(Python3 COMPONENTS Interpreter)
set(WTLS_CA_PEM ${CMAKE_CURRENT_SOURCE_DIR}/data/cacert.pem)
set(WTLS_CA_GEN ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_ca_bundle.py)
if(Python3_Interpreter_FOUND)
    set(WTLS_CA_BUNDLE_C ${CMAKE_BINARY_DIR}/gen/ca_bundle.c)
    add_custom_command(
        OUTPUT ${WTLS_CA_BUNDLE_C}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/gen
        COMMAND ${Python3_EXECUTABLE} ${WTLS_CA_GEN} --pem ${WTLS_CA_PEM}
                --sha256 ${WTLS_CA_PEM}.sha256 --out ${WTLS_CA_BUNDLE_C}
        DEPENDS ${WTLS_CA_PEM} ${WTLS_CA_PEM}.sha256 ${WTLS_CA_GEN}
        COMMENT "Generating CA bundle from data/cacert.pem"
        VERBATIM)
else()
    set(WTLS_CA_BUNDLE_C ${CMAKE_CURRENT_SOURCE_DIR}/src/gen/ca_bundle.c)
endif()

# ---------------------------------------------------------------------------
# WTLS_VERSION* in the public header must match the project version.
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/include/wiitls/wiitls.h _wtls_ver_line
     REGEX "^#define WTLS_VERSION \"[^\"]*\"")
string(REGEX REPLACE ".*\"([^\"]*)\".*" "\\1" _wtls_ver "${_wtls_ver_line}")
if(NOT _wtls_ver STREQUAL PROJECT_VERSION)
    message(FATAL_ERROR "WTLS_VERSION in wiitls.h is '${_wtls_ver}', project version is ${PROJECT_VERSION}")
endif()
foreach(_part MAJOR MINOR PATCH)
    file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/include/wiitls/wiitls.h _wtls_p_line
         REGEX "^#define WTLS_VERSION_${_part}[ \t]+[0-9]+")
    string(REGEX REPLACE ".*[ \t]([0-9]+).*" "\\1" _wtls_p "${_wtls_p_line}")
    if(NOT _wtls_p STREQUAL PROJECT_VERSION_${_part})
        message(FATAL_ERROR "WTLS_VERSION_${_part} in wiitls.h is '${_wtls_p}', expected ${PROJECT_VERSION_${_part}}")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# libwiitls
add_library(wiitls STATIC
    src/wtls_core.c
    src/wtls_entropy.c
    src/wtls_jitter.c
    src/wtls_time.c
    src/wtls_castore.c
    src/wtls_cred.c
    src/wtls_conn.c
    src/wtls_scache.c
    src/wtls_dns.c
    src/http/wtls_url.c
    src/http/wtls_http_req.c
    src/http/wtls_http_parse.c
    src/http/wtls_http_chunked.c
    src/http/wtls_http_util.c
    src/http/wtls_http.c
    src/http/wtls_http_pool.c
    ${WTLS_CA_BUNDLE_C}
    src/plat/plat_common.c
    src/plat/plat_${WTLS_PLATFORM}.c
)
if(WTLS_ACCEL)
    # Called from Everest's mbedtls_x25519_scalarmult (patched above); the
    # tfpsacrypto -> wiitls link edge below resolves it.
    target_sources(wiitls PRIVATE src/accel/x25519_fe32.c)
    if(WTLS_PLATFORM STREQUAL "ogc")
        # The compiler may turn the masked swaps back into branches on the key
        # bits: fail the build if the PPC code of the ladder/select branches.
        add_custom_command(TARGET wiitls POST_BUILD
            COMMAND ${CMAKE_COMMAND} -DOBJDUMP=${CMAKE_OBJDUMP}
                    "-DOBJECTS=$<FILTER:$<TARGET_OBJECTS:wiitls>,INCLUDE,x25519_fe32>"
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_ct.cmake
            VERBATIM)
    endif()
endif()
if(WTLS_ACCEL_P256)
    # Called from the patched builtin driver (psa_crypto_ecp.c) above; the
    # tfpsacrypto -> wiitls link edge resolves it.
    target_sources(wiitls PRIVATE src/accel/p256_verify.c)
endif()
if(WTLS_GZIP)
    target_sources(wiitls PRIVATE src/http/wtls_http_gzip.c $<TARGET_OBJECTS:wtls_zlib>)
    target_include_directories(wiitls PRIVATE ${zlib_SOURCE_DIR})
    target_compile_definitions(wiitls PRIVATE Z_PREFIX WTLS_GZIP=1)
endif()
set_target_properties(wiitls PROPERTIES OUTPUT_NAME wiitls PREFIX lib)
include(GNUInstallDirs)
target_include_directories(wiitls
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(wiitls
    PUBLIC
        $<BUILD_INTERFACE:mbedtls>
        $<BUILD_INTERFACE:mbedx509>
        $<BUILD_INTERFACE:tfpsacrypto>)
add_library(wiitls::wiitls ALIAS wiitls)
# tfpsacrypto calls back into libwiitls (mbedtls_platform_get_entropy): close the
# static-link cycle so linkers that do not rescan archives still resolve it.
target_link_libraries(tfpsacrypto INTERFACE wiitls)
# tests/common/crypto_tests.c compares the accelerated P-256 verify against the
# builtin driver's mbedtls_ecdsa_verify; those headers are driver-private in
# Mbed TLS 4.1.1 (tf-psa-crypto) and not exported by the tfpsacrypto target.
target_include_directories(tfpsacrypto INTERFACE
    ${mbedtls_SOURCE_DIR}/tf-psa-crypto/drivers/builtin/include)
target_compile_options(wiitls PRIVATE -Wall -Wextra)
# Lower bound of the clock sanity check (honours SOURCE_DATE_EPOCH for reproducible builds).
string(TIMESTAMP WTLS_BUILD_UNIX_TIME "%s" UTC)
target_compile_definitions(wiitls PRIVATE WTLS_BUILD_UNIX_TIME=${WTLS_BUILD_UNIX_TIME}LL)
if(WTLS_PLATFORM STREQUAL "host")
    target_compile_definitions(wiitls PRIVATE WTLS_PLATFORM_HOST)
    if(WIN32)
        target_link_libraries(wiitls PUBLIC ws2_32 bcrypt)
    elseif(WTLS_THREAD_CHECK OR CMAKE_BUILD_TYPE STREQUAL "Debug")
        find_package(Threads REQUIRED)
        target_link_libraries(wiitls PUBLIC Threads::Threads)
    endif()
else()
    target_compile_definitions(wiitls PRIVATE WTLS_PLATFORM_OGC)
endif()

# ---------------------------------------------------------------------------
# Benchmark crypto call profile (bench/psa_prof.c): one --wrap per function in
# WTLS_PP_LIST (bench/psa_prof.h). Used by test_bench and wtls_bench.
set(WTLS_BENCH_WRAP
    psa_generate_key psa_export_public_key psa_raw_key_agreement psa_generate_random
    psa_import_key psa_destroy_key psa_verify_hash psa_hash_compute psa_hash_setup
    psa_hash_update psa_hash_finish psa_hash_clone psa_key_derivation_setup
    psa_key_derivation_input_bytes psa_key_derivation_input_key
    psa_key_derivation_output_bytes psa_key_derivation_abort psa_mac_compute
    psa_mac_sign_setup psa_mac_update psa_mac_sign_finish psa_aead_encrypt psa_aead_decrypt
    mbedtls_x509_crt_parse_der mbedtls_x509_crt_verify_with_ca_cb mbedtls_pk_verify_ext
    mbedtls_pk_parse_subpubkey)
function(wtls_bench_wrap target)
    foreach(fn ${WTLS_BENCH_WRAP})
        target_link_options(${target} PRIVATE "LINKER:--wrap=${fn}")
    endforeach()
    # bench/p256m_probe.c compiles p256-m itself, for a speed comparison.
    target_sources(${target} PRIVATE ${PROJECT_SOURCE_DIR}/bench/p256m_probe.c)
    target_include_directories(${target} PRIVATE
        ${mbedtls_SOURCE_DIR}/tf-psa-crypto/drivers/p256-m/p256-m)
endfunction()

# ---------------------------------------------------------------------------
# Tests
if(WTLS_BUILD_TESTS AND WTLS_PLATFORM STREQUAL "host")
    enable_testing()
    function(wtls_host_test name)
        add_executable(${name} tests/host/${name}.c ${ARGN})
        target_include_directories(${name} PRIVATE tests/common src)
        target_link_libraries(${name} PRIVATE wiitls)
        add_test(NAME ${name} COMMAND ${name})
        set_tests_properties(${name} PROPERTIES PASS_REGULAR_EXPRESSION "WTLS-TEST DONE pass=[0-9]+ fail=0")
    endfunction()
    if(NOT Python3_Interpreter_FOUND)
        message(FATAL_ERROR "Host tests need Python 3 (test servers, certificate generation)")
    endif()
    # Test PKI (tests/host/make_certs.py, OpenSSL CLI) in ${CMAKE_BINARY_DIR}/certs.
    find_program(WTLS_OPENSSL openssl REQUIRED)
    set(WTLS_TEST_CERT_DIR ${CMAKE_BINARY_DIR}/certs)
    add_custom_command(
        OUTPUT ${WTLS_TEST_CERT_DIR}/test-ca.pem
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/host/make_certs.py
                --out ${WTLS_TEST_CERT_DIR} --openssl ${WTLS_OPENSSL}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/host/make_certs.py
        COMMENT "Generating test certificates"
        VERBATIM)
    add_custom_target(wtls_test_certs DEPENDS ${WTLS_TEST_CERT_DIR}/test-ca.pem)
    # The checked-in fallback bundle must match data/cacert.pem.
    add_test(NAME ca_bundle_fallback_current
             COMMAND ${CMAKE_COMMAND} -E compare_files
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/gen/ca_bundle.c ${WTLS_CA_BUNDLE_C})
    # Test that needs tests/host/tls_test_server.py; the fixed ports force serial runs.
    function(wtls_host_server_test name)
        add_executable(${name} tests/host/${name}.c ${ARGN})
        target_include_directories(${name} PRIVATE tests/common src)
        target_link_libraries(${name} PRIVATE wiitls)
        add_test(NAME ${name}
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/host/run_with_server.py
                         $<TARGET_FILE:${name}>)
        set_tests_properties(${name} PROPERTIES
            PASS_REGULAR_EXPRESSION "WTLS-TEST DONE pass=[0-9]+ fail=0"
            RESOURCE_LOCK tls_test_server)
    endfunction()

    wtls_host_test(test_smoke)
    wtls_host_test(test_jitter)
    wtls_host_test(test_time)
    wtls_host_server_test(test_plat tests/common/plat_tests.c)
    wtls_host_test(test_castore)
    wtls_host_test(test_scache)
    wtls_host_test(test_dns)
    wtls_host_test(test_crypto tests/common/crypto_tests.c)
    wtls_host_test(test_http_url)
    wtls_host_test(test_http_req)
    wtls_host_test(test_http_parse)
    wtls_host_test(test_http_chunked)
    wtls_host_test(test_http_util)
    if(WTLS_GZIP)
        wtls_host_test(test_http_gzip)
    endif()
    wtls_host_test(test_http_fuzz)
    wtls_host_test(test_http_pool)
    if(WTLS_THREAD_CHECK OR CMAKE_BUILD_TYPE STREQUAL "Debug")
        wtls_host_test(test_thread)
    endif()

    # Deterministic corpus regression tests for all libFuzzer harnesses (runs on every host build).
    function(wtls_fuzz_corpus_test name corpus_dir)
        add_executable(test_${name} tests/fuzz/${name}.c tests/fuzz/fuzz_standalone_main.c)
        target_include_directories(test_${name} PRIVATE src)
        target_link_libraries(test_${name} PRIVATE wiitls)
        target_compile_definitions(test_${name} PRIVATE
            WTLS_FUZZ_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/${corpus_dir}")
        add_test(NAME test_${name} COMMAND test_${name})
        set_tests_properties(test_${name} PROPERTIES PASS_REGULAR_EXPRESSION "WTLS-FUZZ DONE files=[0-9]+ fail=0")
    endfunction()
    wtls_fuzz_corpus_test(fuzz_http_parse http_parse)
    wtls_fuzz_corpus_test(fuzz_http_chunked http_chunked)
    wtls_fuzz_corpus_test(fuzz_url url)
    wtls_fuzz_corpus_test(fuzz_content_range content_range)
    if(WTLS_GZIP)
        wtls_fuzz_corpus_test(fuzz_http_gzip http_gzip)
    endif()
    wtls_fuzz_corpus_test(fuzz_client_cred client_cred)
    add_dependencies(test_castore wtls_test_certs)
    target_compile_definitions(test_castore PRIVATE
        WTLS_TEST_CERT_DIR="${WTLS_TEST_CERT_DIR}"
        WTLS_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/host/fixtures")
    wtls_host_server_test(test_tls tests/common/tls_tests.c)
    add_dependencies(test_tls wtls_test_certs)
    set_tests_properties(test_tls PROPERTIES ENVIRONMENT "WTLS_TEST_CERT_DIR=${WTLS_TEST_CERT_DIR}")
    wtls_host_server_test(test_http tests/common/http_tests.c)
    add_dependencies(test_http wtls_test_certs)
    if(WTLS_GZIP)
        target_compile_definitions(test_http PRIVATE WTLS_GZIP=1)
    endif()
    set_tests_properties(test_http PROPERTIES ENVIRONMENT "WTLS_TEST_CERT_DIR=${WTLS_TEST_CERT_DIR}")
    wtls_host_server_test(test_soak tests/common/soak_tests.c)
    add_dependencies(test_soak wtls_test_certs)
    if(WTLS_GZIP)
        target_compile_definitions(test_soak PRIVATE WTLS_GZIP=1)
    endif()
    set_tests_properties(test_soak PROPERTIES ENVIRONMENT "WTLS_TEST_CERT_DIR=${WTLS_TEST_CERT_DIR}")
    # A TLS-only application must not link the HTTP layer.
    if(CMAKE_NM)
        add_test(NAME http_not_linked_into_tls_only
                 COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} -DFILE=$<TARGET_FILE:test_tls> -DPATTERN=wtls_http_
                         -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_no_symbol.cmake)
        # An application without client certificates must not link the
        # credential parsing (mbedtls_pk_parse_key and friends).
        add_test(NAME cred_parse_not_linked_without_mtls
                 COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} -DFILE=$<TARGET_FILE:test_plat> -DPATTERN=wtls_client_cred_
                         -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_no_symbol.cmake)
        # An application that does not call the HTTP helpers (url_encode /
        # parse_content_range) must not link wtls_http_util.o.
        add_test(NAME http_util_not_linked_without_helpers
                 COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} -DFILE=$<TARGET_FILE:test_http> -DPATTERN="wtls_http_url_encode|wtls_http_parse_content_range"
                         -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_no_symbol.cmake)
    endif()

    # Benchmarks on the host: proves every benchmark runs; the numbers are the PC's.
    add_executable(test_bench bench/host_main.c bench/bench_core.c bench/psa_prof.c)
    target_include_directories(test_bench PRIVATE bench tests/common src)
    target_link_libraries(test_bench PRIVATE wiitls)
    wtls_bench_wrap(test_bench)
    add_dependencies(test_bench wtls_test_certs)
    add_test(NAME test_bench
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/host/run_with_server.py
                     $<TARGET_FILE:test_bench>)
    set_tests_properties(test_bench PROPERTIES
        PASS_REGULAR_EXPRESSION "WTLS-TEST DONE pass=[0-9]+ fail=0"
        RESOURCE_LOCK tls_test_server
        ENVIRONMENT "WTLS_TEST_CERT_DIR=${WTLS_TEST_CERT_DIR}")
endif()

if(WTLS_BUILD_FUZZ)
    if(NOT WTLS_PLATFORM STREQUAL "host")
        message(FATAL_ERROR "WTLS_BUILD_FUZZ requires WTLS_PLATFORM=host")
    endif()
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "WTLS_BUILD_FUZZ=ON requires Clang (-DCMAKE_C_COMPILER=clang)")
    endif()
    foreach(t wiitls wtls_zlib mbedtls mbedx509 tfpsacrypto builtin everest platform utilities extras pqcp)
        if(TARGET ${t})
            target_compile_options(${t} PRIVATE -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        endif()
    endforeach()
    set(_wtls_fuzz_targets fuzz_http_parse fuzz_http_chunked fuzz_url fuzz_content_range fuzz_client_cred)
    if(WTLS_GZIP)
        list(APPEND _wtls_fuzz_targets fuzz_http_gzip)
    endif()
    foreach(ft ${_wtls_fuzz_targets})
        add_executable(${ft} tests/fuzz/${ft}.c)
        target_include_directories(${ft} PRIVATE src)
        target_link_libraries(${ft} PRIVATE wiitls)
        target_compile_options(${ft} PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        target_link_options(${ft} PRIVATE -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all)
    endforeach()
endif()

if(WTLS_PLATFORM STREQUAL "ogc")
    option(WTLS_BUILD_HWTEST "Build the Wii hardware test runner" ON)
    if(WTLS_BUILD_HWTEST)
        add_subdirectory(tests/hw)
    endif()
    option(WTLS_BUILD_BENCH "Build the Wii benchmark runner" ON)
    if(WTLS_BUILD_BENCH)
        add_subdirectory(bench)
    endif()
    option(WTLS_BUILD_EXAMPLES "Build the Wii example programs" ON)
    if(WTLS_BUILD_EXAMPLES)
        add_subdirectory(examples/https_get)
        add_subdirectory(examples/upload_mtls_abort)
    endif()
endif()

# ---------------------------------------------------------------------------
# Install & find_package(wiitls)
option(WTLS_INSTALL "Install libwiitls, headers, bundled Mbed TLS archives, and CMake package" ${PROJECT_IS_TOP_LEVEL})
if(WTLS_INSTALL)
    include(CMakePackageConfigHelpers)

    set(_wtls_need_threads FALSE)
    if(WTLS_PLATFORM STREQUAL "host" AND NOT WIN32 AND (WTLS_THREAD_CHECK OR CMAKE_BUILD_TYPE STREQUAL "Debug"))
        set(_wtls_need_threads TRUE)
    endif()

    install(TARGETS wiitls
        EXPORT wiitlsTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES
        $<TARGET_FILE:mbedtls>
        $<TARGET_FILE:mbedx509>
        $<TARGET_FILE:tfpsacrypto>
        DESTINATION ${CMAKE_INSTALL_LIBDIR})

    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/wiitls
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/config/wtls_mbedtls_config.h
        ${CMAKE_CURRENT_SOURCE_DIR}/config/wtls_crypto_config.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(DIRECTORY
        ${mbedtls_SOURCE_DIR}/include/
        ${mbedtls_SOURCE_DIR}/tf-psa-crypto/include/
        ${mbedtls_SOURCE_DIR}/tf-psa-crypto/drivers/builtin/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h")

    install(EXPORT wiitlsTargets
        FILE wiitlsTargets.cmake
        NAMESPACE wiitls::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wiitls)

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/wiitlsConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/wiitlsConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wiitls
        PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR)
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/wiitlsConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/wiitlsConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/wiitlsConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wiitls)
endif()
