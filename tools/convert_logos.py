#!/usr/bin/env python3
"""
Convert club logos to RGB565 binary files for LittleFS.
Usage: python3 tools/convert_logos.py
Output: data/logos/{slug}.bin (64x64) and data/logos/{slug}_sm.bin (16x16)

Requirements: pip install Pillow requests
"""

import os, sys, struct, requests
from io import BytesIO
from PIL import Image

# macOS: ensure Homebrew cairo is findable for cairosvg
if sys.platform == "darwin":
    for brew_lib in ("/opt/homebrew/lib", "/usr/local/lib"):
        if os.path.exists(os.path.join(brew_lib, "libcairo.2.dylib")):
            os.environ.setdefault("DYLD_LIBRARY_PATH",
                                  brew_lib + ":" + os.environ.get("DYLD_LIBRARY_PATH", ""))
            break

def _open_image(data: bytes, url: str) -> Image.Image:
    """Open image bytes, handling SVG via cairosvg if needed."""
    if url.endswith(".svg") or b"<svg" in data[:512]:
        try:
            import cairosvg
            png_data = cairosvg.svg2png(bytestring=data, output_width=256, output_height=256)
            return Image.open(BytesIO(png_data)).convert("RGBA")
        except ImportError:
            raise RuntimeError("SVG detected but cairosvg not installed. Run: pip install cairosvg")
    return Image.open(BytesIO(data)).convert("RGBA")

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
            img = _open_image(r.content, url_or_path)
        else:
            with open(url_or_path, "rb") as fh:
                img = _open_image(fh.read(), url_or_path)

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
    ("toulon",          "https://upload.wikimedia.org/wikipedia/fr/5/5a/Logo_Rugby_club_toulonnais.svg"),
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
    ("provence",        "https://cdn.lnr.fr/club/provence-rugby/photo/logo-thumbnail-2x.5ab42ba7610b284f40f993f6a452bec5362fa8b6"),
    ("angouleme",       None),
    ("narbonne",        None),
    # Pro D2 (additional)
    ("agen",            None),
    ("biarritz",        "https://upload.wikimedia.org/wikipedia/fr/thumb/9/94/B_O.svg/250px-B_O.svg.png"),
    ("mont-de-marsan",  None),
    ("dax",             None),
    ("beziers",         None),
    ("colomiers",       None),
    # Champions Cup (non-French)
    ("leinster",        "https://media-cdn.incrowdsports.com/02ec4396-a5c2-49b2-bd5d-4056277b1278.png"),
    ("bath",            "https://media-cdn.incrowdsports.com/f4d9a293-9086-41bf-aa1b-c98d1c62fe3b.png"),
    ("glasgow",         "https://media-cdn.incrowdsports.com/f0e4ca1a-3001-42d4-a134-befe8348540c.png"),
    ("munster",         "https://ih1.redbubble.net/image.2984596055.4307/flat,750x,075,f-pad,750x1000,f8f8f8.jpg"),
    ("ulster",          "https://upload.wikimedia.org/wikipedia/fr/c/c0/Ulster_Rugby_logo.svg"),
    ("northampton",     "https://upload.wikimedia.org/wikipedia/fr/thumb/9/92/Logo_Northampton_Saints_2024.png/250px-Logo_Northampton_Saints_2024.png"),
    ("exeter",          "https://c.files.bbci.co.uk/AEDA/production/_123026744_exeterchiefsmasterlogorgbwhite2022-01.jpg"),
]

print("Converting club logos...")
for slug, override in CLUBS:
    url = override or LNR_CDN.format(slug=slug)
    to_rgb565(url, LG, os.path.join(OUTPUT_DIR, f"{slug}.bin"))
    to_rgb565(url, SM, os.path.join(OUTPUT_DIR, f"{slug}_sm.bin"))

def to_rgb565_aspect(url_or_path, target_h, max_w, out_path):
    """Resize to target_h pixels tall, preserving aspect ratio, up to max_w wide."""
    try:
        if url_or_path.startswith("http"):
            r = requests.get(url_or_path, timeout=10,
                             headers={"User-Agent": "Mozilla/5.0"})
            r.raise_for_status()
            img = _open_image(r.content, url_or_path)
        else:
            with open(url_or_path, "rb") as fh:
                img = _open_image(fh.read(), url_or_path)

        bbox = img.split()[3].getbbox()
        if bbox:
            img = img.crop(bbox)

        nat_w, nat_h = img.size
        target_w = max(2, min(round(nat_w * target_h / nat_h), max_w))
        target_size = (target_w, target_h)

        img = img.resize(target_size, Image.LANCZOS)
        bg = Image.new("RGB", target_size, (0, 0, 0))
        bg.paste(img, mask=img.split()[3])

        with open(out_path, "wb") as f:
            for y in range(target_h):
                for x in range(target_w):
                    r_, g, b = bg.getpixel((x, y))
                    px = ((r_ & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                    f.write(struct.pack(">H", px))
        print(f"  OK  {os.path.basename(out_path)} ({target_w}x{target_h})")
    except Exception as e:
        print(f"  ERR {os.path.basename(out_path)}: {e}")

COMP_H    = 32
COMP_MAX_W = 48
COMP_SM_H = 18
COMP_SM_MAX_W = 28

COMP_LOGOS = [
    ("comp_top14", "https://assets.lnr.fr/1/1/1/8/0/7/conversions/logo-top14.e32e7e9a-logo.webp"),
    ("comp_prod2", "https://assets.lnr.fr/8/8/9/4/1/conversions/LOGO_PROD2-logo.webp"),
    ("comp_cc",    "https://medias.france.tv/DCSXZ4x0iXUFVqmzVimSFUrf5u0/1440x0/filters:quality(85):format(png)/d/b/z/phpym3zbd.png"),
]
print("\nConverting competition logos (aspect-ratio preserving)...")
for slug, url in COMP_LOGOS:
    to_rgb565_aspect(url, COMP_H,    COMP_MAX_W,    os.path.join(OUTPUT_DIR, f"{slug}.bin"))
    to_rgb565_aspect(url, COMP_SM_H, COMP_SM_MAX_W, os.path.join(OUTPUT_DIR, f"{slug}_sm.bin"))

print(f"\nDone. Upload with: pio run --target uploadfs")
