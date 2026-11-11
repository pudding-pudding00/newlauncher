"""Lizenzhinweise fürs Release (Spec v2 3.4): LICENSE.txt und THIRD_PARTY.txt unter apps/newlauncher/."""

from __future__ import annotations

from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]


def license_text(root: Path) -> str:
    return (root / "LICENSE").read_text(encoding="utf-8")


def third_party_text(root: Path) -> str:
    """THIRD_PARTY.md, danach jeder Text aus LICENSES/ (nach Dateiname sortiert) mit Trennkopf."""
    parts = [(root / "THIRD_PARTY.md").read_text(encoding="utf-8").rstrip("\n") + "\n"]
    rule = "=" * 72
    for path in sorted((root / "LICENSES").glob("*.txt"), key=lambda p: p.name.lower()):
        parts.append(f"\n{rule}\nLICENSES/{path.name}\n{rule}\n\n")
        parts.append(path.read_text(encoding="utf-8").rstrip("\n") + "\n")
    return "".join(parts)


def write_notices(root: Path, app_dir: Path) -> list[Path]:
    """Schreibt LICENSE.txt und THIRD_PARTY.txt nach app_dir (LF-Zeilenenden) und gibt die Pfade zurück."""
    out = []
    for name, text in (("LICENSE.txt", license_text(root)), ("THIRD_PARTY.txt", third_party_text(root))):
        path = app_dir / name
        path.write_text(text, encoding="utf-8", newline="\n")
        out.append(path)
    return out
