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
    src/core/theme_file.cpp
    src/core/categories.cpp
    src/core/game_filter.cpp
    src/core/play_stats.cpp
    src/core/sha256.cpp
    src/core/parental.cpp
    src/core/rgb5a3.cpp
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
    src/core/disc_id.cpp
    src/core/scanner.cpp
    src/core/library_store.cpp
    src/core/dol.cpp
    src/core/emulators.cpp
    src/core/launch_args.cpp
    src/core/launch_plan.cpp
    src/core/n64.cpp
    src/core/game_options.cpp
    src/core/options_model.cpp
    src/core/gecko_txt.cpp
    src/core/cheat_select.cpp
    src/core/gct.cpp
    src/core/wip_patch.cpp
    src/core/cheat_model.cpp
    src/core/cheat_source.cpp
    src/core/n64_choice.cpp
    src/core/vc_map.cpp
    src/core/wii_title.cpp
    src/core/emunand.cpp
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
    src/core/self_update.cpp
    src/core/return_stub.cpp
    src/core/nin_cfg.cpp
    src/core/fat_chain.cpp
    src/core/wbfs_map.cpp
    src/core/wbfs_part.cpp
    src/core/partition_table.cpp
    src/core/device_names.cpp
    src/core/data_dir.cpp
    src/core/library_merge.cpp
    src/core/library_sources.cpp
    src/core/frag_list.cpp
    src/core/extent_units.cpp
    src/core/ios_info.cpp
    src/core/cios_scan.cpp
    src/core/emu_profiles.cpp
    src/core/sysinfo.cpp
    src/core/setup_wizard.cpp
    src/core/cover_download.cpp
    src/core/lz77.cpp
    src/core/gc_fst.cpp
    src/core/gc_banner.cpp
    src/core/gc_disc.cpp
    src/core/u8_archive.cpp
    src/core/imd5.cpp
    src/core/tpl.cpp
    src/core/brlyt.cpp
    src/core/banner_scene.cpp
    src/core/brlan.cpp
    src/core/dsp_adpcm.cpp
    src/core/bns.cpp
    src/core/banner_sound.cpp
    src/core/channel_banner.cpp
    src/core/banner_budget.cpp
    src/core/aes128.cpp
    src/core/wii_partition.cpp
    src/core/remote_proto.cpp
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
    src/ui/sysinfo_scene.cpp
    src/ui/setup_scene.cpp
    src/ui/debug_overlay.cpp
    src/ui/pointer_cursor.cpp
    src/ui/startup_scene.cpp
    src/ui/jump_bar.cpp
    src/ui/keyboard.cpp
    src/ui/banner_view.cpp
    src/ui/details_scene.cpp
    src/ui/options_scene.cpp
    src/ui/cheats_scene.cpp
    src/ui/vc_map_scene.cpp
    src/ui/dialog_scene.cpp
    src/ui/launch_text.cpp
    src/ui/pin_scene.cpp
    src/ui/parental_scene.cpp
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

    # HTTPS und HTTP/1.1 über wii_tls v0.2.0 (M11, Spec v2 4.1): vendort nach third_party/wii_tls
    # (tools/vendor_wii_tls.py), holt Mbed TLS 4.1.1 und zlib 1.3.1 (Inflate mit Z_PREFIX für gzip) per
    # FetchContent mit festem SHA256. Ohne NL_TLS baut der Launcher wie bisher nur HTTP.
    option(NL_TLS "HTTPS über wii_tls" ON)
    if(NL_TLS)
        set(WTLS_PLATFORM ogc CACHE STRING "" FORCE)
        set(WTLS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(WTLS_BUILD_HWTEST OFF CACHE BOOL "" FORCE)
        set(WTLS_BUILD_BENCH OFF CACHE BOOL "" FORCE)
        set(WTLS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        # Krypto-Einstellungen wie im getesteten Release 0.1.0 (auf der Wii gemessen); FORCE, damit ein alter
        # Build-Cache mit den Standardwerten älterer Stände (Everest aus, O2) nicht hängen bleibt.
        set(WTLS_EVEREST ON CACHE BOOL "" FORCE)
        set(WTLS_ACCEL ON CACHE BOOL "" FORCE)
        set(WTLS_OPT O3 CACHE STRING "" FORCE)
        set(WTLS_GZIP ON CACHE BOOL "" FORCE)
        # Mbed TLS 4.1.1 ruft cmake_minimum_required mit einer Version < 3.10 auf, neuere CMake-Versionen
        # warnen dafür. Die Mindest-Policy-Version hebt nur den Upstream-Code an; danach wieder aus.
        set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
        add_subdirectory(third_party/wii_tls EXCLUDE_FROM_ALL)
        unset(CMAKE_POLICY_VERSION_MINIMUM)
        # GCC meldet bei -O3 in Mbed TLS x509.c (mbedtls_x509_dn_gets) einen Fehlalarm zu snprintf-Grenzen;
        # nur für dieses Fremdziel abgeschaltet, unser Code bleibt bei 0 Warnungen.
        if(TARGET mbedx509)
            target_compile_options(mbedx509 PRIVATE -Wno-format-truncation)
        endif()
        # zlib 1.3.1 benennt bei Z_PREFIX alle Symbole außer z_errmsg um; ohne Umbenennung kollidiert
        # wtls_zlib mit dem statischen portlibs-libz (FreeType/libpng). Nur dieses Fremdziel anpassen.
        if(TARGET wtls_zlib)
            target_compile_definitions(wtls_zlib PRIVATE z_errmsg=wtls_z_errmsg)
        endif()
    endif()

    # NTFS nur lesend (M16, Spec v2 9.1): libntfs vendort nach third_party/libntfs (tools/vendor_fs.py).
    # Ohne NL_FS_NTFS bleibt NTFS „nicht eingehängt“ mit Grund, wie in M12. ext ist laut Spike negativ.
    option(NL_FS_NTFS "NTFS-Partitionen nur lesend einhängen" ON)
    if(NL_FS_NTFS)
        add_subdirectory(third_party/libntfs EXCLUDE_FROM_ALL)
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

    # Rückkehr-Stub (M20, Spec 4): frei stehend bei 0x80001800, wie der App-Booter gebaut.
    set(NL_RSTUB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/launch/return_stub)
    set(NL_RSTUB_ELF ${CMAKE_CURRENT_BINARY_DIR}/return_stub.elf)
    set(NL_RSTUB_BIN ${CMAKE_CURRENT_BINARY_DIR}/return_stub.bin)
    add_custom_command(
        OUTPUT ${NL_RSTUB_BIN}
        COMMAND ${CMAKE_C_COMPILER} -mcpu=750 -meabi -mhard-float -mregnames -G0 -Os -std=c11 -ffreestanding
                -fno-builtin -fno-tree-loop-distribute-patterns -nostdlib -nostartfiles -Wall -Wextra -Werror
                -Wl,-T,${NL_RSTUB_DIR}/stub.ld -Wl,--gc-sections -Wl,--no-warn-rwx-segments
                -o ${NL_RSTUB_ELF} ${NL_RSTUB_DIR}/crt0.S ${NL_RSTUB_DIR}/stub.c -lgcc
        COMMAND ${DKP_OBJCOPY} -O binary ${NL_RSTUB_ELF} ${NL_RSTUB_BIN}
        DEPENDS ${NL_RSTUB_DIR}/stub.c ${NL_RSTUB_DIR}/stub_check.h ${NL_RSTUB_DIR}/crt0.S ${NL_RSTUB_DIR}/stub.ld
        COMMENT "Rückkehr-Stub bauen"
        VERBATIM
    )
    add_custom_target(nl_return_stub DEPENDS ${NL_RSTUB_BIN})
    dkp_set_target_file(nl_return_stub ${NL_RSTUB_BIN})
    dkp_add_embedded_binary_library(nl_rstub nl_return_stub)

    # Gecko-Codehandler (M15, Spec v2 3.3): aus Quelltext (Dolphin docs/codehandler.s, MIT) ab 0x80001800 assembliert.
    # NlCodehandler.cmake erzeugt das Byte-Array für den Booter und die Adressen für Booter und Launcher und bricht ab,
    # wenn die Patch-Stellen des Codelisten-Zeigers (0x104/0x108) nicht passen.
    set(NL_CH_SRC ${CMAKE_CURRENT_SOURCE_DIR}/boot/nl/codehandler/codehandler.s)
    set(NL_CH_DIR ${CMAKE_CURRENT_BINARY_DIR}/codehandler)
    add_custom_command(
        OUTPUT ${NL_CH_DIR}/codehandler.h ${NL_CH_DIR}/codehandler_info.h ${NL_CH_DIR}/codehandler.bin
        COMMAND ${CMAKE_COMMAND} -E make_directory ${NL_CH_DIR}
        COMMAND ${CMAKE_C_COMPILER} -x assembler -Wa,-mgekko -Wa,-mno-regnames -nostdlib -nostartfiles
                -Wl,-Ttext=0x80001800 -Wl,-e,_start -Wl,--no-warn-rwx-segments
                -o ${NL_CH_DIR}/codehandler.elf ${NL_CH_SRC}
        COMMAND ${DKP_OBJCOPY} -O binary ${NL_CH_DIR}/codehandler.elf ${NL_CH_DIR}/codehandler.bin
        COMMAND ${CMAKE_COMMAND} -DBIN=${NL_CH_DIR}/codehandler.bin -DELF=${NL_CH_DIR}/codehandler.elf
                -DOUT_DIR=${NL_CH_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/NlCodehandler.cmake
        DEPENDS ${NL_CH_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/NlCodehandler.cmake
        COMMENT "Codehandler assemblieren"
        VERBATIM
    )
    add_custom_target(nl_codehandler DEPENDS ${NL_CH_DIR}/codehandler.h ${NL_CH_DIR}/codehandler_info.h)

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
        ${CMAKE_CURRENT_SOURCE_DIR}/boot ${CMAKE_CURRENT_SOURCE_DIR}/src ${NL_CH_DIR})
    add_dependencies(nl_game_booter nl_codehandler)
    # NL_BOOTER_CHEATS=1 (M15): Codehandler aus Quelltext statt des Ocarina-Blobs (M11, Spec v2 3.2).
    target_compile_definitions(nl_game_booter PRIVATE HAVE_CONFIG_H NL_VERSION="${PROJECT_VERSION}" NL_BOOTER_CHEATS=1)
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
        src/launch/return_stub.cpp
        src/launch/channel.cpp
        src/launch/wii_boot.cpp
        src/platform/nand_titles.cpp
        src/platform/wii_input.cpp
        src/platform/crash.cpp
        src/platform/storage.cpp
        src/platform/std_fs.cpp
        src/platform/scan_worker.cpp
        src/platform/png_decode.cpp
        src/platform/theme_loader.cpp
        src/platform/cover_worker.cpp
        src/platform/banner_source.cpp
    src/platform/banner_tex.cpp
        src/platform/banner_worker.cpp
        src/platform/http_client.cpp
        src/platform/net_service.cpp
        src/platform/remote_server.cpp
        src/platform/net_worker.cpp
        src/platform/emu_worker.cpp
        src/platform/cheat_worker.cpp
        src/platform/sd_space.cpp
        src/platform/system.cpp
        src/platform/system_probe.cpp
        src/platform/audio.cpp
        src/platform/music.cpp
        src/platform/wii_disc.cpp
        src/platform/disc_drive.cpp
        src/platform/fs_extents.cpp
        src/platform/nand_emu.cpp
        src/app/cover_service.cpp
        src/app/banner_service.cpp
        src/gfx/renderer.cpp
        src/gfx/font_loader.cpp
    )

    function(nl_wii_target name)
        add_executable(${name} ${ARGN})
        target_include_directories(${name} PRIVATE ${FREETYPE_STATIC_INCLUDE_DIRS} ${PNG_STATIC_INCLUDE_DIRS} boot/include ${NL_CH_DIR})
        add_dependencies(${name} nl_codehandler)
        target_link_directories(${name} PRIVATE ${FREETYPE_STATIC_LIBRARY_DIRS})
        target_link_libraries(${name} PRIVATE nl_core nl_fonts nl_booter nl_rstub ${FREETYPE_STATIC_LIBRARIES} ${PNG_STATIC_LIBRARIES} nl_game_booter_blob fat wiiuse bte asnd vorbisidec ogg)
        # NL_WII_CHEATS=1 (M15): der Booter bringt den Codehandler mit, die Cheat-Szene bietet Wii-Codes an.
        target_compile_definitions(${name} PRIVATE NL_VERSION="${PROJECT_VERSION}" NL_WII_CHEATS=1)
        if(NL_TLS)
            target_link_libraries(${name} PRIVATE wiitls)
            target_compile_definitions(${name} PRIVATE NL_TLS=1)
        endif()
        if(NL_FS_NTFS)
            target_link_libraries(${name} PRIVATE ntfs)
            target_compile_definitions(${name} PRIVATE NL_FS_NTFS=1)
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
