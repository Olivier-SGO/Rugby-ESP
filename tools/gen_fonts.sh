#!/bin/bash
# Run once on host to generate Atkinson Hyperlegible GFX font headers
# Requires: gcc, freetype2-dev (brew install freetype on macOS)
set -e

FONT_DIR="$(dirname "$0")/../include/fonts"
mkdir -p "$FONT_DIR"

# Detect freetype prefix (Homebrew Apple Silicon or Intel)
if [ -d /opt/homebrew/opt/freetype ]; then
  FTPREFIX=/opt/homebrew/opt/freetype
elif [ -d /usr/local/opt/freetype ]; then
  FTPREFIX=/usr/local/opt/freetype
else
  echo "freetype not found. Install with: brew install freetype" >&2
  exit 1
fi
FTINC="$FTPREFIX/include/freetype2"
FTLIB="$FTPREFIX/lib"

# Clone Adafruit GFX Library for fontconvert
TMP=$(mktemp -d)
git clone --depth 1 https://github.com/adafruit/Adafruit-GFX-Library.git "$TMP/adafruit-gfx"

# Compile fontconvert with correct freetype paths
gcc -Wall -I"$FTINC" "$TMP/adafruit-gfx/fontconvert/fontconvert.c" \
  -L"$FTLIB" -lfreetype -o "$TMP/fontconvert"

# Download Atkinson Hyperlegible TTF fonts
# Files are renamed so fontconvert derives the correct GFXfont variable names:
#   AtkinsonHyperlegible.ttf     -> AtkinsonHyperlegible{N}pt7b
#   AtkinsonHyperlegibleBold.ttf -> AtkinsonHyperlegibleBold{N}pt7b
BASE="https://github.com/googlefonts/atkinson-hyperlegible/raw/main/fonts/ttf"
curl -L "$BASE/AtkinsonHyperlegible-Regular.ttf" -o "$TMP/AtkinsonHyperlegible.ttf"
curl -L "$BASE/AtkinsonHyperlegible-Bold.ttf"    -o "$TMP/AtkinsonHyperlegibleBold.ttf"

"$TMP/fontconvert" "$TMP/AtkinsonHyperlegible.ttf"     8  > "$FONT_DIR/AtkinsonHyperlegible8pt7b.h"
"$TMP/fontconvert" "$TMP/AtkinsonHyperlegible.ttf"     10 > "$FONT_DIR/AtkinsonHyperlegible10pt7b.h"
"$TMP/fontconvert" "$TMP/AtkinsonHyperlegibleBold.ttf" 12 > "$FONT_DIR/AtkinsonHyperlegibleBold12pt7b.h"

echo "Fonts generated in $FONT_DIR"
rm -rf "$TMP"
