# Plan M20: Rückkehr-Stub

Spec: `docs/superpowers/specs/2026-11-25-m20-direktstart-design.md`, Abschnitt 4.

1. `src/launch/return_stub/stub_check.h` (reines C, host-testbar): Prüfsumme, Blockprüfung, DOL-Kopf.
   Host-Tests zuerst.
2. `core/return_stub` (C++): Parameterblock bauen; Tests zuerst (Layout, argv, Grenzen, Extent-Summe).
3. Stub selbst: `crt0.S` (Sprung + „STUBHAXX“), `stub.c` (IPC, SDIO, DOL-Laden), `stub.ld`
   (0x80001800, ≤ 0x1000). CMake wie der App-Booter, als eingebettetes Binary.
4. `launch/return_stub.cpp`: Extents der eigenen boot.dol (RawDevice SD + fileExtents), Stub und Block
   schreiben, Caches. Aufruf im DOL-Startpfad vor `unmountAll`; Log `NL_RETURN_STUB installed extents=N`.
5. Start: vorgefundenen Fortschrittscode loggen. ExitToHbc: bei installiertem Stub HBC-Titel starten.
6. Dolphin-Smoke `--return-stub`: argv_echo als Emulator, Erfolg bei zweitem `NL_READY`.
7. Hardware-Test, Abnahme-Liste `docs/acceptance/m20-wii.md`, STATUS, PR.
