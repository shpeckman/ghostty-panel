# build.sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
zig_version=0.16.0

case "$(uname -m)" in
    x86_64 | amd64) arch=x86_64 sha256=70e49664a74374b48b51e6f3fdfbf437f6395d42509050588bd49abe52ba3d00 ;;
    aarch64 | arm64) arch=aarch64 sha256=ea4b09bfb22ec6f6c6ceac57ab63efb6b46e17ab08d21f69f3a48b38e1534f17 ;;
    *) echo "build.sh: unsupported architecture: $(uname -m)" >&2; exit 1 ;;
esac

name="zig-$arch-linux-$zig_version"
tarball="$root/vendor/zig/$name.tar.xz"
toolchain="$root/.toolchain/$name"
zig="$toolchain/zig"
cache="$root/.zig-global-cache"

ensure_zig() {
    [ -x "$zig" ] && return 0
    if [ ! -f "$tarball" ]; then
        echo "build.sh: missing $tarball" >&2
        echo "build.sh: fetch it with: curl -fLo '$tarball' https://ziglang.org/download/$zig_version/$name.tar.xz" >&2
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
  install [PREFIX]   optimized build installed to PREFIX (default: \$HOME/.local)
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
    install) zig_build -Doptimize=ReleaseFast --prefix "${1:-$HOME/.local}" ;;
    run) zig_build -Doptimize=ReleaseFast --prefix "$root/zig-out" && exec "$root/zig-out/bin/ghostty-panel" "$@" ;;
    clean) rm -rf "$root/.zig-cache" "$root/zig-out" "$cache" ;;
    zig) ensure_zig && exec "$zig" "$@" ;;
    -h | --help | help) usage ;;
    *) usage >&2; exit 1 ;;
esac
