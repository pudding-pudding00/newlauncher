"""Hilfen für den Wii-Build mit devkitPro (unter Windows über dessen MSYS2-Shell)."""

from pathlib import Path

DEVKITPRO_BASH = r"C:\devkitPro\msys2\usr\bin\bash.exe"
WII_BUILD = "cmake --preset wii -DNL_TLS={tls}{extra} && cmake --build --preset wii"
# Unter Windows vorab entpackte Mbed-TLS-Quelle (tools/mbedtls_src.py); $(pwd -P) läuft erst in der MSYS-Shell (geerbtes $PWD wäre ein Windows-Pfad).
WIN_MBEDTLS_SRC = " -DFETCHCONTENT_SOURCE_DIR_MBEDTLS=$(pwd -P)/build/mbedtls/mbedtls-4.1.1"


def wii_build_invocation(repo_root: Path, platform: str, tls: bool = True) -> tuple[list[str], dict[str, str]]:
    """Befehl und zusätzliche Umgebungsvariablen für den Wii-Build.

    Unter Windows läuft der Build in der Login-Shell von devkitPro. CHERE_INVOKING=1
    hält die Shell im aktuellen Ordner (cwd = repo_root), statt ins MSYS-Home zu wechseln.
    NL_TLS steht immer explizit im Befehl, weil CMake den Wert sonst aus dem Cache des letzten
    Builds nimmt (M11, Spec v2 4.1).
    """
    if platform == "win32":
        build = WII_BUILD.format(tls="ON" if tls else "OFF", extra=WIN_MBEDTLS_SRC if tls else "")
        return [DEVKITPRO_BASH, "-lc", build], {"CHERE_INVOKING": "1"}
    return ["sh", "-c", WII_BUILD.format(tls="ON" if tls else "OFF", extra="")], {}
