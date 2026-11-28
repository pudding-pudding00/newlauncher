"""Cloud-Smokes gegen die Fakes (M27 Drive, M28 OneDrive, M29 Dropbox):
--cloud-fake drive|onedrive|dropbox --cloud-step <schritt>.

Jeder Schritt meldet sich zuerst an ([debug] cloud_device_auto = 1): Drive und OneDrive per Gerätecode (der Fake
genehmigt sofort), Dropbox über die Kopplungsseite (Pairer: Seite über 127.0.0.1:4301 holen, dem Link zum Fake
folgen, den Code zurück posten – wie ein Handy). Die Tokens auf der SD sind an das Gerät gebunden
und lassen sich nicht vorab hinlegen. Ohne cloud_sync folgt
auf die Anmeldung der Wechsel-Abgleich über alle Spiele (M28). Danach:
  link     – nur anmelden: NL_CLOUD_LINKED <anbieter>
  push     – session.ini liegt da (Spiel lief), GB-Spielstand A → Abgleich nach der Anmeldung lädt hoch, Fake hat A
  pull     – Cloud hat B, lokal nichts → cloud_sync holt B auf die SD
  conflict – lokal A, Cloud B, keine Basis → Konflikt, cloud_conflict = cloud → B auf der SD
  refresh  – erste Datei-Anfrage bekommt 401 → Refresh → Upload klappt: NL_CLOUD_REFRESH ok
  offline  – nach der Anmeldung schließt der Fake den Listener (Verbindung abgewiesen) → NL_CLOUD <einheit> skip offline
  syncall  – GB nur lokal (A), Galaxy-NAND nur in der Cloud (B) → NL_CLOUD_SYNCALL pulled=1 pushed=1
  switch   – GB lokal A, Cloud B → Wechsel-Dialog, cloud_switch = wii → NL_CLOUD_SWITCH uploaded=1 conflict=0
  unlink   – anmelden, dann cloud_unlink = 1 → Drive/Dropbox: NL_CLOUD_UNLINKED revoke=ok, OneDrive: revoke=none
  badcode  – nur Dropbox: erst ein falscher Code → NL_CLOUD_LINK badcode, dann der richtige
  pairtwice – nur Dropbox: zweiter POST auf die Kopplungsseite → 410, NL_PAIR reject
Der Galaxy-NAND-Spielstand (save_smoke „nand“) wird vor jedem Lauf geleert, damit er die Zählung nicht verfälscht.
Nach jedem Lauf: Kein Token, kein Gerätecode und kein Client-Secret im Log.
"""

from __future__ import annotations

import html
import io
import re
import shutil
import ssl
import threading
import time
import urllib.error
import urllib.request
import zipfile
import zlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

import cloud_fake
import save_smoke
from dolphin import sd_sync_dir

STEPS = ("link", "push", "pull", "conflict", "refresh", "offline", "syncall", "switch", "unlink", "badcode",
         "pairtwice")
PROVIDERS = ("drive", "onedrive", "dropbox")
DROPBOX_ONLY = ("badcode", "pairtwice")

CLIENT_ID = "fake-client.apps.googleusercontent.com"
OD_CLIENT_ID = "00000000-0000-0000-0000-00000fake0d1"
NAND_UNIT = "wii-RMGP"
_DRIVE_MOCK_FIXTURE = "GOCSPX-fake-secret-not-real"
CLIENT_SECRET = _DRIVE_MOCK_FIXTURE
DBX_APP_KEY = "fakeappkey0dbx1"
PAIR_TOKEN = "5e0c1a2b3d4f60718293a4b5c6d7e8f9"  # [debug] cloud_pair_token: feste URL für den Smoke
PAIR_PORT = 4301
WRONG_CODE = "WRONG-code_0"
PAIRTWICE_DELAY_S = 3.0  # Code-Tausch verzögert: die Seite lebt beim zweiten POST noch
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
    if step in DROPBOX_ONLY and provider != "dropbox":
        raise ValueError(f"Cloud-Schritt {step} gibt es nur mit dropbox")
    kwargs = dict(approve="auto", expire_access_once=step == "refresh", offline_after_link=step == "offline")
    if provider == "dropbox":
        return cloud_fake.FakeDropbox(token_delay_s=PAIRTWICE_DELAY_S if step == "pairtwice" else 0.0, **kwargs)
    cls = cloud_fake.FakeOneDrive if provider == "onedrive" else cloud_fake.FakeDrive
    return cls(**kwargs)


def provider_of(fake: cloud_fake.FakeDrive) -> str:
    if isinstance(fake, cloud_fake.FakeDropbox):
        return "dropbox"
    return "onedrive" if isinstance(fake, cloud_fake.FakeOneDrive) else "drive"


def cloud_hash(fake: cloud_fake.FakeDrive, name: str) -> tuple[str | None, str | None]:
    """(hash, unit) der Meta im Fake: Drive aus appProperties, OneDrive aus der description (ohne Einheit),
    Dropbox (ohne Metadaten-Feld) aus newlauncher-save.ini im ZIP."""
    if isinstance(fake, cloud_fake.FakeDropbox):
        data = fake.file_bytes(name.removesuffix(".zip"))
        if data is None:
            return None, None
        meta = cloud_fake.zip_meta(data)
        return meta.get("hash"), meta.get("unit")
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
    pair: bool = False                                     # Dropbox: Pairer spielt das Handy
    pair_problems: list[str] = field(default_factory=list)  # vom Pairer, post_check meldet sie


def prepare(step: str, fake: cloud_fake.FakeDrive, user_dir: Path) -> CloudPlan:
    """Schreibt Konfiguration, Spielstand und session.ini; liefert Debug-Zeilen und Marker."""
    sd = sd_sync_dir(user_dir)
    shutil.rmtree(sd / CLOUD_REL, ignore_errors=True)  # Tokens und Basis eines früheren Laufs
    plan = CloudPlan(step, fake)
    provider = provider_of(fake)
    # Beide IDs stehen drin (wie bei einem Nutzer, der beide eingerichtet hat); aktiv ist nur provider.
    plan.config_append = (f"[cloud]\nprovider = {provider}\ndrive_client_id = {CLIENT_ID}\n"
                          f"drive_client_secret = {_DRIVE_MOCK_FIXTURE}\nonedrive_client_id = {OD_CLIENT_ID}\n"
                          f"dropbox_app_key = {DBX_APP_KEY}\n")
    plan.debug_lines.append("cloud_device_auto = 1")
    plan.markers.append(f"NL_CLOUD_LINKED {provider}")
    if provider == "dropbox":
        plan.pair = True
        plan.debug_lines.append(f"cloud_pair_token = {PAIR_TOKEN}")
        plan.markers += [f"NL_PAIR listen {PAIR_PORT}", "NL_PAIR ok"]
    nand = save_smoke.fixture("nand", user_dir)
    (nand.host_dir / nand.file_name).unlink(missing_ok=True)
    if step in ("link", "badcode", "pairtwice"):
        plan.markers += {"badcode": ["NL_CLOUD_LINK badcode"], "pairtwice": ["NL_PAIR reject"]}.get(step, [])
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
    """Tokens, device code, and mock client credential must never appear in the log."""
    extra = (PAIR_TOKEN, WRONG_CODE) if isinstance(fake, cloud_fake.FakeDropbox) else ()
    count = sum(1 for s in (*fake.secrets(), _DRIVE_MOCK_FIXTURE, *extra) if s and s in log)
    return [f"secret leaked in log (#{i + 1})" for i in range(count)]


def post_check(plan: CloudPlan, user_dir: Path, log: str, read_sd: Callable[[str], bytes]) -> list[str]:
    problems = leaked_secrets(log, plan.fake) + list(plan.pair_problems)
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
    if plan.step in ("link", "badcode", "pairtwice"):
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


_HREF = re.compile(r'<a class="b" href="([^"]+)"')


def authorize_link(page: str) -> str:
    """Der Freigabe-Link auf der Kopplungsseite (core/pairing_http, HTML-maskiert)."""
    m = _HREF.search(page)
    if m is None:
        raise ValueError("kein Freigabe-Link auf der Kopplungsseite")
    return html.unescape(m.group(1))


class Pairer(threading.Thread):
    """Spielt das Handy (M29): wartet auf NL_PAIR listen, holt die Kopplungsseite, folgt dem Link zum Fake, liest den
    Code ab und postet ihn zurück. badcode postet erst WRONG_CODE und wartet auf NL_CLOUD_LINK badcode, pairtwice
    postet den Code zweimal (zweiter POST → 410). Fehler landen in plan.pair_problems (post_check meldet sie)."""

    def __init__(self, plan: CloudPlan, log: Path, cafile: Path | None, host: str = "127.0.0.1",
                 port: int = PAIR_PORT, timeout_s: float = 120.0):
        super().__init__(daemon=True)
        self.plan, self.log, self.cafile = plan, log, cafile
        self.base = f"http://{host}:{port}/{PAIR_TOKEN}"
        self.deadline = time.monotonic() + timeout_s

    def run(self) -> None:
        try:
            self._pair()
        except Exception as e:  # noqa: BLE001 - jeder Fehler ist ein Smoke-Befund
            self.plan.pair_problems.append(f"Kopplung: {e}")

    def _wait_log(self, marker: str, count: int = 1) -> None:
        while time.monotonic() < self.deadline:
            try:
                if self.log.read_text(encoding="utf-8", errors="replace").count(marker) >= count:
                    return
            except OSError:
                pass
            time.sleep(0.1)
        raise TimeoutError(f"{marker} nicht im Log")

    def _request(self, url: str, data: bytes | None = None, context: ssl.SSLContext | None = None) -> tuple[int, str]:
        req = urllib.request.Request(url, data=data, method="POST" if data is not None else "GET")
        if data is not None:
            req.add_header("Content-Type", "application/x-www-form-urlencoded")
        last: Exception | None = None
        for _ in range(20):  # Dolphin nimmt die erste Verbindung manchmal erst im zweiten Anlauf an
            try:
                with urllib.request.urlopen(req, timeout=10, context=context) as r:
                    return r.status, r.read().decode("utf-8", "replace")
            except urllib.error.HTTPError as e:
                return e.code, e.read().decode("utf-8", "replace")
            except (urllib.error.URLError, ConnectionError, TimeoutError) as e:
                last = e
                if time.monotonic() >= self.deadline:
                    break
                time.sleep(0.5)
        raise ConnectionError(f"pairing request failed: {type(last).__name__}")

    def _post(self, code: str) -> int:
        return self._request(self.base, ("code=" + code).encode())[0]

    def _post_expect(self, code: str, want: int) -> None:
        status = self._post(code)
        if status == 410 and want == 200:  # nach badcode: rearm() kommt mit dem nächsten Bild auf der Wii
            for _ in range(20):
                time.sleep(0.25)
                status = self._post(code)
                if status != 410:
                    break
        if status != want:
            raise RuntimeError(f"POST auf die Kopplungsseite: {status} statt {want}")

    def _pair(self) -> None:
        try:
            self._wait_log(f"NL_PAIR listen {PAIR_PORT}")
        except TimeoutError as e:
            raise RuntimeError(str(e)) from None
        status, page = self._request(self.base)
        if status != 200:
            raise RuntimeError(f"Kopplungsseite: {status}")
        link = authorize_link(page)
        ctx = None
        if link.startswith("https:"):
            ctx = ssl.create_default_context(cafile=str(self.cafile))
            ctx.minimum_version = ssl.TLSVersion.TLSv1_2
        status, page = self._request(link, context=ctx)
        if status != 200:
            raise RuntimeError(f"Freigabeseite des Fakes: {status}")
        code = cloud_fake.dropbox_code_from_page(page)
        if self.plan.step == "badcode":
            self._post_expect(WRONG_CODE, 200)
            self._wait_log("NL_CLOUD_LINK badcode")
        self._post_expect(code, 200)
        if self.plan.step == "pairtwice":
            self._post_expect(code, 410)


def cleanup(user_dir: Path) -> None:
    sd = sd_sync_dir(user_dir)
    shutil.rmtree(sd / CLOUD_REL, ignore_errors=True)
    shutil.rmtree(sd / "saves_smoke", ignore_errors=True)
    shutil.rmtree(sd / save_smoke.SAVES_REL, ignore_errors=True)  # Sicherungen vor dem Pull
    nand = save_smoke.fixture("nand", user_dir)
    (nand.host_dir / nand.file_name).unlink(missing_ok=True)
