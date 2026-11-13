"""Banner-Fixtures für Tests und Smokes (M18). Alles aus eigenen Daten, keine fremden Banner-Dateien.

Formatbeschreibungen stehen an den Parsern in src/core (gc_banner, gc_fst, später u8_archive, tpl, brlyt, …).

    python tools/make_banner_fixtures.py <ausgabeordner>
"""

import functools
import hashlib
import struct
import sys
from pathlib import Path

BANNER_W, BANNER_H = 96, 32


def rgb5a3_pixel(r: int, g: int, b: int) -> int:
    """Opakes RGB555-Pixel (Bit 15 gesetzt) aus 8-Bit-Kanälen."""
    return 0x8000 | (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3)


def tile_rgb5a3(pixels: list[int], w: int, h: int) -> bytes:
    """Zeilenweise Pixel → GX-Kacheln 4×4, 2 Byte big endian je Pixel."""
    out = bytearray()
    for ty in range(0, h, 4):
        for tx in range(0, w, 4):
            for y in range(4):
                for x in range(4):
                    out += struct.pack(">H", pixels[(ty + y) * w + tx + x])
    return bytes(out)


def banner_image() -> bytes:
    """Waagerechter Verlauf blau → türkis mit hellem Rahmen, damit man die Lage auf dem Bildschirm erkennt."""
    px = []
    for y in range(BANNER_H):
        for x in range(BANNER_W):
            if x in (0, BANNER_W - 1) or y in (0, BANNER_H - 1):
                px.append(rgb5a3_pixel(240, 240, 240))
            else:
                px.append(rgb5a3_pixel(20, 60 + x * 150 // BANNER_W, 200))
    return tile_rgb5a3(px, BANNER_W, BANNER_H)


def gc_bnr(blocks: list[tuple[str, str, str, str, str]], bnr2: bool = True) -> bytes:
    """BNR1 (ein Block) oder BNR2 (sechs Blöcke EN, DE, FR, ES, IT, NL); Texte Latin-1."""
    count = 6 if bnr2 else 1
    data = bytearray(0x1820 + count * 0x140)
    data[0:4] = b"BNR2" if bnr2 else b"BNR1"
    data[0x20:0x1820] = banner_image()
    for i in range(count):
        st, sm, t, m, d = blocks[i] if i < len(blocks) else blocks[0]
        base = 0x1820 + i * 0x140
        for off, text, cap in ((0, st, 0x20), (0x20, sm, 0x20), (0x40, t, 0x40), (0x80, m, 0x40), (0xC0, d, 0x80)):
            raw = text.encode("latin-1")[:cap]
            data[base + off:base + off + len(raw)] = raw
    return bytes(data)


def fst(files: list[tuple[str, bytes]], data_start: int) -> tuple[bytes, list[int]]:
    """FST nur mit Dateien in der Wurzel; liefert FST und die Offsets der Dateien (32-Byte-ausgerichtet)."""
    names = bytearray()
    entries = [(0x01000000, 0, len(files) + 1)]
    offsets = []
    at = data_start
    for name, payload in files:
        entries.append((len(names), at, len(payload)))
        offsets.append(at)
        names += name.encode("ascii") + b"\0"
        at = (at + len(payload) + 31) & ~31
    table = b"".join(struct.pack(">III", *e) for e in entries)
    return table + bytes(names), offsets


def gc_iso(header: bytes, files: list[tuple[str, bytes]]) -> bytes:
    """GC-Abbild: Disc-Kopf (mindestens 0x440 Byte), FST bei 0x2000, Dateien dahinter."""
    fst_at = 0x2000
    probe, _ = fst(files, 0)
    data_start = (fst_at + len(probe) + 31) & ~31
    table, offsets = fst(files, data_start)
    end = offsets[-1] + len(files[-1][1]) if files else data_start
    image = bytearray(max(end, fst_at + len(table)))
    image[:len(header)] = header
    struct.pack_into(">II", image, 0x424, fst_at, len(table))
    image[fst_at:fst_at + len(table)] = table
    for (_, payload), off in zip(files, offsets):
        image[off:off + len(payload)] = payload
    return bytes(image)


def u8_archive(tree: dict) -> bytes:
    """U8-Archiv aus einem verschachtelten dict (Name -> bytes oder dict); Daten 32-Byte-ausgerichtet."""
    nodes = [[0x01000000, 0, 0]]
    names = bytearray(b"\0")
    files = []

    def add(d: dict, parent: int) -> None:
        for name, value in d.items():
            off = len(names)
            names.extend(name.encode("ascii") + b"\0")
            idx = len(nodes)
            if isinstance(value, dict):
                nodes.append([0x01000000 | off, parent, 0])
                add(value, idx)
                nodes[idx][2] = len(nodes)
            else:
                nodes.append([off, 0, len(value)])
                files.append((idx, value))

    add(tree, 0)
    nodes[0][2] = len(nodes)
    header = len(nodes) * 12 + len(names)
    at = (0x20 + header + 31) & ~31
    data_start = at
    for idx, payload in files:
        nodes[idx][1] = at
        at = (at + len(payload) + 31) & ~31
    out = bytearray(at)
    struct.pack_into(">IIII", out, 0, 0x55AA382D, 0x20, header, data_start)
    for i, n in enumerate(nodes):
        struct.pack_into(">III", out, 0x20 + i * 12, *n)
    out[0x20 + len(nodes) * 12:0x20 + header] = names
    for idx, payload in files:
        out[nodes[idx][1]:nodes[idx][1] + len(payload)] = payload
    return bytes(out)


def imd5(payload: bytes) -> bytes:
    """IMD5-Hülle: Magic, Größe, 8 Byte frei, MD5 der Nutzdaten, dann die Nutzdaten."""
    return b"IMD5" + struct.pack(">I", len(payload)) + bytes(8) + hashlib.md5(payload).digest() + payload


# GX-Formate: Kachelbreite, Kachelhöhe, Bytes je Kachel (Wii-Banner, M18).
TEX_BYTES = {0: (8, 8, 32), 1: (8, 4, 32), 2: (8, 4, 32), 3: (4, 4, 32), 4: (4, 4, 32), 5: (4, 4, 32),
             6: (4, 4, 64), 8: (8, 8, 32), 9: (8, 4, 32), 10: (4, 4, 32), 14: (8, 8, 32)}
_BITS = {0: 4, 1: 8, 2: 8, 3: 16, 4: 16, 5: 16, 8: 4, 9: 8, 10: 16}


def tex_bytes(fmt: int, w: int, h: int) -> int:
    bw, bh, tb = TEX_BYTES[fmt]
    return ((w + bw - 1) // bw) * ((h + bh - 1) // bh) * tb


def _rgb565(r: int, g: int, b: int) -> int:
    return (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)


def _texel(fmt: int, v) -> int:
    if fmt in (8, 9, 10):
        return v
    r, g, b, a = v
    return {0: r >> 4, 1: r, 2: (a >> 4) << 4 | (r >> 4), 3: a << 8 | r, 4: _rgb565(r, g, b),
            5: rgb5a3_pixel(r, g, b) if a == 255 else (a >> 5) << 12 | (r >> 4) << 8 | (g >> 4) << 4 | (b >> 4)}[fmt]


def _cmpr_block(colors: list) -> bytes:
    """Ein 4×4-Unterblock: Durchschnittsfarbe, alle Indizes 0; bei Alpha < 128 überall transparent."""
    if all(c[3] < 128 for c in colors):
        return struct.pack(">HH", 0, 0) + bytes([0xFF] * 4)
    n = len(colors)
    avg = tuple(sum(c[i] for c in colors) // n for i in range(3))
    c = _rgb565(*avg)
    return struct.pack(">HH", c, c) + bytes(4)


def encode_tex(fmt: int, w: int, h: int, px) -> bytes:
    """Bild im GX-Kachelformat. px(x, y) liefert (r, g, b, a) oder bei C4/C8/C14X2 den Palettenindex."""
    bw, bh, _ = TEX_BYTES[fmt]
    blank = 0 if fmt in (8, 9, 10) else (0, 0, 0, 0)

    def get(x, y):
        return px(x, y) if x < w and y < h else blank

    out = bytearray()
    for ty in range(0, h, bh):
        for tx in range(0, w, bw):
            if fmt == 14:
                for sy, sx in ((0, 0), (0, 4), (4, 0), (4, 4)):
                    out += _cmpr_block([get(tx + sx + x, ty + sy + y) for y in range(4) for x in range(4)])
                continue
            pixels = [get(tx + x, ty + y) for y in range(bh) for x in range(bw)]
            if fmt == 6:
                out += bytes(v for p in pixels for v in (p[3], p[0]))
                out += bytes(v for p in pixels for v in (p[1], p[2]))
                continue
            bits = _BITS[fmt]
            vals = [_texel(fmt, p) for p in pixels]
            if bits == 4:
                out += bytes(vals[i] << 4 | vals[i + 1] for i in range(0, len(vals), 2))
            elif bits == 8:
                out += bytes(vals)
            else:
                out += b"".join(struct.pack(">H", v) for v in vals)
    return bytes(out)


def tpl(images: list) -> bytes:
    """TPL aus (fmt, w, h, px, palette); palette: Liste von RGB5A3-Werten für C4/C8/C14X2, sonst None."""
    out = bytearray(0x0C + 8 * len(images))
    struct.pack_into(">III", out, 0, 0x0020AF30, len(images), 0x0C)

    def align():
        out.extend(bytes(-len(out) % 32))

    for i, (fmt, w, h, px, palette) in enumerate(images):
        pal_hdr = 0
        if palette is not None:
            align()
            pal_hdr = len(out)
            out.extend(bytes(0x0C))
            align()
            struct.pack_into(">HxxII", out, pal_hdr, len(palette), 2, len(out))
            out.extend(b"".join(struct.pack(">H", v) for v in palette))
        align()
        hdr = len(out)
        out.extend(bytes(0x24))
        align()
        struct.pack_into(">HHII", out, hdr, h, w, fmt, len(out))
        out.extend(encode_tex(fmt, w, h, px))
        struct.pack_into(">II", out, 0x0C + 8 * i, hdr, pal_hdr)
    return bytes(out)


def sample_bnr() -> bytes:
    en = ("Fixture", "NL", "Fixture Banner", "newlauncher", "Test banner made by make_banner_fixtures.py")
    de = ("Fixture", "NL", "Fixture-Banner", "newlauncher", "Testbanner aus make_banner_fixtures.py")
    return gc_bnr([en, de, en, en, en, en])


def pane(kind: str, name: str, x: float, y: float, w: float, h: float, *, rot: float = 0.0, alpha: int = 255,
         flags: int = 0x03, origin: int = 4, material: int = 0, vtx=None, uvs=None, children=None) -> dict:
    """Pane-Beschreibung für brlyt(); kind pan1 oder pic1, Drehung um z in Grad."""
    return {"kind": kind, "name": name, "f": (x, y, 0.0, 0.0, 0.0, rot, 1.0, 1.0, w, h), "alpha": alpha,
            "flags": flags, "origin": origin, "material": material, "vtx": vtx or [(255, 255, 255, 255)] * 4,
            "uvs": uvs or [], "children": children or []}


def _section(magic: str, body: bytes) -> bytes:
    body += bytes(-(8 + len(body)) % 4)
    return magic.encode("ascii") + struct.pack(">I", 8 + len(body)) + body


def _fixed(text: str, n: int) -> bytes:
    raw = text.encode("ascii")[:n]
    return raw + bytes(n - len(raw))


def _pane_sections(p: dict) -> list:
    body = bytes([p["flags"], p["origin"], p["alpha"], 0]) + _fixed(p["name"], 16) + bytes(8)
    body += struct.pack(">10f", *p["f"])
    if p["kind"] == "pic1":
        body += b"".join(bytes(c) for c in p["vtx"])
        body += struct.pack(">HBx", p["material"], len(p["uvs"]))
        body += b"".join(struct.pack(">8f", *uv) for uv in p["uvs"])
    out = [_section(p["kind"], body)]
    if p["children"]:
        out.append(_section("pas1", b""))
        for c in p["children"]:
            out += _pane_sections(c)
        out.append(_section("pae1", b""))
    return out


def brlyt(w: float, h: float, textures: list, materials: list, roots: list) -> bytes:
    """BRLYT (Version 0x0A) aus lyt1, txl1, mat1 und Pane-Baum.

    materials: (Name, Texturindex oder None, Materialfarbe (r, g, b, a) oder None)."""
    secs = [_section("lyt1", bytes([1, 0, 0, 0]) + struct.pack(">ff", w, h))]
    names = b"".join(t.encode("ascii") + bytes(1) for t in textures)
    table, at = bytearray(), len(textures) * 8
    for t in textures:
        table += struct.pack(">II", at, 0)
        at += len(t) + 1
    secs.append(_section("txl1", struct.pack(">Hxx", len(textures)) + bytes(table) + names))
    mats = bytearray(struct.pack(">Hxx", len(materials)) + bytes(4 * len(materials)))
    for i, (name, tex, color) in enumerate(materials):
        struct.pack_into(">I", mats, 4 + 4 * i, 8 + len(mats))
        flags = (1 if tex is not None else 0) | (1 << 27 if color is not None else 0)
        mats += _fixed(name, 20) + bytes(24) + bytes(16) + struct.pack(">I", flags)
        if tex is not None:
            mats += struct.pack(">Hxx", tex)
        if color is not None:
            mats += bytes(color)
    secs.append(_section("mat1", bytes(mats)))
    for r in roots:
        secs += _pane_sections(r)
    body = b"".join(secs)
    return b"RLYT" + bytes([0xFE, 0xFF, 0x00, 0x0A]) + struct.pack(">IHH", 0x10 + len(body), 0x10, len(secs)) + body


def brlan(frames: int, loop: bool, patterns: list, entries: list) -> bytes:
    """BRLAN (Version 8) mit einem pai1-Abschnitt.

    entries: (Name, Material?, [(Tag-Magic, [(Index, Ziel, Stufe?, [(Bild, Wert, Steigung)])])])."""
    body = bytearray(struct.pack(">HBxHHI", frames, 1 if loop else 0, len(patterns), len(entries), 0))
    timg = len(body)
    body += bytes(4 * len(patterns))
    for i, name in enumerate(patterns):
        struct.pack_into(">I", body, timg + 4 * i, len(body) - timg)
        body += name.encode("ascii") + bytes(1)
    body += bytes(-len(body) % 4)
    table = len(body)
    struct.pack_into(">I", body, 8, 8 + table)
    body += bytes(4 * len(entries))
    for e, (name, material, tags) in enumerate(entries):
        base = len(body)
        struct.pack_into(">I", body, table + 4 * e, 8 + base)
        body += _fixed(name, 20) + struct.pack(">BBxx", len(tags), 1 if material else 0)
        tag_table = len(body)
        body += bytes(4 * len(tags))
        for t, (magic, infos) in enumerate(tags):
            tag_at = len(body)
            struct.pack_into(">I", body, tag_table + 4 * t, tag_at - base)
            body += magic.encode("ascii") + struct.pack(">Bxxx", len(infos))
            info_table = len(body)
            body += bytes(4 * len(infos))
            for i, (index, target, step, keys) in enumerate(infos):
                struct.pack_into(">I", body, info_table + 4 * i, len(body) - tag_at)
                body += struct.pack(">BBBxHxxI", index, target, 1 if step else 2, len(keys), 0x0C)
                for frame, value, slope in keys:
                    body += struct.pack(">fHxx", frame, int(value)) if step else struct.pack(">fff", frame, value, slope)
    section = _section("pai1", bytes(body))
    return b"RLAN" + bytes([0xFE, 0xFF, 0x00, 0x08]) + struct.pack(">IHH", 0x10 + len(section), 0x10, 1) + section


ADPCM_COEFS = (0, 0, 2048, 0, 4096, -2048, 3600, -1600, 3900, -1900, 1800, 0, 3000, -1000, 4000, -2000)


def _adpcm_sample(nib: int, exp: int, c1: int, c2: int, h1: int, h2: int) -> int:
    return max(-32768, min(32767, (nib * (1 << exp) * 2048 + c1 * h1 + c2 * h2 + 1024) >> 11))


def encode_adpcm(pcm: list) -> tuple[bytes, list]:
    """Eigener DSP-ADPCM-Encoder: je Frame alle 8 Koeffizientenpaare × 12 Skalen, kleinste Fehlerquadratsumme.

    Liefert (Frames, Rekonstruktion). Gleiches Verfahren wie tests/host/support/adpcm_encode.hpp."""
    frames, decoded, h1, h2 = bytearray(), [], 0, 0
    for at in range(0, len(pcm), 14):
        best = None
        for p in range(8):
            c1, c2 = ADPCM_COEFS[2 * p], ADPCM_COEFS[2 * p + 1]
            for exp in range(12):
                a, b, err, nibs, out = h1, h2, 0, [], []
                step = 1 << exp
                for k in range(14):
                    x = pcm[at + k] if at + k < len(pcm) else 0
                    pred = (c1 * a + c2 * b + 1024) >> 11
                    d = x - pred
                    n = (d + step // 2) // step if d >= 0 else -((-d + step // 2) // step)
                    n = max(-8, min(7, n))
                    v = _adpcm_sample(n, exp, c1, c2, a, b)
                    err += (x - v) * (x - v)
                    nibs.append(n)
                    out.append(v)
                    a, b = v, a
                if best is None or err < best[0]:
                    best = (err, p << 4 | exp, nibs, out)
        _, ps, nibs, out = best
        frames.append(ps)
        for k in range(0, 14, 2):
            frames.append((nibs[k] & 0xF) << 4 | (nibs[k + 1] & 0xF))
        decoded += out[:min(14, len(pcm) - at)]
        h1, h2 = out[13], out[12]
    return bytes(frames), decoded


def bns(channels: list, rate: int, loop: bool, loop_start: int) -> bytes:
    """BNS mit DSP-ADPCM: Kopf 0x20, INFO (Kanaltabelle, Kanalinfo, ADPCM-Info 0x30), DATA je Kanal am Stück."""
    count, n = len(channels), len(channels[0])
    table = 0x18
    ch_info = table + 4 * count
    adpcm = ch_info + 0x0C * count
    info = bytearray(adpcm + 0x30 * count)
    struct.pack_into(">BBBxHxxIII", info, 0, 0, 1 if loop else 0, count, rate, loop_start, n, table)
    data = bytearray()
    for c, pcm in enumerate(channels):
        frames, _ = encode_adpcm(pcm)
        struct.pack_into(">I", info, table + 4 * c, ch_info + 0x0C * c)
        struct.pack_into(">II", info, ch_info + 0x0C * c, len(data), adpcm + 0x30 * c)
        struct.pack_into(">16h", info, adpcm + 0x30 * c, *ADPCM_COEFS)
        data += frames
    info_sec = b"INFO" + struct.pack(">I", 8 + len(info)) + bytes(info)
    data_sec = b"DATA" + struct.pack(">I", 8 + len(data)) + bytes(data)
    head = b"BNS " + bytes([0xFE, 0xFF, 0x01, 0x00]) + struct.pack(
        ">IHHIIII", 0x20 + len(info_sec) + len(data_sec), 0x20, 2, 0x20, len(info_sec),
        0x20 + len(info_sec), len(data_sec))
    return head + info_sec + data_sec


@functools.lru_cache(maxsize=1)
def sample_bns() -> bytes:
    """0,75 s Stereo bei 16 kHz: links 440 Hz, rechts 660 Hz, Schleife ab 0,25 s."""
    import math
    rate, n = 16000, 12000
    left = [int(6000 * math.sin(2 * math.pi * 440 * i / rate)) for i in range(n)]
    right = [int(6000 * math.sin(2 * math.pi * 660 * i / rate)) for i in range(n)]
    return bns([left, right], rate, True, 4000)


def channel_banner_app() -> bytes:
    """Kanal-content0 wie auf der NAND: 0x40 Byte frei, IMET-Kopf bis 0x640, dann U8 mit meta/banner.bin
    und meta/sound.bin (BNS, zwei Töne mit Schleife).

    Szene: Hintergrund (RGB5A3), gedrehtes Logo (C8 mit Palette), ungetextureter Balken → drei Quads.
    Animation: banner_Start (30 Bilder) und banner_Loop (120 Bilder, mit Musterwechsel auf logo2.tpl)."""
    bg = tpl([(5, 64, 48, lambda x, y: (30, 40 + y * 3, 120 + x, 255), None)])
    palette = [rgb5a3_pixel(250, 200, 40), rgb5a3_pixel(250, 250, 250)]
    logo = tpl([(9, 32, 16, lambda x, y: (x // 4 + y // 4) % 2, palette)])
    root = pane("pan1", "RootPane", 0, 0, 608, 456, children=[
        pane("pic1", "bg", 0, 0, 608, 456, material=0),
        pane("pic1", "logo", 0, 40, 320, 160, rot=15, material=1),
        pane("pic1", "bar", 0, -170, 560, 40, alpha=200, material=2,
             vtx=[(255, 80, 80, 255), (255, 80, 80, 255), (120, 20, 20, 255), (120, 20, 20, 255)]),
    ])
    layout = brlyt(608, 456, ["bg.tpl", "logo.tpl"],
                   [("bg", 0, None), ("logo", 1, None), ("bar", None, (255, 255, 255, 255))], [root])
    logo2 = tpl([(9, 32, 16, lambda x, y: (x // 4 + y // 4 + 1) % 2, palette)])
    # Start: Logo fährt von links herein. Schleife: Logo pendelt, Balken pulsiert, Logo wechselt das Muster.
    start = brlan(30, False, [], [("logo", False, [("RLPA", [(0, 0, False, [(0, -400, 0), (30, 0, 0)])])])])
    loop = brlan(120, True, ["logo.tpl", "logo2.tpl"], [
        ("logo", False, [("RLPA", [(0, 5, False, [(0, 15, 0), (60, -15, 0), (120, 15, 0)])])]),
        ("bar", False, [("RLVC", [(0, 0x10, False, [(0, 200, 0), (60, 80, 0), (120, 200, 0)])])]),
        ("logo", True, [("RLTP", [(0, 0, True, [(0, 0, 0), (30, 1, 0), (60, 0, 0), (90, 1, 0)])])]),
    ])
    inner = u8_archive({"arc": {"anim": {"banner_Loop.brlan": loop, "banner_Start.brlan": start},
                                "blyt": {"banner.brlyt": layout},
                                "timg": {"bg.tpl": bg, "logo.tpl": logo, "logo2.tpl": logo2}}})
    outer = u8_archive({"meta": {"banner.bin": imd5(inner), "sound.bin": imd5(sample_bns())}})
    head = bytearray(0x640)
    head[0x80:0x84] = b"IMET"
    struct.pack_into(">II", head, 0x84, 0x600, 3)
    return bytes(head) + outer


def main(out: Path) -> int:
    out.mkdir(parents=True, exist_ok=True)
    (out / "opening.bnr").write_bytes(sample_bnr())
    (out / "banner.app").write_bytes(channel_banner_app())
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(Path(sys.argv[1])))
