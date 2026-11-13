"""Hilfen für den Wii-Build mit devkitPro (unter Windows über dessen MSYS2-Shell)."""

from pathlib import Path

DEVKITPRO_BASH = r"C:\devkitPro\msys2\usr\bin\bash.exe"
# Eigener Build-Ordner je Variante: ein ON/OFF-Wechsel im selben Ordner würde fast alles neu übersetzen.
# --parallel $(nproc): Unix Makefiles bauen sonst seriell (16 Kerne → ~4x schneller).
WII_BUILD = "cmake --preset wii -DNL_TLS=ON -DNL_FS_NTFS=ON{extra} && cmake --build --preset wii --parallel $(nproc)"
WII_BUILD_NOTLS = "cmake --preset wii -B build/wii-notls -DNL_TLS=OFF -DNL_FS_NTFS=OFF && cmake --build build/wii-notls --parallel $(nproc)"
# Unter Windows vorab entpackte Mbed-TLS-Quelle (tools/mbedtls_src.py); $(pwd -P) läuft erst in der MSYS-Shell (geerbtes $PWD wäre ein Windows-Pfad).
WIN_MBEDTLS_SRC = " -DFETCHCONTENT_SOURCE_DIR_MBEDTLS=$(pwd -P)/build/mbedtls/mbedtls-4.1.1"


def wii_build_invocation(repo_root: Path, platform: str, tls: bool = True) -> tuple[list[str], dict[str, str]]:
    """Befehl und zusätzliche Umgebungsvariablen für den Wii-Build.

    Unter Windows läuft der Build in der Login-Shell von devkitPro. CHERE_INVOKING=1
    hält die Shell im aktuellen Ordner (cwd = repo_root), statt ins MSYS-Home zu wechseln.
    NL_TLS und NL_FS_NTFS (M16, gleiche Variante: ON/ON oder OFF/OFF) stehen immer explizit im Befehl, weil CMake den Wert sonst aus dem Cache des letzten
    Builds nimmt (M11, Spec v2 4.1).
    """
    if platform == "win32":
        build = WII_BUILD.format(extra=WIN_MBEDTLS_SRC) if tls else WII_BUILD_NOTLS
        return [DEVKITPRO_BASH, "-lc", build], {"CHERE_INVOKING": "1"}
    return ["sh", "-c", WII_BUILD.format(extra="") if tls else WII_BUILD_NOTLS], {}
