# Makefile
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DESTDIR ?=
INSTALL ?= install
ARGS ?=

BIN := ghostty-panel
BUILD := ./build.sh

.DEFAULT_GOAL := all
.PHONY: all release debug run install uninstall clean distclean lint vendor fetch help

all: release    ## optimized build into ./zig-out (default)

release:        ## optimized build into ./zig-out
	$(BUILD) release

debug:          ## debug build into ./zig-out
	$(BUILD) debug

run: release    ## optimized build, then run with ARGS="..."
	./zig-out/bin/$(BIN) $(ARGS)

install: release ## install to $(DESTDIR)$(BINDIR), default /usr/local/bin
	$(INSTALL) -d "$(DESTDIR)$(BINDIR)"
	$(INSTALL) -m 755 zig-out/bin/$(BIN) "$(DESTDIR)$(BINDIR)/$(BIN)"

uninstall:      ## remove the installed binary
	rm -f "$(DESTDIR)$(BINDIR)/$(BIN)"

clean:          ## remove build outputs and caches
	$(BUILD) clean

distclean: clean ## clean, and remove the unpacked zig toolchain and fetched vendor/
	rm -rf .toolchain
	find vendor -mindepth 1 -maxdepth 1 ! -name MANIFEST -exec rm -rf {} +

lint: debug     ## compile the sources with all warnings enabled
	./tools/lint.sh

vendor:         ## (re)fetch vendor/ from vendor/MANIFEST (needs network)
	./tools/fetch-vendor.sh

fetch:          ## refresh zig-pkg after changing the ghostty pin (needs network)
	./tools/fetch-zig-packages.sh

help:           ## show this help
	@grep -E '^[a-zA-Z_-]+:.*?## ' $(MAKEFILE_LIST) | awk 'BEGIN {FS = ":.*?## "}; {printf "  make %-18s %s\n", $$1, $$2}'