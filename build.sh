# build.sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

case "$(uname -m)" in
    x86_64 | amd64) arch=x86_64 ;;
    aarch64 | arm64) arch=aarch64 ;;
    *) echo "build.sh: unsupported architecture: $(uname -m)" >&2; exit 1 ;;
esac

read -r zig_version dest checksum <<EOF
$(awk -v arch="$arch" '$1 == "zig" && $3 == arch { print $4, $5, $8 }' "$root/vendor/MANIFEST")
EOF
if [ -z "${dest:-}" ]; then
    echo "build.sh: no zig entry for $arch in vendor/MANIFEST" >&2
    exit 1
fi

sha256=${checksum#sha256:}
name=$(basename "$dest" .tar.xz)
tarball="$root/vendor/$dest"
toolchain="$root/.toolchain/$name"
zig="$toolchain/zig"
cache="$root/.zig-global-cache"

ensure_zig() {
    [ -x "$zig" ] && return 0
    if [ ! -f "$tarball" ]; then
        echo "build.sh: missing $tarball" >&2
        echo "build.sh: fetch it with: ./tools/fetch-vendor.sh zig" >&2
        exit 1
    fi
    actual=$(sha256sum "$tarball" | cut -d' ' -f1)
    if [ "$actual" != "$sha256" ]; then
        echo "build.sh: checksum mismatch for $tarball" >&2
        echo "build.sh:   expected $sha256" >&2
        echo "build.sh:   actual   $actual" >&2
        exit 1
    fi
    mkdir -p "$root/.toolchain"
    rm -rf "$toolchain.tmp"
    mkdir "$toolchain.tmp"
    tar -xJf "$tarball" -C "$toolchain.tmp"
    mv "$toolchain.tmp/$name" "$toolchain"
    rmdir "$toolchain.tmp"
}

zig_build() {
    ensure_zig
    cd "$root"
    "$zig" build --global-cache-dir "$cache" "$@"
}

usage() {
    cat <<EOF
usage: ./build.sh [command]

commands:
  release            optimized build into ./zig-out (default)
  debug              debug build into ./zig-out
  install [PREFIX]   optimized build installed to PREFIX (default: \$HOME/.local),
                     with the systemd user unit in PREFIX/share/systemd/user
  run [ARGS...]      optimized build, then run ghostty-panel with ARGS
  clean              remove build outputs and caches
  zig [ARGS...]      run the vendored zig $zig_version
EOF
}

cmd=${1:-release}
[ $# -gt 0 ] && shift

case "$cmd" in
    release) zig_build -Doptimize=ReleaseFast --prefix "$root/zig-out" ;;
    debug) zig_build -Doptimize=Debug --prefix "$root/zig-out" ;;
    install)
        prefix=${1:-$HOME/.local}
        zig_build -Doptimize=ReleaseFast --prefix "$root/zig-out"
        "$root/tools/install.sh" install "$prefix/bin" "$prefix/share/systemd/user"
        ;;
    run) zig_build -Doptimize=ReleaseFast --prefix "$root/zig-out" && exec "$root/zig-out/bin/ghostty-panel" "$@" ;;
    clean) rm -rf "$root/.zig-cache" "$root/zig-out" "$cache" ;;
    zig) ensure_zig && exec "$zig" "$@" ;;
    -h | --help | help) usage ;;
    *) usage >&2; exit 1 ;;
esac