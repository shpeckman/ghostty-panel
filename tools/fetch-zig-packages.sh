#!/bin/sh
# tools/fetch-zig-packages.sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
dl=$(mktemp -d)
cache="$root/.zig-global-cache"
trap 'rm -rf "$dl"' EXIT
while :; do
    out=$("$root/build.sh" zig build --global-cache-dir "$cache" --help 2>&1 || true)
    urls=$(printf '%s\n' "$out" | grep -o 'https://[^" ]*' | sort -u || true)
    [ -z "$urls" ] && break
    for url in $urls; do
        file="$dl/$(basename "$url")"
        curl -sSfL --retry 3 -o "$file" "$url"
        "$root/build.sh" zig fetch --global-cache-dir "$cache" "$file" >/dev/null
    done
done

ls "$root/zig-pkg"
