# Spike M15: Codehandler aus belegter Quelle

**Frage:** Gibt es einen Gecko-Codehandler als Quelltext mit einer Lizenz, die mit GPLv3 verträglich ist (Spec v2 3.3), der zur Schnittstelle des WiiFlow-Booters passt?

**Ergebnis: positiv.** Wir nehmen `docs/codehandler.s` aus Dolphin (MIT).

## Kandidaten

| Kandidat | Lizenz | Vollständige Quelle | Ergebnis |
|---|---|---|---|
| Dolphin `docs/codehandler.s` (Commit `da9e9323ce8c7a6c0e06bb0d3f183a05753f8803`) | **MIT** im Dateikopf, Copyright 2010–2017 Nuke, brkirch, Y.S, Kenobi, gamemasterplc; „Based off of codehandleronly.s from Gecko OS source code“ | ja, 1059 Zeilen `.s`, Linkerskript `docs/codehandler.ld` | **gewählt** |
| Nintendont `codehandler/codehandleronly.s` (`FIX94/Nintendont`, Commit `c683db3`) | kein Kopf in der Datei; das Repo hat keine Lizenzdatei, der Kernel steht unter „GPL version 2“ ohne „oder später“ | ja | verworfen: GPLv2-only bzw. unbelegt, nicht GPLv3-verträglich |
| Gecko-OS-Quelle (`MP2E/Gecko-OS`, `hinxx/GeckoOS`) | keine Lizenzangabe im Repo | ja | verworfen: Lizenz unbelegt |
| weitere (Neogamma-, swiss-gc-Kopien) | nicht geprüft, weil Dolphin die Frage beantwortet | – | – |

## Prüfung der Dolphin-Quelle

- **Assembliert** mit devkitPPC: `powerpc-eabi-as -mregnames -mgekko`, dann `powerpc-eabi-ld -Ttext=0x80001800 -e _start` und `objcopy -O binary`.
  - Einzige Meldung: „end of file not at end of a line“, weil die Datei ohne Zeilenende endet. Die eingebundene Kopie bekommt ein abschließendes Zeilenende; sonst bleibt sie unverändert.
- **Größe:** 2880 Bytes (0xB40). Damit belegt der Handler 0x80001800–0x80002340 und passt in den Bereich, den `load_handler()` leert (0x1800 Bytes).
- **Symbole** (Ladeadresse 0x80001800):
  - `gameid` 0x80001800;
  - `cheatdata` 0x80001808;
  - `_start` **0x800018A8**: Darauf springen die Hooks in `patchhook.S` und `main.cpp` (`ori …, 0x18A8`);
  - `codelist` 0x80002338, nur 8 Bytes im Handler.
- **Adresse der Codeliste:** `lis r15, codelist@h` liegt an Offset 0x104, `ori r15, r15, codelist@l` an 0x108.
  - Die Halbwörter liegen also bei **0x80001906 und 0x8000190A**.
  - Genau dort trägt `fst.c` (`load_handler()`, Zweig ohne Debugger) heute schon die Adresse der GCT ein.
  - Der Booter braucht keine neue Schnittstelle.
- **Debugger-Zweig:** `codehandler.h` (mit Debugger, Patch-Offsets 0x1CDE/0x1F5A) hat keine belegte Quelle. M15 baut nur den Zweig ohne Debugger.
  - `debugger` bleibt 0.
  - Der Blob `codehandler.h` fliegt aus dem Quellbaum, ebenso `codehandleronly.h`.

## Entscheidungen für Task 5

- `boot/nl/codehandler/codehandler.s` = Dolphin-Datei mit unverändertem Kopf, plus Zeilenende am Dateiende. Dazu `README.md` mit Quelle, Commit und Lizenz.
- Der Build erzeugt den Handler und prüft die Patch-Offsets 0x106/0x10A beim Bauen: An 0x104 muss `lis r15` stehen, an 0x108 `ori r15,r15`. Sonst bricht der Build ab.
- **Größengrenze der GCT (Ruling):** Der Handler liest die Codes über den eingetragenen Zeiger aus MEM2. Die Grenze ist deshalb `NL_GCT_MAX` (Größe des reservierten Bereichs) und nicht `codelistend − codelist`; das wären im Handler nur 8 Bytes.
