cmake_minimum_required(VERSION 3.20)

project(newlauncher VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/NlFlags.cmake)

# Plattformfreier Kern (Spec 4.1): ohne libogc, auf dem Host getestet, auf der Wii mitgelinkt.
add_library(nl_core STATIC
    src/core/assert.cpp
    src/core/theme.cpp
    src/core/console.cpp
    src/core/frame_stats.cpp
    src/core/crash_report.cpp
    src/core/ini.cpp
    src/core/config.cpp
    src/core/utf8.cpp
    src/core/text_fold.cpp
    src/core/bin_io.cpp
    src/core/index_file.cpp
    src/core/meta_db.cpp
    src/core/disc_header.cpp
    src/core/scanner.cpp
    src/core/library_store.cpp
    src/core/dol.cpp
    src/core/emulators.cpp
    src/core/launch_args.cpp
    src/core/launch_plan.cpp
    src/core/n64.cpp
    src/core/n64_choice.cpp
    src/core/vc_map.cpp
    src/core/wii_title.cpp
    src/core/cmpr.cpp
    src/core/tex_file.cpp
    src/core/cover_image.cpp
    src/core/cover_paths.cpp
    src/core/cover_cache.cpp
    src/core/http.cpp
    src/core/cover_download.cpp
    src/gfx/draw_list.cpp
    src/gfx/shapes.cpp
    src/gfx/atlas.cpp
    src/gfx/font.cpp
    src/gfx/text.cpp
    src/ui/input.cpp
    src/ui/scene.cpp
    src/ui/layout.cpp
    src/ui/test_library.cpp
    src/ui/main_scene.cpp
    src/ui/home_menu.cpp
    src/ui/debug_overlay.cpp
    src/ui/pointer_cursor.cpp
    src/ui/startup_scene.cpp
    src/ui/jump_bar.cpp
    src/ui/keyboard.cpp
    src/ui/details_scene.cpp
    src/ui/vc_map_scene.cpp
    src/ui/dialog_scene.cpp
    src/ui/launch_text.cpp
)
target_include_directories(nl_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
nl_apply_flags(nl_core)

if(CMAKE_SYSTEM_NAME STREQUAL "NintendoWii")
    # FreeType aus den devkitPro-portlibs (pacman -S ppc-freetype), statisch mit Abhängigkeiten.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FREETYPE REQUIRED freetype2)
    # libpng für die Cover (pacman -S ppc-libpng); FreeType bringt sie meist schon mit.
    pkg_check_modules(PNG REQUIRED libpng)

    # Schriften (Spec 3.7), per tools/subset_fonts.py auf Latin-1 + „…" reduziert.
    dkp_add_embedded_binary_library(nl_fonts
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/MPLUSRounded1c-Regular-latin1.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/MPLUSRounded1c-Medium-latin1.ttf
    )

    # App-Booter (Spec 9, M3): frei stehend, eigenes Linkerskript. Bewusst kein add_executable, weil die
    # Toolchain dort -mrvl samt rvl.ld und libogc-crt0 einschleust.
    set(NL_BOOTER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/launch/app_booter)
    set(NL_BOOTER_ELF ${CMAKE_CURRENT_BINARY_DIR}/app_booter.elf)
    set(NL_BOOTER_BIN ${CMAKE_CURRENT_BINARY_DIR}/app_booter.bin)
    add_custom_command(
        OUTPUT ${NL_BOOTER_BIN}
        COMMAND ${CMAKE_C_COMPILER} -mcpu=750 -meabi -mhard-float -mregnames -G0 -Os -std=c11 -ffreestanding
                -fno-builtin -fno-tree-loop-distribute-patterns -nostdlib -nostartfiles -Wall -Wextra -Werror
                -Wl,-T,${NL_BOOTER_DIR}/booter.ld -Wl,--gc-sections -Wl,--no-warn-rwx-segments
                -o ${NL_BOOTER_ELF} ${NL_BOOTER_DIR}/crt0.S ${NL_BOOTER_DIR}/booter.c
        COMMAND ${DKP_OBJCOPY} -O binary ${NL_BOOTER_ELF} ${NL_BOOTER_BIN}
        DEPENDS ${NL_BOOTER_DIR}/booter.c ${NL_BOOTER_DIR}/crt0.S ${NL_BOOTER_DIR}/booter.ld
        COMMENT "App-Booter bauen"
        VERBATIM
    )
    add_custom_target(nl_app_booter DEPENDS ${NL_BOOTER_BIN})
    dkp_set_target_file(nl_app_booter ${NL_BOOTER_BIN})
    dkp_add_embedded_binary_library(nl_booter nl_app_booter)

    # Test-DOL für den Chainloader: gibt argc/argv über SYS_Report aus.
    add_executable(argv_echo tests/wii/argv_echo.c)
    target_compile_options(argv_echo PRIVATE -Wall -Wextra)
    ogc_create_dol(argv_echo)

    set(NL_WII_SOURCES
        src/app/main.cpp
        src/platform/log.cpp
        src/platform/video.cpp
        src/launch/chainloader.cpp
        src/launch/channel.cpp
        src/platform/nand_titles.cpp
        src/platform/wii_input.cpp
        src/platform/crash.cpp
        src/platform/storage.cpp
        src/platform/std_fs.cpp
        src/platform/scan_worker.cpp
        src/platform/png_decode.cpp
        src/platform/cover_worker.cpp
        src/app/cover_service.cpp
        src/gfx/renderer.cpp
        src/gfx/font_loader.cpp
    )

    function(nl_wii_target name)
        add_executable(${name} ${ARGN})
        target_include_directories(${name} PRIVATE ${FREETYPE_STATIC_INCLUDE_DIRS} ${PNG_STATIC_INCLUDE_DIRS})
        target_link_directories(${name} PRIVATE ${FREETYPE_STATIC_LIBRARY_DIRS})
        target_link_libraries(${name} PRIVATE nl_core nl_fonts nl_booter ${FREETYPE_STATIC_LIBRARIES} ${PNG_STATIC_LIBRARIES} fat wiiuse bte)
        target_compile_definitions(${name} PRIVATE NL_VERSION="${PROJECT_VERSION}")
        nl_apply_flags(${name})
        ogc_create_dol(${name})
    endfunction()

    nl_wii_target(newlauncher ${NL_WII_SOURCES})

    # Selbsttest (Task 11): geskriptete Eingaben, Allokationszähler, absichtlicher Absturz am Ende.
    nl_wii_target(nl_selftest ${NL_WII_SOURCES} src/app/selftest.cpp)
    target_compile_definitions(nl_selftest PRIVATE NL_SELFTEST)
    target_link_options(nl_selftest PRIVATE -Wl,--wrap=_malloc_r -Wl,--wrap=_memalign_r -Wl,--wrap=_realloc_r)
else()
    enable_testing()

    file(GLOB_RECURSE NL_HOST_TEST_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/host/*_test.cpp)
    add_executable(nl_host_tests tests/host/main.cpp ${NL_HOST_TEST_SOURCES})
    target_include_directories(nl_host_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/host)
    target_compile_definitions(nl_host_tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
    target_link_libraries(nl_host_tests PRIVATE nl_core)
    nl_apply_flags(nl_host_tests)

    add_test(NAME nl_host_tests COMMAND nl_host_tests)
endif()
