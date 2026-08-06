#!/usr/bin/env python3
"""
Convert RGB565 .bin logos back to viewable PNGs, for visual QA before flashing.
Usage: python3 tools/preview_logos.py <slug> [<slug> ...]
Output: tools/logo_previews/{slug}.png (large) and {slug}_sm.png (small, 16x16)
"""
import os, struct, sys
from PIL import Image

LOGO_DIR = os.path.join(os.path.dirname(__file__), "..", "data", "logos")
OUT_DIR = os.path.join(os.path.dirname(__file__), "logo_previews")
os.makedirs(OUT_DIR, exist_ok=True)

SCALE = 6

def render(bin_path, dim, out_path):
    if not os.path.exists(bin_path):
        print(f"  MISSING {bin_path}")
        return
    with open(bin_path, "rb") as f:
        data = f.read()
    img = Image.new("RGB", (dim, dim))
    px = img.load()
    idx = 0
    for y in range(dim):
        for x in range(dim):
            val = struct.unpack(">H", data[idx:idx + 2])[0]
            idx += 2
            r = (val >> 11) & 0x1F
            g = (val >> 5) & 0x3F
            b = val & 0x1F
            px[x, y] = ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))
    img = img.resize((dim * SCALE, dim * SCALE), Image.NEAREST)
    img.save(out_path)
    print(f"  OK {out_path}")

if __name__ == "__main__":
    slugs = sys.argv[1:]
    if not slugs:
        print("Usage: python3 tools/preview_logos.py <slug> [<slug> ...]")
        sys.exit(1)
    for slug in slugs:
        render(os.path.join(LOGO_DIR, f"{slug}.bin"), 64, os.path.join(OUT_DIR, f"{slug}.png"))
        render(os.path.join(LOGO_DIR, f"{slug}_sm.bin"), 16, os.path.join(OUT_DIR, f"{slug}_sm.png"))
