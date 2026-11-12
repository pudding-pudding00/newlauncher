# Spike M13: Nimmt RetroArch auf der Wii einen ROM-Pfad als Argument an?

**Quelle:** `libretro/RetroArch`, Tag `v1.22.2` (Commit `69a4f0ea1e8aaf442ae4858f2e7f2b31a1776576`).
Gelesen wurde `frontend/drivers/platform_gx.c`, ohne Code zu übernehmen.

## Ergebnis: positiv, aber mit zwei Argumenten statt `{path}`

- `frontend_gx_get_env` (Zeilen 206–245, `HW_RVL`, kein Salamander) wertet `argc > 2` aus. Ist
  `argv[1]` nicht `-H`/`-C`, gilt `fill_pathname_join(argv[1], argv[2])` als Inhaltspfad (`content_path`).
  - Der Kommentar dort nennt es den Loader-Standard „Pfad und Dateiname getrennt“.
  - Das ist dasselbe Schema wie bei FCEUGX, Snes9xGX und VBA-GX: `{dir}|{file}`.
- Mit nur `argv[1]` (`argc == 2`) startet RetroArch **ohne** Inhalt. Ein einzelnes `{path}` reicht also nicht.
- **Pfadformat:** Die Pfade werden unverändert verbunden; RetroArch hängt selbst `sd:` und `usb:` ein
  (`gx_devices`). Erwartet wird also `sd:/roms/nes` + `Spiel.nes`. Weitere USB-Partitionen kennt RetroArch
  nicht; das passt zu `DeviceUnsupported` aus M12.
- **Kern-DOL direkt startbar:** Jede `<kern>_libretro_wii.dol` ist ein vollständiges RetroArch mit fest
  gebundenem Kern.
  - `frontend_gx_process_args` (Zeilen 460–476) setzt den Kern aus dem Dateinamen in `argv[0]`, wenn
    noch keiner gesetzt ist.
  - `argv[0]` muss also der DOL-Pfad sein. So füllt `expandArgs` ihn bereits.
- Ab Zeile 219 behandelt RetroArch `argv[0]`, das mit `usb1`/`usb2` beginnt (Arbeitsordner). Für uns ohne
  Belang, weil `argv[0]` `sd:/…` oder `usb:/…` ist.

## Entscheidung für Task 3

- RetroArch-Profile bleiben: `retroarch-fceumm`, `retroarch-snes9x2010`, `retroarch-gambatte`,
  `retroarch-mgba`.
- Argument-Schema `{dir}|{file}` statt `{path}` aus Spec v2 6.3 (Ruling im Ledger).
- `acceptsZip`:
  - `false` für `fceumm` und `snes9x2010`, weil die Wii-Builds kein Entpacken mitliefern (konservativ);
  - Nutzer können `.zip` über `[extensions]` und ein Spiel-Override trotzdem erzwingen.
- Ein Dolphin-Gegentest entfällt: Es gibt kein selbst erzeugtes Test-ROM, das in einem RetroArch-Kern
  sichtbar etwas zeigt, und die Quelle ist eindeutig. Geprüft wird auf der Hardware (W5).
