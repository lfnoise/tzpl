#!/bin/bash
# Build a full multi-representation .icns from a 1024x1024 RGBA PNG.
# JUCE's juceaide writes only the single 1024px (ic10) representation,
# which macOS renders inconsistently (e.g. the placeholder icon in the
# Cmd+Tab app switcher); iconutil emits the complete standard set.
#
#   make_icns.sh <source-1024.png> <output.icns>
set -euo pipefail

src="$1"
out="$2"
tmp="$(mktemp -d)/Icon.iconset"
mkdir -p "$tmp"

for size in 16 32 128 256 512; do
    sips -z "$size" "$size" "$src" --out "$tmp/icon_${size}x${size}.png" > /dev/null
    two=$((size * 2))
    sips -z "$two" "$two" "$src" --out "$tmp/icon_${size}x${size}@2x.png" > /dev/null
done

iconutil -c icns -o "$out" "$tmp"
rm -rf "$(dirname "$tmp")"
