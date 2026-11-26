"""Cloud-Smokes gegen die Fakes (M27 Drive, M28 OneDrive): --cloud-fake drive|onedrive --cloud-step <schritt>.

Jeder Schritt meldet sich zuerst per Gerätecode an ([debug] cloud_device_auto = 1, der Fake genehmigt sofort),
denn die Tokens auf der SD sind an das Gerät gebunden und lassen sich nicht vorab hinlegen. Ohne cloud_sync folgt
auf die Anmeldung der Wechsel-Abgleich über alle Spiele (M28). Danach:
  link     – nur anmelden: NL_CLOUD_LINKED <anbieter>
  push     – session.ini liegt da (Spiel lief), GB-Spielstand A → Abgleich nach der Anmeldung lädt hoch, Fake hat A
  pull     – Cloud hat B, lokal nichts → cloud_sync holt B auf die SD
  conflict – lokal A, Cloud B, keine Basis → Konflikt, cloud_conflict = cloud → B auf der SD
  refresh  – erste Datei-Anfrage bekommt 401 → Refresh → Upload klappt: NL_CLOUD_REFRESH ok
  offline  – nach der Anmeldung schließt der Fake den Listener (Verbindung abgewiesen) → NL_CLOUD <einheit> skip offline
  syncall  – GB nur lokal (A), Galaxy-NAND nur in der Cloud (B) → NL_CLOUD_SYNCALL pulled=1 pushed=1
  switch   – GB lokal A, Cloud B → Wechsel-Dialog, cloud_switch = wii → NL_CLOUD_SWITCH uploaded=1 conflict=0
  unlink   – anmelden, dann cloud_unlink = 1 → Drive: NL_CLOUD_UNLINKED revoke=ok, OneDrive: revoke=none
Der Galaxy-NAND-Spielstand (save_smoke „nand“) wird vor jedem Lauf geleert, damit er die Zählung nicht verfälscht.
Nach jedem Lauf: Kein Token, kein Gerätecode und kein Client-Secret im Log.
"""

from __future__ import annotations

import io
import shutil
import zipfile
import zlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

import cloud_fake
import save_smoke
from dolphin import sd_sync_dir

STEPS = ("link", "push", "pull", "conflict", "refresh", "offline", "syncall", "switch", "unlink")
PROVIDERS = ("drive", "onedrive")

CLIENT_ID = "fake-client.apps.googleusercontent.com"
OD_CLIENT_ID = "00000000-0000-0000-0000-00000fake0d1"
NAND_UNIT = "wii-RMGP"
CLIENT_SECRET = "GOCSPX-fake-secret-not-real"
CLOUD_REL = "newlauncher/cloud"
SESSION_STARTED = 1700000000
CLOUD_SAVED = 1700000500


def slug(stem: str) -> str:
    """Wie slugOf in core/save_locator: a-z0-9, sonst ein Bindestrich, höchstens 40 Zeichen."""
    out = ""
    for c in stem.lower():
        if ("a" <= c <= "z") or ("0" <= c <= "9"):
            out += c
        elif out and out[-1] != "-":
            out += "-"
    return out[:40].rstrip("-") or "rom"


def rom_unit(game_key: str) -> str:
    """Einheit eines ROMs: <konsole>-<slug>-<crc32(schlüssel)>."""
    console, _, rest = game_key.partition("/")
    stem = rest.rsplit("/", 1)[-1].rsplit(".", 1)[0]
    return f"{console}-{slug(stem)}-{zlib.crc32(game_key.encode()) & 0xFFFFFFFF:08x}"


def save_zip(unit: str, files: dict[str, bytes], device: str = "Dolphin-PC", saved: int = CLOUD_SAVED) -> bytes:
    """Paket wie core/save_archive: Meta zuerst (gespeichert), Dateien mit Deflate."""
    h = cloud_fake.content_hash(files)
    meta = (f"[save]\nformat = 1\nunit = {unit}\ntitle = Smoke\ndevice = {device}\nsaved = {saved}\n"
            f"hash = {h}\nfiles = {len(files)}\n")
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        z.writestr(cloud_fake.META_NAME, meta, compress_type=zipfile.ZIP_STORED)
        for name, data in files.items():
            z.writestr(name, data, compress_type=zipfile.ZIP_DEFLATED)
    return buf.getvalue()


def make_fake(step: str, provider: str = "drive") -> cloud_fake.FakeDrive:
    if step not in STEPS:
        raise ValueError(f"unbekannter Cloud-Schritt: {step}")
    if provider not in PROVIDERS:
        raise ValueError(f"unbekannter Cloud-Anbieter: {provider}")
    cls = cloud_fake.FakeOneDrive if provider == "onedrive" else cloud_fake.FakeDrive
    return cls(approve="auto", expire_access_once=step == "refresh", offline_after_link=step == "offline")


def provider_of(fake: cloud_fake.FakeDrive) -> str:
    return "onedrive" if isinstance(fake, cloud_fake.FakeOneDrive) else "drive"


def cloud_hash(fake: cloud_fake.FakeDrive, name: str) -> tuple[str | None, str | None]:
    """(hash, unit) der Meta im Fake: Drive aus appProperties, OneDrive aus der description (ohne Einheit)."""
    entry = fake.state_dump()["files"].get(name, {})
    if isinstance(fake, cloud_fake.FakeOneDrive):
        meta = cloud_fake.parse_onedrive_description(entry.get("description", ""))
        return meta.get("hash"), name.removesuffix(".zip")
    props = entry.get("appProperties", {})
    return props.get("hash"), props.get("unit")


@dataclass
class CloudPlan:
    step: str
    fake: cloud_fake.FakeDrive
    unit: str = ""
    debug_lines: list[str] = field(default_factory=list)
    markers: list[str] = field(default_factory=list)
    config_append: str = ""


def prepare(step: str, fake: cloud_fake.FakeDrive, user_dir: Path) -> CloudPlan:
    """Schreibt Konfiguration, Spielstand und session.ini; liefert Debug-Zeilen und Marker."""
    sd = sd_sync_dir(user_dir)
    shutil.rmtree(sd / CLOUD_REL, ignore_errors=True)  # Tokens und Basis eines früheren Laufs
    plan = CloudPlan(step, fake)
    provider = provider_of(fake)
    # Beide IDs stehen drin (wie bei einem Nutzer, der beide eingerichtet hat); aktiv ist nur provider.
    plan.config_append = (f"[cloud]\nprovider = {provider}\ndrive_client_id = {CLIENT_ID}\n"
                          f"drive_client_secret = {CLIENT_SECRET}\nonedrive_client_id = {OD_CLIENT_ID}\n")
    plan.debug_lines.append("cloud_device_auto = 1")
    plan.markers.append(f"NL_CLOUD_LINKED {provider}")
    nand = save_smoke.fixture("nand", user_dir)
    (nand.host_dir / nand.file_name).unlink(missing_ok=True)
    if step == "link":
        return plan
    if step == "unlink":
        plan.debug_lines.append("cloud_unlink = 1")
        plan.markers.append("NL_CLOUD_UNLINKED revoke=" + ("none" if provider == "onedrive" else "ok"))
        return plan

    fx = save_smoke.fixture("gb", user_dir)
    plan.unit = rom_unit(fx.key)
    save_smoke._add_gb_save_dir(sd / "newlauncher/emulators.ini", fx.launcher_dir)
    fx.host_dir.mkdir(parents=True, exist_ok=True)
    local = fx.host_dir / fx.file_name
    local.unlink(missing_ok=True)
    if step != "pull":
        local.write_bytes(save_smoke.BYTES_A)
    if step == "syncall":
        fake.seed(NAND_UNIT, save_zip(NAND_UNIT, {nand.file_name: save_smoke.BYTES_B}), device="Dolphin-PC",
                  saved=CLOUD_SAVED)
        plan.markers += [f"NL_CLOUD {NAND_UNIT} pull ok", f"NL_CLOUD {plan.unit} push ok",
                         "NL_CLOUD_SYNCALL pulled=1 pushed=1", "NL_CLOUD_SWITCH uploaded=1 conflict=0"]
        return plan
    if step in ("pull", "conflict", "switch"):
        fake.seed(plan.unit, save_zip(plan.unit, {fx.file_name: save_smoke.BYTES_B}), device="Dolphin-PC",
                  saved=CLOUD_SAVED)
    if step == "switch":
        plan.debug_lines.append("cloud_switch = wii")
        plan.markers += [f"NL_CLOUD {plan.unit} conflict", "NL_CLOUD_SWITCH_CHOICE wii",
                         "NL_CLOUD_SWITCH uploaded=1 conflict=0"]
        return plan

    if step == "push":
        (sd / CLOUD_REL).mkdir(parents=True, exist_ok=True)
        (sd / CLOUD_REL / "session.ini").write_text(
            f"[session]\ngame = {fx.key}\nunit = {plan.unit}\nstarted = {SESSION_STARTED}\n",
            encoding="utf-8", newline="\n")
        plan.markers.append(f"NL_CLOUD_UPLOAD {plan.unit} ok")
        return plan

    plan.debug_lines.append(f"cloud_sync = {fx.key}")
    if step == "pull":
        plan.markers += [f"NL_CLOUD {plan.unit} pull ok", f"NL_CLOUD_SYNC_DONE {fx.key}"]
    elif step == "conflict":
        plan.debug_lines.append("cloud_conflict = cloud")
        plan.markers += [f"NL_CLOUD_CONFLICT {plan.unit}", f"NL_CLOUD_CHOICE {plan.unit} cloud",
                         f"NL_CLOUD {plan.unit} pull ok"]
    elif step == "refresh":
        plan.markers += ["NL_CLOUD_REFRESH ok", f"NL_CLOUD {plan.unit} push ok"]
    elif step == "offline":
        plan.markers += [f"NL_CLOUD {plan.unit} skip offline", f"NL_CLOUD_SYNC_DONE {fx.key}"]
    return plan


def leaked_secrets(log: str, fake: cloud_fake.FakeDrive) -> list[str]:
    """Tokens, Gerätecode und Client-Secret dürfen nie im Log stehen (nur der Anfang wird gemeldet)."""
    found = [s for s in (*fake.secrets(), CLIENT_SECRET) if s and s in log]
    return [f"Geheimnis im Log: {s[:8]}…" for s in found]


def post_check(plan: CloudPlan, user_dir: Path, log: str, read_sd: Callable[[str], bytes]) -> list[str]:
    problems = leaked_secrets(log, plan.fake)
    dump = plan.fake.state_dump()
    onedrive = isinstance(plan.fake, cloud_fake.FakeOneDrive)
    if plan.step == "unlink":
        # Drive widerruft genau einmal (danach ist der Fake abgemeldet), OneDrive hat keinen Widerruf.
        want_revokes = 0 if onedrive else 1
        if dump["revokes"] != want_revokes:
            problems.append(f"Fake: {dump['revokes']} Widerrufe statt {want_revokes}")
        elif not onedrive and dump["linked"]:
            problems.append("Fake: nach dem Widerruf noch angemeldet")
        return problems
    if not dump["linked"]:
        problems.append("Fake: keine Anmeldung")
    # Frische Tokens bleiben je Sitzung im Speicher: Refresh nur nach einem 401 (refresh-Schritt).
    want_refreshes = 1 if plan.step == "refresh" else 0
    if dump["refreshes"] != want_refreshes:
        problems.append(f"Fake: {dump['refreshes']} Refreshes statt {want_refreshes}")
    if plan.step == "link":
        return problems

    fx = save_smoke.fixture("gb", user_dir)
    if plan.step in ("push", "refresh", "syncall", "switch"):
        data = plan.fake.file_bytes(plan.unit)
        if data is None:
            return problems + [f"Fake: {plan.unit}.zip fehlt"]
        meta = cloud_fake.zip_meta(data)
        want = cloud_fake.content_hash({fx.file_name: save_smoke.BYTES_A})
        if meta.get("unit") != plan.unit or meta.get("hash") != want:
            problems.append(f"Fake: {plan.unit}.zip hat unit/hash {meta.get('unit')}/{meta.get('hash')}")
        got_hash, got_unit = cloud_hash(plan.fake, plan.unit + ".zip")
        if got_hash != want or got_unit != plan.unit:
            problems.append(f"Fake: Meta passt nicht: {got_hash}/{got_unit}")
        if plan.step == "syncall":
            nand = save_smoke.fixture("nand", user_dir)
            p = nand.host_dir / nand.file_name
            if not p.is_file() or p.read_bytes() != save_smoke.BYTES_B:
                problems.append(f"{nand.launcher_dir}/{nand.file_name} hat nicht die Cloud-Bytes")
    elif plan.step in ("pull", "conflict"):
        try:
            got = read_sd(f"{fx.sd_rel}/{fx.file_name}")
        except (OSError, KeyError, ValueError) as e:
            return problems + [f"{fx.sd_rel}/{fx.file_name} nicht im SD-Abbild ({e})"]
        if got != save_smoke.BYTES_B:
            problems.append(f"{fx.sd_rel}/{fx.file_name} hat nicht die Cloud-Bytes")
    elif plan.step == "offline" and dump["uploads"] != 0:
        problems.append("Fake: Upload trotz offline")
    return problems


def cleanup(user_dir: Path) -> None:
    sd = sd_sync_dir(user_dir)
    shutil.rmtree(sd / CLOUD_REL, ignore_errors=True)
    shutil.rmtree(sd / "saves_smoke", ignore_errors=True)
    shutil.rmtree(sd / save_smoke.SAVES_REL, ignore_errors=True)  # Sicherungen vor dem Pull
    nand = save_smoke.fixture("nand", user_dir)
    (nand.host_dir / nand.file_name).unlink(missing_ok=True)
