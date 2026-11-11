cmake_minimum_required(VERSION 3.20)
project(libwiitls VERSION 0.1.0 LANGUAGES C)

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
option(WTLS_EVEREST "X25519 via Everest/HACL* (MBEDTLS_ECDH_VARIANT_EVEREST_ENABLED)" OFF)
set(WTLS_OPT O2 CACHE STRING "Optimisation level for library and Mbed TLS: O2 or O3")
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
add_compile_options(-${WTLS_OPT}) # after CMAKE_C_FLAGS_<CONFIG>: the last -O wins

include(FetchContent)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(GEN_FILES OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
set(MBEDTLS_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/wtls_mbedtls_config.h" CACHE FILEPATH "" FORCE)
set(TF_PSA_CRYPTO_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/wtls_crypto_config.h" CACHE FILEPATH "" FORCE)
FetchContent_Declare(mbedtls
    URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-4.1.1/mbedtls-4.1.1.tar.bz2
    URL_HASH SHA256=3359a349e23db3d5536fcee032ae7b2ecbfc08972fab643089b5cbf2a375c98c
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(mbedtls)
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
# libwiitls
add_library(wiitls STATIC
    src/wtls_core.c
    src/wtls_entropy.c
    src/wtls_jitter.c
    src/wtls_time.c
    src/wtls_castore.c
    src/wtls_conn.c
    src/wtls_scache.c
    src/wtls_dns.c
    ${WTLS_CA_BUNDLE_C}
    src/plat/plat_common.c
    src/plat/plat_${WTLS_PLATFORM}.c
)
set_target_properties(wiitls PROPERTIES OUTPUT_NAME wiitls PREFIX lib)
target_include_directories(wiitls
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(wiitls PUBLIC mbedtls mbedx509 tfpsacrypto)
# tfpsacrypto calls back into libwiitls (mbedtls_platform_get_entropy): close the
# static-link cycle so linkers that do not rescan archives still resolve it.
target_link_libraries(tfpsacrypto INTERFACE wiitls)
target_compile_options(wiitls PRIVATE -Wall -Wextra)
# Lower bound of the clock sanity check (honours SOURCE_DATE_EPOCH for reproducible builds).
string(TIMESTAMP WTLS_BUILD_UNIX_TIME "%s" UTC)
target_compile_definitions(wiitls PRIVATE WTLS_BUILD_UNIX_TIME=${WTLS_BUILD_UNIX_TIME}LL)
if(WTLS_PLATFORM STREQUAL "host")
    target_compile_definitions(wiitls PRIVATE WTLS_PLATFORM_HOST)
    if(WIN32)
        target_link_libraries(wiitls PUBLIC ws2_32 bcrypt)
    endif()
else()
    target_compile_definitions(wiitls PRIVATE WTLS_PLATFORM_OGC)
endif()

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
    add_dependencies(test_castore wtls_test_certs)
    target_compile_definitions(test_castore PRIVATE
        WTLS_TEST_CERT_DIR="${WTLS_TEST_CERT_DIR}"
        WTLS_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/host/fixtures")
    wtls_host_server_test(test_tls tests/common/tls_tests.c)
    add_dependencies(test_tls wtls_test_certs)
    set_tests_properties(test_tls PROPERTIES ENVIRONMENT "WTLS_TEST_CERT_DIR=${WTLS_TEST_CERT_DIR}")

    # Benchmarks on the host: proves every benchmark runs; the numbers are the PC's.
    add_executable(test_bench bench/host_main.c bench/bench_core.c)
    target_include_directories(test_bench PRIVATE bench tests/common src)
    target_link_libraries(test_bench PRIVATE wiitls)
    add_dependencies(test_bench wtls_test_certs)
    add_test(NAME test_bench
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/host/run_with_server.py
                     $<TARGET_FILE:test_bench>)
    set_tests_properties(test_bench PROPERTIES
        PASS_REGULAR_EXPRESSION "WTLS-TEST DONE pass=[0-9]+ fail=0"
        RESOURCE_LOCK tls_test_server
        ENVIRONMENT "WTLS_TEST_CERT_DIR=${WTLS_TEST_CERT_DIR}")
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
    endif()
endif()
