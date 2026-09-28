#!/bin/sh
# tools/fetch-vendor.sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
vendor="$root/vendor"
manifest="$vendor/MANIFEST"

die() { echo "fetch-vendor: $*" >&2; exit 1; }
say() { echo "fetch-vendor: $*" >&2; }

case "$(uname -m)" in
    x86_64 | amd64) host=x86_64 ;;
    aarch64 | arm64) host=aarch64 ;;
    *) die "unsupported architecture: $(uname -m)" ;;
esac

rows() {
    awk -v host="$host" 'NF == 8 && $1 != "#" && $1 != "component" && $1 !~ /:$/ && ($3 == "any" || $3 == host)' "$manifest"
}

directive() {
    awk -v key="$1:" '$1 == key { $1 = ""; sub(/^ /, ""); print }' "$manifest"
}

expand() {
    tr ' ' '\n' | awk 'NF {
        if (match($0, /\{[^}]*\}/)) {
            pre = substr($0, 1, RSTART - 1)
            post = substr($0, RSTART + RLENGTH)
            n = split(substr($0, RSTART + 1, RLENGTH - 2), part, ",")
            for (i = 1; i <= n; i++) print pre part[i] post
        } else print
    }'
}

selected=$(rows | awk -v want="$*" 'BEGIN { n = split(want, w, " "); for (i = 1; i <= n; i++) keep[w[i]] = 1 } n == 0 || $1 in keep')
for name in "$@"; do
    printf '%s\n' "$selected" | awk -v c="$name" '$1 == c { f = 1 } END { exit !f }' || die "unknown component for $host: $name"
done
[ -n "$selected" ] || die "nothing to fetch"

owned() {
    printf '%s\n' "$selected" | awk -v p="$1" '$5 == p || index(p, $5 "/") == 1 { f = 1 } END { exit !f }'
}

stage=$(mktemp -d "$vendor/.fetch.XXXXXX")
trap 'rm -rf "$stage"' EXIT
trap 'exit 1' INT TERM HUP

verify() {
    case "$2" in
        sha256:*) actual=sha256:$(sha256sum "$1" | cut -d' ' -f1) ;;
        *) die "$component: unsupported checksum $2" ;;
    esac
    [ "$actual" = "$2" ] || die "$component: checksum mismatch, expected $2, got $actual"
}

place() {
    src=$1
    [ "$subdir" = . ] || src="$1/$subdir"
    [ -e "$src" ] || die "$component: $subdir not found in $source"
    mkdir -p "$(dirname "$out")"
    mv "$src" "$out"
}

fetch_file() {
    mkdir -p "$(dirname "$out")"
    curl -sSfL --retry 3 -o "$out" "$source"
    verify "$out" "$checksum"
}

fetch_tar() {
    archive="$stage/dl/$key"
    extract="$stage/x/$key"
    mkdir -p "$stage/dl" "$extract"
    curl -sSfL --retry 3 -o "$archive" "$source"
    verify "$archive" "$checksum"
    tar -xf "$archive" -C "$extract" --strip-components=1
    place "$extract"
}

fetch_git() {
    rev=${checksum#git:}
    [ "$rev" != "$checksum" ] || die "$component: git source needs a git:<commit> checksum"
    work="$stage/git/$key"
    git init -q "$work"
    git -C "$work" remote add origin "$source"
    if [ "$subdir" = . ]; then
        git -C "$work" fetch -q --depth 1 origin "$rev"
    else
        git -C "$work" fetch -q --depth 1 --filter=blob:none origin "$rev"
        git -C "$work" sparse-checkout set --no-cone "/$subdir/"
    fi
    git -C "$work" -c advice.detachedHead=false checkout -q FETCH_HEAD
    [ "git:$(git -C "$work" rev-parse HEAD)" = "$checksum" ] || die "$component: fetched commit does not match $checksum"
    rm -rf "$work/.git"
    place "$work"
}

printf '%s\n' "$selected" | while read -r component kind arch version dest subdir source checksum; do
    say "$component $version"
    out="$stage/tree/$dest"
    key=$(printf '%s' "$dest" | tr / _)
    case "$kind" in
        file | tar | git) "fetch_$kind" </dev/null ;;
        *) die "$component: unknown kind $kind" ;;
    esac
done

directive pruned | expand | while read -r path; do
    owned "$path" || continue
    [ -e "$stage/tree/$path" ] || die "pruned path not found: $path"
    rm -rf "$stage/tree/$path"
done

directive generated | while read -r dir tool version cmd; do
    owned "$dir" || continue
    command -v "$tool" >/dev/null || die "$dir: needs $tool $version"
    have=$("$tool" --version | awk 'NR == 1 { print $NF }')
    [ "$have" = "$version" ] || die "$dir: needs $tool $version, found $have"
    say "generate $dir"
    (cd "$stage/tree/$dir" && sh -c "$cmd") </dev/null || die "$dir: generation failed"
done

printf '%s\n' "$selected" | while read -r component kind arch version dest subdir source checksum; do
    rm -rf "${vendor:?}/$dest"
    mkdir -p "$(dirname "$vendor/$dest")"
    mv "$stage/tree/$dest" "$vendor/$dest"
done