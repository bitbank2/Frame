"""
Convert any PNG to EPD raw binary for TRMNL V3 C5 display.

Output: 8 sections × 288000 bytes = 2,304,000 bytes total.
Each section is 800×720 pixels packed as 4bpp nibbles (2 pixels/byte):
  high nibble (bits 7-4) = even-x pixel,  low nibble (bits 3-0) = odd-x pixel.

Usage:
    python png_to_bin.py input.png image.bin
    python png_to_bin.py input.png          # writes image.bin

Requirements:
    pip install Pillow numpy
"""

import sys
import numpy as np
from PIL import Image

# EPD 6-color palette
PALETTE_RGB = np.array([
    [0,   0,   0  ],  # 0x0  Black
    [255, 255, 255],  # 0x1  White
    [255, 223, 0  ],  # 0x2  Yellow
    [196, 2,   51 ],  # 0x3  Red
    [0,   86,  163],  # 0x5  Blue
    [0,   155, 72 ],  # 0x6  Green
], dtype=np.float32)
PALETTE_NIB = np.array([0x0, 0x1, 0x2, 0x3, 0x5, 0x6], dtype=np.uint8)

DISPLAY_W = 5120   # total visible columns across all ICs
DISPLAY_H = 720    # rows
SEC_W     = 800    # columns per section buffer (including dummy for IC4/IC8)
SECTIONS  = 8

def quantize(pixels):
    """(H, W, 3) float32 → (H, W) EPD nibble values, nearest-color."""
    # diff: (H, W, 6, 3)  →  dist: (H, W, 6)  →  idx: (H, W)
    diff = pixels[:, :, np.newaxis, :] - PALETTE_RGB
    idx  = (diff * diff).sum(axis=-1).argmin(axis=-1)
    return PALETTE_NIB[idx]   # (H, W)

def pack_section(nibs):
    """Pack (720, 800) nibble array into 288 000-byte EPD section buffer."""
    # Reshape into (720, 400, 2): column 0=even-x (high nib), 1=odd-x (low nib)
    pairs  = nibs.reshape(DISPLAY_H, SEC_W // 2, 2).astype(np.uint8)
    packed = (pairs[:, :, 0] << 4) | pairs[:, :, 1]
    return packed.tobytes()

def convert(src_path, dst_path):
    img = Image.open(src_path).convert("RGB")
    print(f"Input : {img.size[0]}x{img.size[1]}  →  resizing to {DISPLAY_W}x{DISPLAY_H}")
    img  = img.resize((DISPLAY_W, DISPLAY_H), Image.NEAREST)
    arr  = np.array(img, dtype=np.float32)   # (720, 5120, 3)
    nibs = quantize(arr)                     # (720, 5120)  — nibble per display pixel

    # Build per-section nibble arrays (default white = 0x1 for dummy columns)
    WHITE = np.uint8(0x1)
    secs  = [np.full((DISPLAY_H, SEC_W), WHITE, dtype=np.uint8) for _ in range(SECTIONS)]

    # IC1-3  (sections 0-2): full 800 active columns each
    secs[0][:] = nibs[:,    0: 800]
    secs[1][:] = nibs[:,  800:1600]
    secs[2][:] = nibs[:, 1600:2400]

    # IC4   (section  3): 160 active + 640 dummy → active columns go into buf[0:160]
    secs[3][:, :160] = nibs[:, 2400:2560]

    # IC5-7  (sections 4-6): full 800 active columns each
    secs[4][:] = nibs[:, 2560:3360]
    secs[5][:] = nibs[:, 3360:4160]
    secs[6][:] = nibs[:, 4160:4960]

    # IC8   (section  7): 160 active + 640 dummy → active columns go into buf[0:160]
    secs[7][:, :160] = nibs[:, 4960:5120]

    with open(dst_path, "wb") as f:
        for s in secs:
            f.write(pack_section(s))

    total = SECTIONS * SEC_W * DISPLAY_H // 2
    print(f"Output: {dst_path}  ({total:,} bytes)")

if __name__ == "__main__":
    src = sys.argv[1] if len(sys.argv) > 1 else "input.png"
    dst = sys.argv[2] if len(sys.argv) > 2 else "image.bin"
    convert(src, dst)
