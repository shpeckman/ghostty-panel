// src/dynload.c
#include "dynload.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#define DYNLOAD_LIBRARIES(X)                          \
    X(LIB_WAYLAND_CLIENT, "libwayland-client.so.0") \
    X(LIB_WAYLAND_EGL, "libwayland-egl.so.1")       \
    X(LIB_EGL, "libEGL.so.1")                       \
    X(LIB_GLES, "libGLESv2.so.2")

#define DYNLOAD_SYMBOLS(X)                            \
    X(LIB_WAYLAND_CLIENT, wl_display_connect)         \
    X(LIB_WAYLAND_CLIENT, wl_display_disconnect)      \
    X(LIB_WAYLAND_CLIENT, wl_display_get_fd)          \
    X(LIB_WAYLAND_CLIENT, wl_display_get_error)       \
    X(LIB_WAYLAND_CLIENT, wl_display_dispatch)        \
    X(LIB_WAYLAND_CLIENT, wl_display_dispatch_pending) \
    X(LIB_WAYLAND_CLIENT, wl_display_flush)           \
    X(LIB_WAYLAND_CLIENT, wl_display_roundtrip)       \
    X(LIB_WAYLAND_CLIENT, wl_display_prepare_read)    \
    X(LIB_WAYLAND_CLIENT, wl_display_read_events)     \
    X(LIB_WAYLAND_CLIENT, wl_display_cancel_read)     \
    X(LIB_WAYLAND_CLIENT, wl_proxy_marshal_flags)     \
    X(LIB_WAYLAND_CLIENT, wl_proxy_add_listener)      \
    X(LIB_WAYLAND_CLIENT, wl_proxy_destroy)           \
    X(LIB_WAYLAND_CLIENT, wl_proxy_get_version)       \
    X(LIB_WAYLAND_CLIENT, wl_proxy_set_user_data)     \
    X(LIB_WAYLAND_CLIENT, wl_proxy_get_user_data)     \
    X(LIB_WAYLAND_EGL, wl_egl_window_create)          \
    X(LIB_WAYLAND_EGL, wl_egl_window_destroy)         \
    X(LIB_WAYLAND_EGL, wl_egl_window_resize)          \
    X(LIB_EGL, eglGetProcAddress)                     \
    X(LIB_EGL, eglGetDisplay)                         \
    X(LIB_EGL, eglInitialize)                         \
    X(LIB_EGL, eglTerminate)                          \
    X(LIB_EGL, eglBindAPI)                            \
    X(LIB_EGL, eglChooseConfig)                       \
    X(LIB_EGL, eglGetConfigAttrib)                    \
    X(LIB_EGL, eglCreateContext)                      \
    X(LIB_EGL, eglDestroyContext)                     \
    X(LIB_EGL, eglCreateWindowSurface)                \
    X(LIB_EGL, eglDestroySurface)                     \
    X(LIB_EGL, eglMakeCurrent)                        \
    X(LIB_EGL, eglSwapBuffers)                        \
    X(LIB_EGL, eglSwapInterval)                       \
    X(LIB_EGL, eglGetError)                           \
    X(LIB_EGL, eglQueryString)                        \
    X(LIB_GLES, glActiveTexture)                      \
    X(LIB_GLES, glAttachShader)                       \
    X(LIB_GLES, glBindAttribLocation)                 \
    X(LIB_GLES, glBindBuffer)                         \
    X(LIB_GLES, glBindTexture)                        \
    X(LIB_GLES, glBlendFunc)                          \
    X(LIB_GLES, glBufferData)                         \
    X(LIB_GLES, glClear)                              \
    X(LIB_GLES, glClearColor)                         \
    X(LIB_GLES, glCompileShader)                      \
    X(LIB_GLES, glCreateProgram)                      \
    X(LIB_GLES, glCreateShader)                       \
    X(LIB_GLES, glDeleteBuffers)                      \
    X(LIB_GLES, glDeleteProgram)                      \
    X(LIB_GLES, glDeleteShader)                       \
    X(LIB_GLES, glDeleteTextures)                     \
    X(LIB_GLES, glDisable)                            \
    X(LIB_GLES, glDrawElements)                       \
    X(LIB_GLES, glEnable)                             \
    X(LIB_GLES, glEnableVertexAttribArray)            \
    X(LIB_GLES, glGenBuffers)                         \
    X(LIB_GLES, glGenTextures)                        \
    X(LIB_GLES, glGetIntegerv)                        \
    X(LIB_GLES, glGetProgramInfoLog)                  \
    X(LIB_GLES, glGetProgramiv)                       \
    X(LIB_GLES, glGetShaderInfoLog)                   \
    X(LIB_GLES, glGetShaderiv)                        \
    X(LIB_GLES, glGetUniformLocation)                 \
    X(LIB_GLES, glLinkProgram)                        \
    X(LIB_GLES, glPixelStorei)                        \
    X(LIB_GLES, glShaderSource)                       \
    X(LIB_GLES, glTexImage2D)                         \
    X(LIB_GLES, glTexParameteri)                      \
    X(LIB_GLES, glTexSubImage2D)                      \
    X(LIB_GLES, glUniform1i)                          \
    X(LIB_GLES, glUniform2f)                          \
    X(LIB_GLES, glUseProgram)                         \
    X(LIB_GLES, glVertexAttribPointer)                \
    X(LIB_GLES, glViewport)

enum {
#define X(id, file) id,
    DYNLOAD_LIBRARIES(X)
#undef X
        LIB_COUNT
};

static const char *const library_files[LIB_COUNT] = {
#define X(id, file) [id] = file,
    DYNLOAD_LIBRARIES(X)
#undef X
};

#if defined(__x86_64__)
#define TRAMPOLINE_BODY(name) "\tjmp *dynload_slot_" #name "(%rip)\n"
#elif defined(__aarch64__)
#define TRAMPOLINE_BODY(name)                           \
    "\tadrp x16, dynload_slot_" #name "\n"              \
    "\tldr x16, [x16, :lo12:dynload_slot_" #name "]\n" \
    "\tbr x16\n"
#else
#error "unsupported architecture"
#endif

#define X(lib, name)                                                   \
    __attribute__((visibility("hidden"), used)) void *dynload_slot_##name; \
    __asm__(".text\n"                                                  \
            ".globl " #name "\n"                                       \
            ".type " #name ", %function\n"                             \
            ".p2align 4\n" #name ":\n" TRAMPOLINE_BODY(name) ".size " #name ", .-" #name "\n");
DYNLOAD_SYMBOLS(X)
#undef X

typedef struct {
    int library;
    const char *name;
    void **slot;
} Symbol;

static const Symbol symbols[] = {
#define X(lib, sym) {lib, #sym, &dynload_slot_##sym},
    DYNLOAD_SYMBOLS(X)
#undef X
};

bool dynload_init(char *err, size_t errlen)
{
    void *handles[LIB_COUNT] = {0};
    for (int i = 0; i < LIB_COUNT; i++) {
        handles[i] = dlopen(library_files[i], RTLD_NOW | RTLD_GLOBAL);
        if (!handles[i]) {
            snprintf(err, errlen, "cannot load %s: %s", library_files[i], dlerror());
            return false;
        }
    }
    for (size_t i = 0; i < sizeof(symbols) / sizeof(symbols[0]); i++) {
        *symbols[i].slot = dlsym(handles[symbols[i].library], symbols[i].name);
        if (!*symbols[i].slot) {
            snprintf(err, errlen, "%s does not provide %s", library_files[symbols[i].library], symbols[i].name);
            return false;
        }
    }
    return true;
}
