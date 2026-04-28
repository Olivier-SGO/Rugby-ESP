#!/bin/bash
set -e

VERSION=$1
if [ -z "$VERSION" ]; then
    echo "Usage: $0 <version_tag>  (ex: v1.1.0)"
    exit 1
fi

# Extract version string from config.h
FW_VERSION=$(grep 'FIRMWARE_VERSION' include/config.h | sed 's/.*"\(.*\)".*/\1/')
if [ -z "$FW_VERSION" ]; then
    echo "ERROR: could not extract FIRMWARE_VERSION from include/config.h"
    exit 1
fi

echo "Building release for firmware version: $FW_VERSION"

# Build firmware and filesystem image
pio run -e matrixportal_s3
pio run -e matrixportal_s3 --target buildfs

FIRMWARE=".pio/build/matrixportal_s3/firmware.bin"
LITTLEFS=".pio/build/matrixportal_s3/littlefs.bin"

if [ ! -f "$FIRMWARE" ]; then
    echo "ERROR: firmware.bin not found at $FIRMWARE"
    exit 1
fi
if [ ! -f "$LITTLEFS" ]; then
    echo "ERROR: littlefs.bin not found at $LITTLEFS"
    exit 1
fi

FW_SIZE=$(stat -f%z "$FIRMWARE" 2>/dev/null || stat -c%s "$FIRMWARE")
FS_SIZE=$(stat -f%z "$LITTLEFS" 2>/dev/null || stat -c%s "$LITTLEFS")

cat > version.json <<EOF
{
  "version": "$FW_VERSION",
  "firmware_url": "https://github.com/Olivier-SGO/rugby-display-releases/releases/download/$VERSION/firmware.bin",
  "littlefs_url": "https://github.com/Olivier-SGO/rugby-display-releases/releases/download/$VERSION/littlefs.bin",
  "firmware_size": $FW_SIZE,
  "littlefs_size": $FS_SIZE
}
EOF

echo "version.json:"
cat version.json

# Create GitHub release (requires gh CLI authenticated)
if ! command -v gh &> /dev/null; then
    echo ""
    echo "WARNING: gh CLI not found. Release not created on GitHub."
    echo "Install with: brew install gh && gh auth login"
    echo "Then run: gh release create $VERSION firmware.bin littlefs.bin version.json --title \"Rugby ESP32 $VERSION\""
    exit 0
fi

echo ""
echo "Creating GitHub release $VERSION..."
gh release create "$VERSION" \
    "$FIRMWARE" \
    "$LITTLEFS" \
    version.json \
    --repo "Olivier-SGO/rugby-display-releases" \
    --title "Rugby ESP32 $VERSION" \
    --notes "Firmware version: $FW_VERSION"

echo "Release $VERSION created successfully."
