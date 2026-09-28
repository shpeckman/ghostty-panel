#!/bin/sh
# tools/lint.sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
gen=$(dirname "$(find .zig-cache -name wlr-layer-shell-unstable-v1-client-protocol.h | head -1)")
ver=$(dirname "$(find .zig-cache -name wayland-version.h | head -1)")
ft=$(dirname "$(find zig-pkg -path '*/include/freetype/freetype.h' | head -1)")/..
fc=$(dirname "$(dirname "$(find zig-pkg -path '*fontconfig/fontconfig.h' | head -1)")")
png=$(dirname "$(find zig-pkg -name png.h | head -1)")
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
for f in src/*.c; do
    "$root/build.sh" zig cc -c -o "$out/$(basename "$f" .c).o" -std=gnu23 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
        -DGHOSTTY_STATIC -DWL_EGL_PLATFORM=1 -D_GNU_SOURCE=1 -I"$gen" -I"$ver" -Ivendor/wayland/src -Ivendor/wayland/egl \
        -Ivendor/khronos -Ivendor/libxkbcommon/include -Ivendor/ghostty/include -I"$ft" -I"$fc" -I"$png" \
        -Ivendor/ghostty/pkg/libpng --embed-dir=vendor/ghostty/src/font/res "$f"
done
