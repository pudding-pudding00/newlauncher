# Erzeugt about_entries.inc für die Seite „Über“ (M11, Spec v2 3.4) aus der Tabelle in THIRD_PARTY.md.
# Eine Quelle für Datei und Bildschirm: Name, Version und Lizenz jeder Tabellenzeile werden zu
#   {"Name", "Version", "Lizenz"},
# Läuft beim Konfigurieren (ohne Python, die Wii-CI hat es nicht sicher); eine Änderung an THIRD_PARTY.md
# konfiguriert neu. Backticks fallen weg, Anführungszeichen und Backslashes werden maskiert.

function(nl_generate_about_entries md out)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${md})
    file(STRINGS ${md} lines ENCODING UTF-8)
    set(body "// Erzeugt aus THIRD_PARTY.md durch cmake/NlAboutEntries.cmake – nicht von Hand ändern.\n")
    set(count 0)
    foreach(line IN LISTS lines)
        if(NOT line MATCHES "^\\|")
            continue()
        endif()
        # Erste drei Zellen: Name | Version | Lizenz
        if(NOT line MATCHES "^\\|([^|]*)\\|([^|]*)\\|([^|]*)\\|")
            continue()
        endif()
        set(cells "${CMAKE_MATCH_1}" "${CMAKE_MATCH_2}" "${CMAKE_MATCH_3}")
        set(out_cells "")
        foreach(cell IN LISTS cells)
            string(STRIP "${cell}" cell)
            string(REPLACE "`" "" cell "${cell}")
            string(REPLACE "\\" "\\\\" cell "${cell}")
            string(REPLACE "\"" "\\\"" cell "${cell}")
            list(APPEND out_cells "${cell}")
        endforeach()
        list(GET out_cells 0 name)
        if(name STREQUAL "Name" OR name MATCHES "^[-: ]*$")
            continue()
        endif()
        list(GET out_cells 1 version)
        list(GET out_cells 2 license)
        string(APPEND body "{\"${name}\", \"${version}\", \"${license}\"},\n")
        math(EXPR count "${count} + 1")
    endforeach()
    if(count EQUAL 0)
        message(FATAL_ERROR "THIRD_PARTY.md enthält keine Tabellenzeilen für die Seite „Über“")
    endif()
    # Nur bei Änderung schreiben, sonst baut jedes Konfigurieren die Szene neu.
    file(WRITE ${out}.tmp "${body}")
    configure_file(${out}.tmp ${out} COPYONLY)
endfunction()
