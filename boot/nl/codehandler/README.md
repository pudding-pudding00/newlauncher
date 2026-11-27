# Gecko code handler (M15)

`codehandler.s` is the Gecko code handler from Dolphin:

- Source: https://github.com/dolphin-emu/dolphin/blob/da9e9323ce8c7a6c0e06bb0d3f183a05753f8803/docs/codehandler.s
- Commit: `da9e9323ce8c7a6c0e06bb0d3f183a05753f8803`
- License: MIT (file header; full text in `LICENSES/MIT.txt`)
- Change: only a trailing newline added

The build assembles the file with the devkitPPC assembler (`-mgekko`) at 0x80001800 and generates `codehandler.h`
and `codehandler_info.h` from it (see `cmake/NlCodehandler.cmake`). No binary blob is in the repository.

Interface to the booter (`boot/source/fst.c`, `load_handler()`):

- Entry point `_start` = 0x800018A8; the hooks in `patchhook.S` jump there.
- Offset 0x104/0x108 holds `lis r15,codelist@h` / `ori r15,r15,codelist@l`. The booter patches both half-words when
  codes are present; the generator script aborts if something else is there.
- The code list is behind the handler up to 0x80003000 (start of the OS globals).
