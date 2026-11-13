# libntfs (extremscorner, ae00fc8) als statische Bibliothek `ntfs` (M16, Spec v2 9.1).
# upstream/ erzeugt tools/vendor_fs.py aus dem festen Commit plus patches/; nie von Hand ändern.
# Eingehängt wird nur lesend (NTFS_READ_ONLY), siehe src/platform/storage.cpp.
file(GLOB NL_NTFS_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/upstream/source/*.c)
# nl/ ist eigener Code (Läufe einer Datei für die Fragmentliste, M16 Task 3), kein Upstream.
add_library(ntfs STATIC ${NL_NTFS_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/nl/nl_ntfs_extents.c)
target_include_directories(ntfs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/upstream/include ${CMAKE_CURRENT_SOURCE_DIR}/nl
                                PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/upstream/source)
target_compile_definitions(ntfs PRIVATE HAVE_CONFIG_H)
# Upstream-Warnungen, die keine Fehler im genutzten Pfad sind (Spike-Notiz, Abschnitt Warnungen):
# Null-Längen-Arrays in reparse.c/ea.c, gesetzte aber ungenutzte Variablen in attrib.c und ein
# Zeigervergleich nach free in inode.c. Gepackte Strukturen sind das NTFS-Plattenformat.
target_compile_options(ntfs PRIVATE -O2 "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/nl_compat.h"
    -Wall -Wno-address-of-packed-member -Wno-zero-length-bounds -Wno-unused-but-set-variable -Wno-use-after-free)
