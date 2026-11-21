#!/usr/bin/env bash
# Convert a PNG into a multi-resolution Windows .ico using ImageMagick.
#
# The resulting layout mirrors icons/radiant.ico:
#   - 256x256 as a 32-bit PNG-compressed frame (Vista+ style)
#   - 64, 48, 32, 24 and 16 px each twice: once as 32-bit BGRA BMP and
#     once as 8-bit palettized BMP (with 1-bit AND transparency mask) for
#     legacy consumers
#
# Usage: tools/convert-png-to-ico.sh input.png [output.ico]
set -euo pipefail

usage() {
    echo "usage: $0 input.png [output.ico]" >&2
    exit 2
}

[[ $# -ge 1 && $# -le 2 ]] || usage

in=$1
out=${2:-${in%.*}.ico}

[[ -f $in ]] || { echo "error: input '$in' does not exist" >&2; exit 1; }

# Prefer the ImageMagick 7 entry point, fall back to IM6's 'convert'.
if command -v magick >/dev/null 2>&1; then
    im=(magick)
elif command -v convert >/dev/null 2>&1; then
    im=(convert)
else
    echo "error: ImageMagick ('magick' or 'convert') not found" >&2
    exit 1
fi

# Fit the source into a square canvas of the given size, keeping the aspect
# ratio and padding with transparency so non-square inputs don't get stretched.
fit() {
    local s=$1
    printf '%s\n' -resize "${s}x${s}" -gravity center -extent "${s}x${s}"
}

args=()

# Large frame: forced truecolor; the png-compression-size define below makes
# ImageMagick store frames of this size as embedded PNG instead of raw BMP.
mapfile -t f < <(fit 256)
args+=( '(' -clone 0 "${f[@]}" -type TrueColorAlpha ')' )

# Smaller frames: one 32-bit and one 8-bit (256-colour palette) variant each.
for s in 64 48 32 24 16; do
    mapfile -t f < <(fit "$s")
    args+=( '(' -clone 0 "${f[@]}" -type TrueColorAlpha ')' )
    args+=( '(' -clone 0 "${f[@]}" -colors 256 ')' )
done

"${im[@]}" "$in" -background none -alpha on \
    -define icon:png-compression-size=256 \
    "${args[@]}" \
    -delete 0 \
    "$out"

[[ -s $out ]] || { echo "error: failed to write '$out'" >&2; exit 1; }
echo "wrote $out"
