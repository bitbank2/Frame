#!/usr/bin/env python3
"""
img2epd.py - Convert image to E2803 ACeP 6-color EPD format

EPD color nibble mapping (4bpp):
  0x0 = Black    0x1 = White   0x2 = Yellow
  0x3 = Red      0x5 = Blue    0x6 = Green

Modes
-----
  C header (default):
    python img2epd.py image.png --full          # 8-section C header
    python img2epd.py image.png --section 2     # single section

  PNG for WiFi download (2560x720, Floyd-Steinberg dithered):
    python img2epd.py image.jpg --png           # → image_epd.png
    python img2epd.py image.jpg --png --out out.png

  The PNG is 2560 wide (half the 5120 visible display columns).
  The ESP32 callback upscales each source pixel 2x horizontally,
  matching the format TRMNL's own server produces.

  Serve the PNG locally so the ESP32 can fetch it:
    python -m http.server 8080 --directory /path/to/output/
    # set TRMNL_BASE_URL or image URL to http://<your-pc-ip>:8080/out.png

Dependencies:  pip install Pillow numpy
"""

import sys
import os
import argparse
from pathlib import Path
from PIL import Image
import numpy as np

# ── EPD color palette ─────────────────────────────────────────────────────────
EPD_PALETTE = {
    0x0: (  0,   0,   0),   # Black
    0x1: (255, 255, 255),   # White
    0x2: (255, 223,   0),   # Yellow
    0x3: (196,   2,  51),   # Red
    0x5: (  0,  86, 163),   # Blue
    0x6: (  0, 155,  72),   # Green
}

SECTION_W      = 800
SECTION_H      = 720
NUM_SECTIONS   = 8
FULL_W         = SECTION_W * NUM_SECTIONS   # 6400 (raw IC layout)
DISPLAY_VIS_W  = 5120                       # visible columns on the display
BYTES_PER_SEC  = SECTION_W * SECTION_H // 2

palette_rgb  = np.array(list(EPD_PALETTE.values()), dtype=np.float32)
palette_keys = list(EPD_PALETTE.keys())

# ── Color quantization (nearest-neighbor, no dithering) ───────────────────────
def image_to_nibbles(img: Image.Image) -> np.ndarray:
    """Convert PIL image → (H, W) nibble array using nearest-color (no dithering)."""
    img = img.convert("RGB")
    pixels = np.array(img, dtype=np.float32)   # (H, W, 3)
    h, w   = pixels.shape[:2]
    nibbles   = np.zeros((h, w), dtype=np.uint8)
    best_dist = np.full((h, w), np.inf, dtype=np.float32)
    for nib_val, rgb in EPD_PALETTE.items():
        diff = pixels - np.array(rgb, dtype=np.float32)
        dist = (diff * diff).sum(axis=2)
        better = dist < best_dist
        best_dist[better] = dist[better]
        nibbles[better]   = nib_val
    return nibbles


# ── Floyd-Steinberg dithering (Pillow C implementation — fast) ────────────────
def _make_pal_image() -> Image.Image:
    """Build a Pillow 'P' mode palette image containing the 6 EPD colors."""
    pal = bytearray(768)   # 256 × 3 bytes
    for i, rgb in enumerate(EPD_PALETTE.values()):
        pal[i*3:i*3+3] = rgb
    img = Image.new("P", (1, 1))
    img.putpalette(bytes(pal))
    return img

_PAL_IMAGE = _make_pal_image()

def floyd_steinberg(img: Image.Image) -> Image.Image:
    """
    Quantise img to the 6 EPD colors with Floyd-Steinberg dithering.
    Uses Pillow's internal C implementation — completes in < 1 second.
    Returns a palette-mode ('P') image ready to save as PNG.
    """
    # dither=1 → Image.Dither.FLOYDSTEINBERG in Pillow ≥ 9.1
    return img.convert("RGB").quantize(palette=_PAL_IMAGE, dither=1)


# ── Packing ───────────────────────────────────────────────────────────────────
def nibbles_to_bytes(nibbles: np.ndarray) -> bytes:
    """Pack (H, W) nibble array → 4bpp byte stream (even col = high nibble)."""
    h, w = nibbles.shape
    assert w % 2 == 0, "Width must be even"
    packed = (nibbles[:, 0::2].astype(np.uint8) << 4) | nibbles[:, 1::2].astype(np.uint8)
    return packed.flatten().tobytes()


# PNG output: 2560×1440 matches TRMNL's HGD source format.
# Firmware row-skips 1440→720 (HGD Y compression) and 2× upscales columns 2560→5120.
PNG_W = DISPLAY_VIS_W // 2   # 2560
PNG_H = SECTION_H * 2        # 1440


# ── PNG output ────────────────────────────────────────────────────────────────
def save_epd_png(img: Image.Image, dst: Path, dither: bool = True) -> None:
    src_w, src_h = img.size
    print(f"Input: {src_w}×{src_h}", flush=True)

    # Letterbox into 2560×1440. Firmware row-skips 1440→720 and 2× upscales 2560→5120.
    scale = min(PNG_W / src_w, PNG_H / src_h)
    nw, nh = int(src_w * scale), int(src_h * scale)
    canvas = Image.new("RGB", (PNG_W, PNG_H), (255, 255, 255))
    canvas.paste(img.resize((nw, nh), Image.LANCZOS),
                 ((PNG_W - nw) // 2, (PNG_H - nh) // 2))
    print(f"Letterboxed {nw}×{nh} into {PNG_W}×{PNG_H} canvas", flush=True)

    mode = "Floyd-Steinberg" if dither else "nearest-color"
    print(f"Quantizing to 6 EPD colors ({mode})…", flush=True)
    result = floyd_steinberg(canvas) if dither else canvas.convert("RGB").quantize(
        palette=_PAL_IMAGE, dither=0)
    result.save(str(dst))

    kb = dst.stat().st_size / 1024
    print(f"Saved: {dst}  ({kb:.0f} KB)", flush=True)
    print()
    print("Serve to ESP32:")
    print(f'  python -m http.server 8080 --directory "{dst.parent}"')
    print(f"  URL: http://<your-PC-IP>:8080/{dst.name}")


# ── C header output ───────────────────────────────────────────────────────────
def bytes_to_c_array(data: bytes, label: str) -> str:
    lines = [f"// {len(data)} bytes  ({SECTION_W}×{SECTION_H} @ 4bpp)",
             f"static const uint8_t {label}[{len(data)}] = {{"]
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)


def write_header(sections_data, var_name: str, out_path: Path, source_file: str) -> None:
    lines = [
        f"// Auto-generated by img2epd.py from: {source_file}",
        f"// {len(sections_data)} section(s), {SECTION_W}×{SECTION_H} each, 4bpp E2803 format",
        "#pragma once", "#include <stdint.h>", "",
    ]
    for i, data in enumerate(sections_data):
        label = f"{var_name}_sec{i}" if len(sections_data) > 1 else var_name
        lines.append(bytes_to_c_array(data, label))
        lines.append("")
    if len(sections_data) > 1:
        lines.append(f"static const uint8_t* {var_name}_sections[{len(sections_data)}] = {{")
        for i in range(len(sections_data)):
            lines.append(f"    {var_name}_sec{i},")
        lines.append("};")
        lines.append("")
    out_path.write_text("\n".join(lines))
    print(f"Written: {out_path}  ({out_path.stat().st_size // 1024} KB)")


# ── CLI ───────────────────────────────────────────────────────────────────────
def main() -> None:
    parser = argparse.ArgumentParser(description="Convert image to E2803 EPD format")
    parser.add_argument("image", help="Input image file")
    parser.add_argument("--png",     action="store_true",
                        help=f"Output a {PNG_W}×{PNG_H} dithered PNG for WiFi download")
    parser.add_argument("--no-dither", action="store_true",
                        help="Skip Floyd-Steinberg dithering (nearest-color only)")
    parser.add_argument("--full",    action="store_true",
                        help=f"C-header mode: full display ({FULL_W}×{SECTION_H}), 8 sections")
    parser.add_argument("--section", type=int, default=None,
                        help="C-header mode: output only this section (0-7)")
    parser.add_argument("--name",   default=None,
                        help="Variable/file name prefix (default: input filename stem)")
    parser.add_argument("--out",    default=None, help="Output file path")
    args = parser.parse_args()

    src  = Path(args.image)
    base = (args.name or src.stem).replace("-", "_").replace(" ", "_")
    img  = Image.open(src)

    if args.png:
        dst = Path(args.out) if args.out else src.with_name(base + "_epd.png")
        save_epd_png(img, dst, dither=not args.no_dither)
        return

    # ── C header mode ──────────────────────────────────────────────────────────
    out_dir  = Path(__file__).parent.parent / "include"
    out_path = Path(args.out) if args.out else out_dir / f"{base}.h"

    if args.full or args.section is not None:
        img = img.resize((FULL_W, SECTION_H), Image.LANCZOS)
        nibbles = image_to_nibbles(img)
        if args.section is not None:
            s = args.section
            data = nibbles_to_bytes(nibbles[:, s*SECTION_W:(s+1)*SECTION_W])
            write_header([data], base, out_path, args.image)
        else:
            sections_data = [
                nibbles_to_bytes(nibbles[:, i*SECTION_W:(i+1)*SECTION_W])
                for i in range(NUM_SECTIONS)
            ]
            write_header(sections_data, base, out_path, args.image)
    else:
        img = img.resize((SECTION_W, SECTION_H), Image.LANCZOS)
        data = nibbles_to_bytes(image_to_nibbles(img))
        write_header([data], base, out_path, args.image)


if __name__ == "__main__":
    main()
