cmake_minimum_required(VERSION 3.20)

# devkitPro unter Windows: CMake läuft in MSYS2, der GCC ist nativ und schreibt Abhängigkeiten als C:/….
# In Unterordnern (third_party/wii_tls, Mbed TLS) macht CMake daraus „_deps/…/C:/…“ und make bricht beim
# zweiten Build ab. CMakes eigener Abhängigkeits-Scanner hat das Problem nicht (nur dort nötig).
if(DEFINED ENV{MSYSTEM})  # vor project(): CMAKE_HOST_SYSTEM_NAME gibt es noch nicht
    set(CMAKE_DEPENDS_USE_COMPILER FALSE)
endif()

project(newlauncher VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/NlFlags.cmake)
include(cmake/NlAboutEntries.cmake)

# Plattformfreier Kern (Spec 4.1): ohne libogc, auf dem Host getestet, auf der Wii mitgelinkt.
add_library(nl_core STATIC
    src/core/assert.cpp
    src/core/theme.cpp
    src/core/console.cpp
    src/core/frame_stats.cpp
    src/core/crash_report.cpp
    src/core/ini.cpp
    src/core/config.cpp
    src/core/net_error.cpp
    src/core/job_queue.cpp
    src/core/settings_model.cpp
    src/core/ui_sound.cpp
    src/core/synth.cpp
    src/core/playlist.cpp
    src/core/perf_stats.cpp
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
    src/core/crc32.cpp
    src/core/inflate.cpp
    src/core/zip_reader.cpp
    src/core/emu_catalog.cpp
    src/core/emu_install.cpp
    src/core/nin_cfg.cpp
    src/core/fat_chain.cpp
    src/core/wbfs_map.cpp
    src/core/partition_table.cpp
    src/core/device_names.cpp
    src/core/data_dir.cpp
    src/core/library_sources.cpp
    src/core/frag_list.cpp
    src/core/ios_info.cpp
    src/core/cover_download.cpp
    src/core/lz77.cpp
    src/gfx/draw_list.cpp
    src/gfx/shapes.cpp
    src/gfx/atlas.cpp
    src/gfx/font.cpp
    src/gfx/text.cpp
    src/ui/input.cpp
    src/ui/scene.cpp
    src/ui/layout.cpp
    src/ui/strings.cpp
    src/ui/test_library.cpp
    src/ui/main_scene.cpp
    src/ui/home_menu.cpp
    src/ui/settings_scene.cpp
    src/ui/emulators_scene.cpp
    src/ui/about_scene.cpp
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
# Komponentenliste der Seite „Über“ aus THIRD_PARTY.md (Spec v2 3.4), nicht doppelt gepflegt.
nl_generate_about_entries(${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY.md ${CMAKE_CURRENT_BINARY_DIR}/generated/about_entries.inc)
target_include_directories(nl_core PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
nl_apply_flags(nl_core)

if(CMAKE_SYSTEM_NAME STREQUAL "NintendoWii")
    # FreeType aus den devkitPro-portlibs (pacman -S ppc-freetype), statisch mit Abhängigkeiten.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FREETYPE REQUIRED freetype2)
    # libpng für die Cover (pacman -S ppc-libpng); FreeType bringt sie meist schon mit.
    pkg_check_modules(PNG REQUIRED libpng)

    # HTTPS über wii_tls (M11, Spec v2 4.1): vendort nach third_party/wii_tls (tools/vendor_wii_tls.py), holt
    # Mbed TLS 4.1.1 per FetchContent mit festem SHA256. Ohne NL_TLS baut der Launcher wie bisher nur HTTP.
    option(NL_TLS "HTTPS über wii_tls" ON)
    if(NL_TLS)
        set(WTLS_PLATFORM ogc CACHE STRING "" FORCE)
        set(WTLS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(WTLS_BUILD_HWTEST OFF CACHE BOOL "" FORCE)
        set(WTLS_BUILD_BENCH OFF CACHE BOOL "" FORCE)
        set(WTLS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        # Mbed TLS 4.1.1 ruft cmake_minimum_required mit einer Version < 3.10 auf, neuere CMake-Versionen
        # warnen dafür. Die Mindest-Policy-Version hebt nur den Upstream-Code an; danach wieder aus.
        set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
        add_subdirectory(third_party/wii_tls EXCLUDE_FROM_ALL)
        unset(CMAKE_POLICY_VERSION_MINIMUM)
    endif()

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

    # Game-Booter für Wii-Spiele (M9, Spec 5.1): Fremdcode aus WiiFlow Lite, siehe boot/UPSTREAM.md.
    # Läuft ab 0x80A80000 hinter dem Launcher-Bereich; die Warnoptionen entsprechen dem upstream-Makefile.
    file(GLOB NL_GAME_BOOTER_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/boot/source/*.c
        ${CMAKE_CURRENT_SOURCE_DIR}/boot/source/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/boot/source/*.S)
    enable_language(ASM)
    # Eigener Code im Booter (M11): LZ77 aus core/ über eine C-Brücke, ohne Exceptions/RTTI wie nl_core.
    set(NL_BOOTER_NL_SOURCES boot/nl/exception_close.cpp boot/nl/lz77_bridge.cpp boot/nl/progress.c
        src/core/lz77.cpp src/core/assert.cpp)
    add_executable(nl_game_booter ${NL_GAME_BOOTER_SOURCES} ${NL_BOOTER_NL_SOURCES})
    set_source_files_properties(boot/nl/lz77_bridge.cpp src/core/lz77.cpp src/core/assert.cpp
        TARGET_DIRECTORY nl_game_booter PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-fno-rtti")
    set_target_properties(nl_game_booter PROPERTIES OUTPUT_NAME game_booter)
    target_include_directories(nl_game_booter PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/boot/source ${CMAKE_CURRENT_SOURCE_DIR}/boot/include
        ${CMAKE_CURRENT_SOURCE_DIR}/boot ${CMAKE_CURRENT_SOURCE_DIR}/src)
    # NL_BOOTER_CHEATS=0: der Ocarina-Codehandler (Blob ohne Quelle) bleibt aus dem Build (M11, Spec v2 3.2).
    target_compile_definitions(nl_game_booter PRIVATE HAVE_CONFIG_H NL_VERSION="${PROJECT_VERSION}" NL_BOOTER_CHEATS=0)
    target_compile_options(nl_game_booter PRIVATE $<$<COMPILE_LANGUAGE:C>:-std=gnu17> -O1 -Wall -Wextra -Wno-shift-negative-value
        -Wno-array-bounds -Wno-stringop-overflow -Wno-stringop-overread)
    target_link_options(nl_game_booter PRIVATE -Wl,--section-start,.init=0x80A80000)
    # Die Plattform setzt -mregnames für alle ASM-Dateien; patchhook.S definiert r0…r31 selbst per .set.
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/boot/source/patchhook.S PROPERTIES
        COMPILE_OPTIONS -Wa,-mno-regnames)
    ogc_create_dol(nl_game_booter)
    dkp_add_embedded_binary_library(nl_game_booter_blob nl_game_booter)

    # Test-DOL für den Fortschrittsbalken des Booters (M11): prüft Pixel, meldet NL_PROGRESS ok.
    add_executable(progress_probe tests/wii/progress_probe.c boot/nl/progress.c)
    target_include_directories(progress_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/boot)
    target_compile_options(progress_probe PRIVATE -Wall -Wextra)
    ogc_create_dol(progress_probe)

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
        src/launch/wii_boot.cpp
        src/platform/nand_titles.cpp
        src/platform/wii_input.cpp
        src/platform/crash.cpp
        src/platform/storage.cpp
        src/platform/std_fs.cpp
        src/platform/scan_worker.cpp
        src/platform/png_decode.cpp
        src/platform/cover_worker.cpp
        src/platform/http_client.cpp
        src/platform/net_service.cpp
        src/platform/net_worker.cpp
        src/platform/emu_worker.cpp
        src/platform/sd_space.cpp
        src/platform/system.cpp
        src/platform/audio.cpp
        src/platform/music.cpp
        src/platform/wii_disc.cpp
        src/app/cover_service.cpp
        src/gfx/renderer.cpp
        src/gfx/font_loader.cpp
    )

    function(nl_wii_target name)
        add_executable(${name} ${ARGN})
        target_include_directories(${name} PRIVATE ${FREETYPE_STATIC_INCLUDE_DIRS} ${PNG_STATIC_INCLUDE_DIRS} boot/include)
        target_link_directories(${name} PRIVATE ${FREETYPE_STATIC_LIBRARY_DIRS})
        target_link_libraries(${name} PRIVATE nl_core nl_fonts nl_booter ${FREETYPE_STATIC_LIBRARIES} ${PNG_STATIC_LIBRARIES} nl_game_booter_blob fat wiiuse bte asnd vorbisidec ogg)
        target_compile_definitions(${name} PRIVATE NL_VERSION="${PROJECT_VERSION}")
        if(NL_TLS)
            target_link_libraries(${name} PRIVATE wiitls)
            target_compile_definitions(${name} PRIVATE NL_TLS=1)
        endif()
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
