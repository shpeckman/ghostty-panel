// build.zig
const std = @import("std");

const Protocol = struct { name: []const u8, xml: []const u8 };

const protocols = [_]Protocol{
    .{ .name = "wayland", .xml = "vendor/wayland/protocol/wayland.xml" },
    .{ .name = "xdg-shell", .xml = "vendor/wayland-protocols/stable/xdg-shell/xdg-shell.xml" },
    .{ .name = "viewporter", .xml = "vendor/wayland-protocols/stable/viewporter/viewporter.xml" },
    .{ .name = "tablet-v2", .xml = "vendor/wayland-protocols/stable/tablet/tablet-v2.xml" },
    .{ .name = "fractional-scale-v1", .xml = "vendor/wayland-protocols/staging/fractional-scale/fractional-scale-v1.xml" },
    .{ .name = "cursor-shape-v1", .xml = "vendor/wayland-protocols/staging/cursor-shape/cursor-shape-v1.xml" },
    .{ .name = "xdg-output-unstable-v1", .xml = "vendor/wayland-protocols/unstable/xdg-output/xdg-output-unstable-v1.xml" },
    .{ .name = "keyboard-shortcuts-inhibit-unstable-v1", .xml = "vendor/wayland-protocols/unstable/keyboard-shortcuts-inhibit/keyboard-shortcuts-inhibit-unstable-v1.xml" },
    .{ .name = "wlr-layer-shell-unstable-v1", .xml = "vendor/wlr-protocols/unstable/wlr-layer-shell-unstable-v1.xml" },
};

const app_sources = [_][]const u8{
    "main.c",
    "app.c",
    "boxdraw.c",
    "cli.c",
    "config.c",
    "dynload.c",
    "font.c",
    "image.c",
    "input.c",
    "json.c",
    "panel.c",
    "pty.c",
    "rc.c",
    "render.c",
    "util.c",
};

const expat_sources = [_][]const u8{
    "lib/xmlparse.c",
    "lib/xmlrole.c",
    "lib/xmltok.c",
};

const xkbcommon_sources = [_][]const u8{
    "src/compose/parser.c",
    "src/compose/paths.c",
    "src/compose/state.c",
    "src/compose/table.c",
    "src/xkbcomp/action.c",
    "src/xkbcomp/ast-build.c",
    "src/xkbcomp/compat.c",
    "src/xkbcomp/expr.c",
    "src/xkbcomp/include.c",
    "src/xkbcomp/keycodes.c",
    "src/xkbcomp/keymap.c",
    "src/xkbcomp/keymap-dump.c",
    "src/xkbcomp/keywords.c",
    "src/xkbcomp/rules.c",
    "src/xkbcomp/scanner.c",
    "src/xkbcomp/symbols.c",
    "src/xkbcomp/types.c",
    "src/xkbcomp/vmod.c",
    "src/xkbcomp/xkbcomp.c",
    "src/atom.c",
    "src/context.c",
    "src/context-priv.c",
    "src/keysym.c",
    "src/keysym-case-mappings.c",
    "src/keysym-utf.c",
    "src/keymap.c",
    "src/keymap-priv.c",
    "src/rmlvo.c",
    "src/scanner-utils.c",
    "src/state.c",
    "src/text.c",
    "src/utf8.c",
    "src/utf8-decoding.c",
    "src/utils.c",
    "src/utils-paths.c",
    "generated/parser.c",
};

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const scanner = buildScanner(b);
    const wayland_includes = generateProtocols(b, scanner);
    const xkbcommon = buildXkbcommon(b, target, optimize);

    const ghostty = b.dependency("ghostty", .{ .target = target, .optimize = optimize });
    const freetype = b.dependency("freetype", .{ .target = target, .optimize = optimize, .@"enable-libpng" = true });
    const fontconfig = b.dependency("fontconfig", .{ .target = target, .optimize = optimize });
    const libpng = b.dependency("libpng", .{ .target = target, .optimize = optimize });

    const mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
        .strip = optimize == .ReleaseFast or optimize == .ReleaseSmall,
    });
    mod.addCSourceFiles(.{
        .root = b.path("src"),
        .files = &app_sources,
        .flags = &.{
            "-std=gnu23",
            "-Wall",
            "-Wextra",
            "-Wno-unused-parameter",
            "-Wno-missing-field-initializers",
            "-fno-strict-aliasing",
            b.fmt("--embed-dir={s}", .{b.pathFromRoot("vendor/ghostty/src/font/res")}),
        },
    });
    for (wayland_includes.code) |code| mod.addCSourceFile(.{ .file = code, .flags = &.{"-std=gnu11"} });
    mod.addIncludePath(wayland_includes.dir);
    mod.addIncludePath(b.path("vendor/wayland/src"));
    mod.addIncludePath(b.path("vendor/wayland/egl"));
    mod.addIncludePath(wayland_includes.version_dir);
    mod.addIncludePath(b.path("vendor/khronos"));
    mod.addIncludePath(b.path("vendor/libxkbcommon/include"));
    mod.addCMacro("GHOSTTY_STATIC", "");
    mod.addCMacro("WL_EGL_PLATFORM", "1");
    mod.addCMacro("_GNU_SOURCE", "1");
    mod.linkLibrary(ghostty.artifact("ghostty-vt-static"));
    mod.linkLibrary(freetype.artifact("freetype"));
    mod.linkLibrary(fontconfig.artifact("fontconfig"));
    mod.linkLibrary(libpng.artifact("png"));
    mod.linkLibrary(xkbcommon);
    mod.linkSystemLibrary("dl", .{});
    mod.linkSystemLibrary("m", .{});

    const exe = b.addExecutable(.{ .name = "ghostty-panel", .root_module = mod });
    b.installArtifact(exe);

    const run = b.addRunArtifact(exe);
    run.step.dependOn(b.getInstallStep());
    if (b.args) |args| run.addArgs(args);
    b.step("run", "Run ghostty-panel").dependOn(&run.step);
}

fn buildScanner(b: *std.Build) *std.Build.Step.Compile {
    const host = b.graph.host;
    const mod = b.createModule(.{ .target = host, .optimize = .ReleaseSafe, .link_libc = true });

    const expat_config = b.addConfigHeader(.{ .style = .blank, .include_path = "expat_config.h" }, .{
        .BYTEORDER = @as(i64, if (host.result.cpu.arch.endian() == .little) 1234 else 4321),
        .HAVE_FCNTL_H = 1,
        .HAVE_GETPAGESIZE = 1,
        .HAVE_GETRANDOM = 1,
        .HAVE_MMAP = 1,
        .HAVE_STDINT_H = 1,
        .HAVE_STDLIB_H = 1,
        .HAVE_STRING_H = 1,
        .HAVE_SYSCALL_GETRANDOM = 1,
        .HAVE_UNISTD_H = 1,
        .XML_CONTEXT_BYTES = 1024,
        .XML_DEV_URANDOM = 1,
        .XML_DTD = 1,
        .XML_GE = 1,
        .XML_NS = 1,
    });
    const scanner_config = b.addConfigHeader(.{ .style = .blank, .include_path = "config.h" }, .{
        .HAVE_STRNDUP = 1,
        .PACKAGE_VERSION = "1.23.90",
    });
    const version = waylandVersionHeader(b);

    mod.addConfigHeader(expat_config);
    mod.addConfigHeader(scanner_config);
    mod.addIncludePath(version.getOutputDir());
    mod.addIncludePath(b.path("vendor/expat/lib"));
    mod.addIncludePath(b.path("vendor/wayland/src"));
    mod.addIncludePath(b.path("vendor/wayland/protocol"));
    mod.addCSourceFiles(.{ .root = b.path("vendor/expat"), .files = &expat_sources, .flags = &.{"-DXML_ENABLE_VISIBILITY=0"} });
    const wrappers = b.addWriteFiles();
    for ([_][]const u8{ "scanner.c", "wayland-util.c" }) |file| {
        const wrapper = wrappers.add(b.fmt("wrap-{s}", .{file}), b.fmt("#include \"config.h\"\n#include \"{s}\"\n", .{file}));
        mod.addCSourceFile(.{ .file = wrapper, .flags = &.{"-D_GNU_SOURCE"} });
    }
    return b.addExecutable(.{ .name = "wayland-scanner", .root_module = mod });
}

fn waylandVersionHeader(b: *std.Build) *std.Build.Step.ConfigHeader {
    return b.addConfigHeader(.{
        .style = .{ .autoconf_at = b.path("vendor/wayland/src/wayland-version.h.in") },
        .include_path = "wayland-version.h",
    }, .{
        .WAYLAND_VERSION_MAJOR = 1,
        .WAYLAND_VERSION_MINOR = 23,
        .WAYLAND_VERSION_MICRO = 90,
        .WAYLAND_VERSION = "1.23.90",
    });
}

const WaylandIncludes = struct {
    dir: std.Build.LazyPath,
    version_dir: std.Build.LazyPath,
    code: []const std.Build.LazyPath,
};

fn generateProtocols(b: *std.Build, scanner: *std.Build.Step.Compile) WaylandIncludes {
    const headers = b.addWriteFiles();
    var code: [protocols.len]std.Build.LazyPath = undefined;
    for (protocols, 0..) |p, i| {
        const header_name = b.fmt("{s}-client-protocol.h", .{p.name});
        const gen_header = b.addRunArtifact(scanner);
        gen_header.addArg("client-header");
        gen_header.addFileArg(b.path(p.xml));
        const header = gen_header.addOutputFileArg(header_name);
        _ = headers.addCopyFile(header, header_name);

        const gen_code = b.addRunArtifact(scanner);
        gen_code.addArg("private-code");
        gen_code.addFileArg(b.path(p.xml));
        code[i] = gen_code.addOutputFileArg(b.fmt("{s}-protocol.c", .{p.name}));
    }
    return .{
        .dir = headers.getDirectory(),
        .version_dir = waylandVersionHeader(b).getOutputDir(),
        .code = b.allocator.dupe(std.Build.LazyPath, &code) catch @panic("OOM"),
    };
}

fn buildXkbcommon(b: *std.Build, target: std.Build.ResolvedTarget, optimize: std.builtin.OptimizeMode) *std.Build.Step.Compile {
    const mod = b.createModule(.{ .target = target, .optimize = optimize, .link_libc = true });
    const config = b.addConfigHeader(.{ .style = .blank, .include_path = "config.h" }, .{
        ._GNU_SOURCE = 1,
        .EXIT_INVALID_USAGE = 2,
        .LIBXKBCOMMON_VERSION = "1.11.0",
        .LIBXKBCOMMON_TOOL_PATH = "/usr/libexec/xkbcommon",
        .DFLT_XKB_CONFIG_ROOT = "/usr/share/X11/xkb",
        .DFLT_XKB_CONFIG_EXTRA_PATH = "/etc/xkb",
        .XLOCALEDIR = "/usr/share/X11/locale",
        .DEFAULT_XKB_RULES = "evdev",
        .DEFAULT_XKB_MODEL = "pc105",
        .DEFAULT_XKB_LAYOUT = "us",
        .DEFAULT_XKB_VARIANT = .NULL,
        .DEFAULT_XKB_OPTIONS = .NULL,
        .HAVE_UNISTD_H = 1,
        .HAVE___BUILTIN_EXPECT = 1,
        .HAVE_EACCESS = 1,
        .HAVE_EUIDACCESS = 1,
        .HAVE_MMAP = 1,
        .HAVE_MKOSTEMP = 1,
        .HAVE_POSIX_FALLOCATE = 1,
        .HAVE_STRNDUP = 1,
        .HAVE_ASPRINTF = 1,
        .HAVE_VASPRINTF = 1,
        .HAVE_OPEN_MEMSTREAM = 1,
        .HAVE_SECURE_GETENV = 1,
    });
    mod.addConfigHeader(config);
    mod.addIncludePath(b.path("vendor/libxkbcommon"));
    mod.addIncludePath(b.path("vendor/libxkbcommon/src"));
    mod.addIncludePath(b.path("vendor/libxkbcommon/include"));
    mod.addIncludePath(b.path("vendor/libxkbcommon/generated"));
    mod.addCSourceFiles(.{
        .root = b.path("vendor/libxkbcommon"),
        .files = &xkbcommon_sources,
        .flags = &.{ "-std=gnu11", "-fno-strict-aliasing", "-fvisibility=hidden" },
    });
    return b.addLibrary(.{ .name = "xkbcommon", .root_module = mod, .linkage = .static });
}
