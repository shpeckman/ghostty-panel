# Makefile
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
UNITDIR ?= $(PREFIX)/lib/systemd/user
DESTDIR ?=
ARGS ?=

BIN := ghostty-panel
BUILD := ./build.sh
STAMP := vendor/.stamp

.DEFAULT_GOAL := all
.PHONY: all release debug run install uninstall clean distclean lint vendor fetch help

all: release    ## optimized build into ./zig-out (default)

release: $(STAMP) ## optimized build into ./zig-out
	$(BUILD) release

debug: $(STAMP) ## debug build into ./zig-out
	$(BUILD) debug

run: release    ## optimized build, then run with ARGS="..."
	./zig-out/bin/$(BIN) $(ARGS)

install: release ## install the binary, the service link and the systemd user unit
	DESTDIR="$(DESTDIR)" ./tools/install.sh install "$(BINDIR)" "$(UNITDIR)"

uninstall:      ## remove the installed files
	DESTDIR="$(DESTDIR)" ./tools/install.sh uninstall "$(BINDIR)" "$(UNITDIR)"

clean:          ## remove build outputs and caches
	$(BUILD) clean

distclean: clean ## clean, and remove the unpacked zig toolchain and fetched vendor/
	rm -rf .toolchain zig-pkg
	find vendor -mindepth 1 -maxdepth 1 ! -name MANIFEST -exec rm -rf {} +

lint: debug     ## compile the sources with all warnings enabled
	./tools/lint.sh

$(STAMP): vendor/MANIFEST
	./tools/fetch-vendor.sh
	touch $@

vendor:         ## force a refetch of vendor/ from vendor/MANIFEST (needs network)
	rm -f $(STAMP)
	$(MAKE) $(STAMP)

fetch: $(STAMP) ## refresh zig-pkg after changing the ghostty pin (needs network)
	./tools/fetch-zig-packages.sh

help:           ## show this help
	@grep -E '^[a-zA-Z_-]+:.*?## ' $(MAKEFILE_LIST) | awk 'BEGIN {FS = ":.*?## "}; {printf "  make %-18s %s\n", $$1, $$2}'