#!/bin/sh
# tools/install.sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
destdir=${DESTDIR:-}

usage() {
    echo "usage: tools/install.sh install|uninstall BINDIR UNITDIR" >&2
    exit 1
}

[ $# -eq 3 ] || usage
action=$1
bindir=$2
unitdir=$3

case "$action" in
    install)
        install -d "$destdir$bindir" "$destdir$unitdir"
        install -m 755 "$root/zig-out/bin/ghostty-panel" "$destdir$bindir/ghostty-panel"
        ln -sf ghostty-panel "$destdir$bindir/ghostty-panel-service"
        sed "s|@BINDIR@|$bindir|g" "$root/dist/ghostty-panel.service.in" > "$destdir$unitdir/ghostty-panel.service"
        chmod 644 "$destdir$unitdir/ghostty-panel.service"
        ;;
    uninstall)
        rm -f "$destdir$bindir/ghostty-panel" "$destdir$bindir/ghostty-panel-service" "$destdir$unitdir/ghostty-panel.service"
        ;;
    *) usage ;;
esac
