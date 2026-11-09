"""Reduziert M PLUS Rounded 1c auf Latin-1 plus „…" (Spec 3.7) und legt sie unter assets/fonts/ ab.

Einmaliges Dev-Werkzeug, nicht Teil des Builds. Braucht fonttools (`pip install fonttools`).
Quelle: github.com/google/fonts, Ordner ofl/mplusrounded1c (SIL Open Font License 1.1).

    python tools/subset_fonts.py <Regular.ttf> <Medium.ttf>
"""

import sys
from pathlib import Path

from fontTools import subset

REPO_ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = REPO_ROOT / "assets/fonts"
UNICODES = "U+0020-007E,U+00A0-00FF,U+2026"


def subset_font(src: Path, dst: Path) -> None:
    subset.main([str(src), f"--unicodes={UNICODES}", "--layout-features=", "--no-hinting",
                 "--drop-tables+=GSUB,GPOS,GDEF", f"--output-file={dst}"])


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__)
        return 2
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for src, weight in zip(argv, ("Regular", "Medium")):
        dst = OUT_DIR / f"MPLUSRounded1c-{weight}-latin1.ttf"
        subset_font(Path(src), dst)
        print(f"{dst.name}: {dst.stat().st_size} Bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
