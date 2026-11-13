# Erzeugt aus dem assemblierten Codehandler (M15, boot/nl/codehandler) zwei Header für Booter und Launcher:
#   codehandler.h       – static const unsigned char nl_codehandler[] (nur fst.c bindet ihn ein)
#   codehandler_info.h  – Größe, Einsprung, Adresse der Codeliste und die Patch-Stellen des Codelisten-Zeigers
#
#   cmake -DBIN=<handler.bin> -DELF=<handler.elf> -DOUT_DIR=<dir> -P NlCodehandler.cmake
#
# Läuft ohne Python (die Wii-CI hat es nicht sicher). Bricht ab, wenn der Handler nicht zur Schnittstelle von
# load_handler() passt: an 0x104 muss „lis r15,codelist@h“ stehen, an 0x108 „ori r15,r15,codelist@l“.
cmake_minimum_required(VERSION 3.20)

foreach(var BIN ELF OUT_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "NlCodehandler.cmake: ${var} fehlt")
    endif()
endforeach()

set(LOAD_ADDR 0x80001800)
set(CODELIST_END 0x80003000)  # Beginn der OS-Globals; wie upstream WiiFlow (codelistend)

file(READ "${BIN}" hex HEX)
string(LENGTH "${hex}" hexlen)
math(EXPR size "${hexlen} / 2")
if(size LESS 0x10C)
    message(FATAL_ERROR "Codehandler zu klein (${size} Bytes)")
endif()

# Halbwort (4 Hex-Zeichen) an Byte-Offset off
function(nl_half off outvar)
    math(EXPR pos "${off} * 2")
    string(SUBSTRING "${hex}" ${pos} 4 h)
    set(${outvar} "${h}" PARENT_SCOPE)
endfunction()

nl_half(0x104 op_hi)
nl_half(0x108 op_lo)
if(NOT op_hi STREQUAL "3de0" OR NOT op_lo STREQUAL "61ef")
    message(FATAL_ERROR "Codehandler: an 0x104/0x108 stehen ${op_hi}…/${op_lo}… statt lis r15 (3de0) / ori r15,r15 (61ef)")
endif()
nl_half(0x106 imm_hi)
nl_half(0x10A imm_lo)
set(codelist "0x${imm_hi}${imm_lo}")
math(EXPR handler_end "${LOAD_ADDR} + ${size}" OUTPUT_FORMAT HEXADECIMAL)
math(EXPR d_codelist "${codelist}")
math(EXPR d_load "${LOAD_ADDR}")
math(EXPR d_listend "${CODELIST_END}")
math(EXPR d_end "${handler_end}")
if(d_codelist LESS d_load OR d_codelist GREATER_EQUAL d_listend OR d_end GREATER d_listend)
    message(FATAL_ERROR "Codehandler: Codeliste ${codelist} bzw. Ende ${handler_end} außerhalb ${LOAD_ADDR}–${CODELIST_END}")
endif()
math(EXPR patch_hi "${LOAD_ADDR} + 0x106" OUTPUT_FORMAT HEXADECIMAL)
math(EXPR patch_lo "${LOAD_ADDR} + 0x10A" OUTPUT_FORMAT HEXADECIMAL)

# ELF-Kopf: e_entry (big-endian) an Offset 24
file(READ "${ELF}" entry_hex OFFSET 24 LIMIT 4 HEX)
set(entry "0x${entry_hex}")

string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")

file(WRITE "${OUT_DIR}/codehandler.h.tmp"
"/* Erzeugt von cmake/NlCodehandler.cmake aus boot/nl/codehandler/codehandler.s (MIT, siehe dort). Nicht bearbeiten. */
#ifndef NL_CODEHANDLER_H
#define NL_CODEHANDLER_H
#include \"codehandler_info.h\"
static const unsigned char nl_codehandler[NL_CH_SIZE] = {
\t${bytes}
};
#endif
")
file(WRITE "${OUT_DIR}/codehandler_info.h.tmp"
"/* Erzeugt von cmake/NlCodehandler.cmake aus dem assemblierten Codehandler. Nicht bearbeiten. */
#ifndef NL_CODEHANDLER_INFO_H
#define NL_CODEHANDLER_INFO_H
#define NL_CH_LOAD_ADDR ${LOAD_ADDR}u
#define NL_CH_SIZE ${size}u
#define NL_CH_ENTRY ${entry}u          /* _start; Ziel der Hooks in patchhook.S */
#define NL_CH_CODELIST ${codelist}u       /* Symbol codelist: Beginn der Codeliste */
#define NL_CH_CODELIST_END ${CODELIST_END}u
#define NL_CH_PATCH_HI ${patch_hi}u       /* Halbwort von lis r15,codelist@h */
#define NL_CH_PATCH_LO ${patch_lo}u       /* Halbwort von ori r15,r15,codelist@l */
#endif
")
# Nur bei Änderung ersetzen, damit abhängige Ziele nicht unnötig neu bauen.
foreach(name codehandler.h codehandler_info.h)
    file(COPY_FILE "${OUT_DIR}/${name}.tmp" "${OUT_DIR}/${name}" ONLY_IF_DIFFERENT)
    file(REMOVE "${OUT_DIR}/${name}.tmp")
endforeach()
