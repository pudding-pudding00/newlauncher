import re
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from osc_emulators import (CATALOG, EmuInstallError, OscApp, format_installed_emus,  # noqa: E402
                           install_emulator, parse_installed_emus, parse_osc_list)

REPO = Path(__file__).resolve().parents[2]

# Gleicher Auszug wie tests/host/core/emu_catalog_test.cpp (CRLF).
LIST = ("Homebrew 2092896 v0.3.9e | - Updated with latest libogc\r\n"
        "=Emulators=\r\n"
        "fceugx 1793664000 8367 5065728 dol 2849329 56463 0 wwwwngcs /fceugx;/fceugx/roms;/apps/fceugx; . .\r\n"
        "FCE Ultra GX\r\n"
        "Tantric, Zopenko, Askot, others\r\n"
        "4.0.4\r\n"
        "5074467\r\n"
        "Nintendo Emulator\r\n"
        "A port of FCE Ultra to the Wii.\r\n"
        "=Utilities=\r\n"
        "Nintendont 1482019200 1156 1648128 dol 1077171 213279 0 wwwwngcsk /controllers;/apps/Nintendont; . .\r\n"
        "Nintendont\r\n"
        "FIX94, crediar\r\n"
        "Newer than 4.431\r\n"
        "1649864\r\n"
        "Gamecube Loader\r\n"
        "Nintendont allows you to run GameCube games.\r\n")


class ParseOscListTest(unittest.TestCase):
    def test_parses_blocks_and_skips_header_and_categories(self):
        apps = parse_osc_list(LIST)
        self.assertEqual(sorted(apps), ["Nintendont", "fceugx"])
        f = apps["fceugx"]
        self.assertEqual((f.timestamp, f.unzipped_size, f.zip_size), (1793664000, 5065728, 2849329))
        self.assertEqual((f.display_name, f.version), ("FCE Ultra GX", "4.0.4"))
        self.assertEqual(apps["Nintendont"].version, "Newer than 4.431")

    def test_lf_broken_numbers_and_truncated_block(self):
        text = LIST.replace("\r\n", "\n")
        broken = text.replace("fceugx 1793664000 8367 5065728", "fceugx 17x3664000 8367 5065728")
        self.assertEqual(sorted(parse_osc_list(broken)), ["Nintendont"])
        truncated = "\n".join(text.split("\n")[:13])  # Nintendont-Block nach drei Zeilen abgeschnitten
        self.assertEqual(sorted(parse_osc_list(truncated)), ["fceugx"])


class CatalogTest(unittest.TestCase):
    def test_catalog_matches_cpp(self):
        src = (REPO / "src/core/emu_catalog.cpp").read_text(encoding="utf-8")
        cpp = re.findall(r'\{"([^"]+)", "[^"]+", "sd:/apps/[^"]+"\}', src)
        self.assertEqual(len(cpp), 6)
        self.assertEqual([c.name for c in CATALOG], cpp)


class InstalledIniTest(unittest.TestCase):
    def test_format_matches_cpp_and_round_trips(self):
        text = format_installed_emus({"vbagx": (5, "2.0"), "fceugx": (1793664000, "4.0.4")})
        self.assertEqual(text, "; von newlauncher geschrieben (Spec 6.9)\n"
                               "[fceugx]\ntimestamp = 1793664000\nversion = 4.0.4\n"
                               "[vbagx]\ntimestamp = 5\nversion = 2.0\n")
        src = (REPO / "src/core/emu_catalog.cpp").read_text(encoding="utf-8")
        self.assertIn('"; von newlauncher geschrieben (Spec 6.9)\\n"', src)
        self.assertEqual(parse_installed_emus(text), {"vbagx": (5, "2.0"), "fceugx": (1793664000, "4.0.4")})

    def test_parse_skips_unknown_and_broken(self):
        text = "[fceugx]\ntimestamp = x\n[snes9xgx]\ntimestamp = 7\n[fremd]\ntimestamp = 1\n"
        self.assertEqual(parse_installed_emus(text), {"snes9xgx": (7, "")})


class InstallEmulatorTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.sd = self.tmp / "sd"
        self.sd.mkdir()
        self.app = OscApp("fceugx", 1793664000, 0, 0, "FCE Ultra GX", "4.0.4")

    def tearDown(self):
        self._tmp.cleanup()

    def _zip(self, entries: dict[str, bytes], method: int = zipfile.ZIP_DEFLATED, flags: int = 0) -> Path:
        path = self.tmp / "fceugx.zip"
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
            for name, data in entries.items():
                info = zipfile.ZipInfo("x")
                info.filename = name  # Rohname: ZipInfo(name) ersetzt unter Windows "\" und streicht "c:"
                info.compress_type = method
                z.writestr(info, data)
        if flags:  # writestr setzt flag_bits zurück: Bit 0 („verschlüsselt“) in lokalem und zentralem Kopf setzen
            raw = bytearray(path.read_bytes())
            for sig, off in ((b"PK\x03\x04", 6), (b"PK\x01\x02", 8)):
                pos = raw.find(sig)
                while pos >= 0:
                    raw[pos + off] |= flags
                    pos = raw.find(sig, pos + 4)
            path.write_bytes(bytes(raw))
        self.app = self.app._replace(zip_size=path.stat().st_size, unzipped_size=sum(map(len, entries.values())))
        return path

    def _files(self) -> set[str]:
        return {p.relative_to(self.sd).as_posix() for p in self.sd.rglob("*") if p.is_file()}

    def test_replaces_apps_keeps_other_existing_and_records_version(self):
        (self.sd / "apps/fceugx").mkdir(parents=True)
        (self.sd / "apps/fceugx/boot.dol").write_bytes(b"alt")
        (self.sd / "fceugx").mkdir()
        (self.sd / "fceugx/settings.xml").write_bytes(b"meine")
        (self.sd / "newlauncher").mkdir()
        (self.sd / "newlauncher/emulators_installed.ini").write_text(
            format_installed_emus({"vbagx": (5, "2.0")}), encoding="utf-8")
        z = self._zip({"apps/fceugx/boot.dol": b"neu", "apps/fceugx/meta.xml": b"<app/>",
                       "fceugx/settings.xml": b"standard", "fceugx/roms/": b""})
        self.assertEqual(install_emulator(self.sd, z, self.app), 3)
        self.assertEqual((self.sd / "apps/fceugx/boot.dol").read_bytes(), b"neu")
        self.assertEqual((self.sd / "fceugx/settings.xml").read_bytes(), b"meine")
        self.assertEqual((self.sd / "apps/fceugx/meta.xml").read_bytes(), b"<app/>")
        ini = (self.sd / "newlauncher/emulators_installed.ini").read_text(encoding="utf-8")
        self.assertEqual(parse_installed_emus(ini), {"vbagx": (5, "2.0"), "fceugx": (1793664000, "4.0.4")})
        self.assertEqual(self._files(), {"apps/fceugx/boot.dol", "apps/fceugx/meta.xml", "fceugx/settings.xml",
                                         "newlauncher/emulators_installed.ini"})

    def test_unsafe_path_writes_nothing(self):
        for bad in ("../evil.dol", "/abs.dol", "apps\\x.dol", "a/./b", "c:/x"):
            with self.subTest(bad=bad):
                z = self._zip({"apps/fceugx/boot.dol": b"neu", bad: b"x"})
                with self.assertRaises(EmuInstallError):
                    install_emulator(self.sd, z, self.app)
                self.assertEqual(self._files(), set())

    def test_crc_error_keeps_old_version(self):
        (self.sd / "apps/fceugx").mkdir(parents=True)
        (self.sd / "apps/fceugx/boot.dol").write_bytes(b"alt")
        z = self.tmp / "fceugx.zip"
        name = "apps/fceugx/boot.dol"
        with zipfile.ZipFile(z, "w", zipfile.ZIP_STORED) as zf:
            zf.writestr(name, b"N" * 4000)
        data = bytearray(z.read_bytes())
        data[30 + len(name) + 100] ^= 0xFF  # ein Datenbyte kippen: Größe stimmt, CRC nicht
        z.write_bytes(bytes(data))
        self.app = self.app._replace(zip_size=len(data), unzipped_size=4000)
        with self.assertRaises(EmuInstallError):
            install_emulator(self.sd, z, self.app)
        self.assertEqual((self.sd / "apps/fceugx/boot.dol").read_bytes(), b"alt")
        self.assertEqual(self._files(), {"apps/fceugx/boot.dol"})

    def test_same_rules_as_wii_method_encryption_and_dol(self):
        # Review M10: die Wii nimmt nur Methode 0/8 ohne Verschlüsselung und verlangt die Katalog-DOL.
        cases = {"bzip2": ({"apps/fceugx/boot.dol": b"neu"}, zipfile.ZIP_BZIP2, 0),
                 "verschlüsselt": ({"apps/fceugx/boot.dol": b"neu"}, zipfile.ZIP_DEFLATED, 1),
                 "ohne DOL": ({"apps/fceugx/boot.elf": b"elf"}, zipfile.ZIP_DEFLATED, 0)}
        for label, (entries, method, flags) in cases.items():
            with self.subTest(label):
                z = self._zip(entries, method, flags)
                with self.assertRaises(EmuInstallError):
                    install_emulator(self.sd, z, self.app)
                self.assertEqual(self._files(), set())

    def test_apps_prefix_case_insensitive(self):
        (self.sd / "apps/fceugx").mkdir(parents=True)
        (self.sd / "apps/fceugx/boot.dol").write_bytes(b"alt")
        z = self._zip({"Apps/fceugx/boot.dol": b"neu"})
        self.assertEqual(install_emulator(self.sd, z, self.app), 1)
        self.assertEqual((self.sd / "apps/fceugx/boot.dol").read_bytes(), b"neu")

    def test_wrong_zip_size_rejected(self):
        z = self._zip({"apps/fceugx/boot.dol": b"neu"})
        with self.assertRaises(EmuInstallError):
            install_emulator(self.sd, z, self.app._replace(zip_size=self.app.zip_size + 1))
        self.assertEqual(self._files(), set())


if __name__ == "__main__":
    unittest.main()
