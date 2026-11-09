"""Hilfen für den Wii-Build mit devkitPro (unter Windows über dessen MSYS2-Shell)."""

from pathlib import Path

DEVKITPRO_BASH = r"C:\devkitPro\msys2\usr\bin\bash.exe"
WII_BUILD = "cmake --preset wii && cmake --build --preset wii"


def wii_build_invocation(repo_root: Path, platform: str) -> tuple[list[str], dict[str, str]]:
    """Befehl und zusätzliche Umgebungsvariablen für den Wii-Build.

    Unter Windows läuft der Build in der Login-Shell von devkitPro. CHERE_INVOKING=1
    hält die Shell im aktuellen Ordner (cwd = repo_root), statt ins MSYS-Home zu wechseln.
    """
    if platform == "win32":
        return [DEVKITPRO_BASH, "-lc", WII_BUILD], {"CHERE_INVOKING": "1"}
    return ["sh", "-c", WII_BUILD], {}
