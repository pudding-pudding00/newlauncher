# Gecko-Codehandler (M15)

`codehandler.s` ist der Gecko-Codehandler aus Dolphin:

- Quelle: https://github.com/dolphin-emu/dolphin/blob/da9e9323ce8c7a6c0e06bb0d3f183a05753f8803/docs/codehandler.s
- Commit: `da9e9323ce8c7a6c0e06bb0d3f183a05753f8803`
- Lizenz: MIT (Kopf der Datei; Volltext in `LICENSES/MIT.txt`)
- Änderung: nur ein abschließender Zeilenumbruch ergänzt

Der Build assembliert die Datei mit dem devkitPPC-Assembler (`-mgekko`) an 0x80001800 und erzeugt daraus
`codehandler.h` und `codehandler_info.h` (siehe `cmake/NlCodehandler.cmake`). Kein Binärblob liegt im Repo.

Schnittstelle zum Booter (`boot/source/fst.c`, `load_handler()`):

- Einsprung `_start` = 0x800018A8; dorthin springen die Hooks aus `patchhook.S`.
- An Offset 0x104/0x108 steht `lis r15,codelist@h` / `ori r15,r15,codelist@l`. Der Booter patcht die beiden
  Halbwörter, wenn Codes vorhanden sind; das Generator-Skript bricht ab, wenn dort etwas anderes steht.
- Die Codeliste liegt hinter dem Handler bis 0x80003000 (Beginn der OS-Globals).
