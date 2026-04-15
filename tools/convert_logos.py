#!/usr/bin/env python3
"""
Convert club logos to RGB565 binary files for LittleFS.
Usage: python3 tools/convert_logos.py
Output: data/logos/{slug}.bin (64x64) and data/logos/{slug}_sm.bin (16x16)

Requirements: pip install Pillow requests
"""

import os, struct, requests
from io import BytesIO
from PIL import Image

OUTPUT_DIR = os.path.join(os.path.dirname(__file__), "..", "data", "logos")
os.makedirs(OUTPUT_DIR, exist_ok=True)

LG = (64, 64)
SM = (16, 16)

def to_rgb565(url_or_path, target_size, out_path):
    try:
        if url_or_path.startswith("http"):
            r = requests.get(url_or_path, timeout=10,
                             headers={"User-Agent": "Mozilla/5.0"})
            r.raise_for_status()
            img = Image.open(BytesIO(r.content)).convert("RGBA")
        else:
            img = Image.open(url_or_path).convert("RGBA")

        # Auto-crop transparent padding
        bbox = img.split()[3].getbbox()
        if bbox:
            img = img.crop(bbox)

        img = img.resize(target_size, Image.LANCZOS)

        # Composite on black background
        bg = Image.new("RGB", target_size, (0, 0, 0))
        bg.paste(img, mask=img.split()[3])

        with open(out_path, "wb") as f:
            for y in range(target_size[1]):
                for x in range(target_size[0]):
                    r_, g, b = bg.getpixel((x, y))
                    px = ((r_ & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                    f.write(struct.pack(">H", px))  # big-endian
        print(f"  OK  {os.path.basename(out_path)}")
    except Exception as e:
        print(f"  ERR {os.path.basename(out_path)}: {e}")

LNR_CDN = "https://cdn.lnr.fr/club/{slug}/photo/logo-thumbnail-2x.5ab42ba7610b284f40f993f6a452bec5362fa8b6"

CLUBS = [
    # (slug, url_override_or_None)
    # Top 14
    ("toulouse",        None),
    ("racing92",        "https://www.racing92.fr/wp-content/uploads/2025/05/racing92.png"),
    ("bordeaux-begles", None),
    ("la-rochelle",     None),
    ("clermont",        None),
    ("toulon",          "https://upload.wikimedia.org/wikipedia/fr/thumb/5/5a/Logo_Rugby_club_toulonnais.svg/480px-Logo_Rugby_club_toulonnais.svg.png"),
    ("montpellier",     None),
    ("lyon",            None),
    ("paris",           None),
    ("castres",         None),
    ("bayonne",         None),
    ("pau",             None),
    ("perpignan",       None),
    ("vannes",          None),
    # Pro D2
    ("montauban",       None),
    ("brive",           None),
    ("oyonnax",         None),
    ("aurillac",        None),
    ("grenoble",        None),
    ("rouen",           None),
    ("nevers",          None),
    ("carcassonne",     None),
    ("valence-romans",  None),
    ("provence",        None),
    ("angouleme",       None),
    ("narbonne",        None),
    # Champions Cup (non-French)
    ("leinster",        "https://media-cdn.incrowdsports.com/02ec4396-a5c2-49b2-bd5d-4056277b1278.png"),
    ("bath",            "https://media-cdn.incrowdsports.com/f4d9a293-9086-41bf-aa1b-c98d1c62fe3b.png"),
    ("glasgow",         "https://media-cdn.incrowdsports.com/f0e4ca1a-3001-42d4-a134-befe8348540c.png"),
    ("munster",         None),
    ("ulster",          None),
    ("northampton",     None),
    ("exeter",          None),
]

print("Converting logos...")
for slug, override in CLUBS:
    url = override or LNR_CDN.format(slug=slug)
    to_rgb565(url, LG, os.path.join(OUTPUT_DIR, f"{slug}.bin"))
    to_rgb565(url, SM, os.path.join(OUTPUT_DIR, f"{slug}_sm.bin"))

print(f"\nDone. Upload with: pio run --target uploadfs")
