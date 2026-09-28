// src/app.c
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <unistd.h>

#include "app.h"

#include <EGL/eglext.h>
#include "json.h"
#include "rc.h"

static const struct wl_output_listener output_listener;
static const struct zxdg_output_v1_listener xdg_output_listener;

static void output_geometry(void *data, struct wl_output *o, int32_t x, int32_t y, int32_t pw, int32_t ph, int32_t subpixel, const char *make,
                            const char *model, int32_t transform)
{
}

static void output_mode(void *data, struct wl_output *o, uint32_t flags, int32_t width, int32_t height, int32_t refresh)
{
    Output *out = data;
    if (flags & WL_OUTPUT_MODE_CURRENT) {
        out->width = width;
        out->height = height;
    }
}

static void output_done(void *data, struct wl_output *o)
{
    Output *out = data;
    for (size_t i = 0; i < out->app->npanels; i++) panel_outputs_changed(out->app->panels[i]);
}

static void output_scale(void *data, struct wl_output *o, int32_t factor)
{
    Output *out = data;
    out->scale = factor;
}

static void set_string(char **dst, const char *value)
{
    free(*dst);
    *dst = xstrdup(value);
}

static void output_name(void *data, struct wl_output *o, const char *name)
{
    set_string(&((Output *)data)->name, name);
}

static void output_description(void *data, struct wl_output *o, const char *description)
{
    set_string(&((Output *)data)->description, description);
}

static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
    .name = output_name,
    .description = output_description,
};

static void xdg_output_logical_position(void *data, struct zxdg_output_v1 *x, int32_t px, int32_t py)
{
}

static void xdg_output_logical_size(void *data, struct zxdg_output_v1 *x, int32_t w, int32_t h)
{
    Output *out = data;
    out->logical_width = w;
    out->logical_height = h;
}

static void xdg_output_done(void *data, struct zxdg_output_v1 *x)
{
}

static void xdg_output_name(void *data, struct zxdg_output_v1 *x, const char *name)
{
    Output *out = data;
    if (!out->name) set_string(&out->name, name);
}

static void xdg_output_description(void *data, struct zxdg_output_v1 *x, const char *description)
{
    Output *out = data;
    if (!out->description) set_string(&out->description, description);
}

static const struct zxdg_output_v1_listener xdg_output_listener = {
    .logical_position = xdg_output_logical_position,
    .logical_size = xdg_output_logical_size,
    .done = xdg_output_done,
    .name = xdg_output_name,
    .description = xdg_output_description,
};

static void attach_xdg_output(App *app, Output *o)
{
    if (!app->xdg_output_manager || o->xdg) return;
    o->xdg = zxdg_output_manager_v1_get_xdg_output(app->xdg_output_manager, o->wl);
    zxdg_output_v1_add_listener(o->xdg, &xdg_output_listener, o);
}

static void free_output(Output *o)
{
    if (o->xdg) zxdg_output_v1_destroy(o->xdg);
    if (wl_output_get_version(o->wl) >= WL_OUTPUT_RELEASE_SINCE_VERSION) wl_output_release(o->wl);
    else wl_output_destroy(o->wl);
    free(o->name);
    free(o->description);
    free(o);
}

typedef struct {
    const char *interface;
    uint32_t max_version;
    size_t offset;
} GlobalSpec;

static const GlobalSpec global_specs[] = {
    {"wl_compositor", 6, offsetof(App, compositor)},
    {"zwlr_layer_shell_v1", 5, offsetof(App, layer_shell)},
    {"wp_fractional_scale_manager_v1", 1, offsetof(App, fractional_scale)},
    {"wp_viewporter", 1, offsetof(App, viewporter)},
    {"wp_cursor_shape_manager_v1", 1, offsetof(App, cursor_shape)},
    {"zwp_keyboard_shortcuts_inhibit_manager_v1", 1, offsetof(App, shortcuts_inhibit)},
    {"zxdg_output_manager_v1", 3, offsetof(App, xdg_output_manager)},
};

static const struct wl_interface *const global_interfaces[] = {
    &wl_compositor_interface,
    &zwlr_layer_shell_v1_interface,
    &wp_fractional_scale_manager_v1_interface,
    &wp_viewporter_interface,
    &wp_cursor_shape_manager_v1_interface,
    &zwp_keyboard_shortcuts_inhibit_manager_v1_interface,
    &zxdg_output_manager_v1_interface,
};

static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    App *app = data;
    for (size_t i = 0; i < ARRAY_LEN(global_specs); i++) {
        if (strcmp(interface, global_specs[i].interface) != 0) continue;
        void **slot = (void **)((char *)app + global_specs[i].offset);
        if (!*slot) *slot = wl_registry_bind(registry, name, global_interfaces[i], MIN(version, global_specs[i].max_version));
        for (size_t k = 0; k < app->noutputs; k++) attach_xdg_output(app, app->outputs[k]);
        return;
    }
    if (strcmp(interface, "wl_output") == 0) {
        Output *o = xcalloc(1, sizeof(Output));
        o->app = app;
        o->global = name;
        o->scale = 1;
        o->wl = wl_registry_bind(registry, name, &wl_output_interface, MIN(version, 4u));
        wl_output_add_listener(o->wl, &output_listener, o);
        attach_xdg_output(app, o);
        app->outputs = xrealloc(app->outputs, (app->noutputs + 1) * sizeof(Output *));
        app->outputs[app->noutputs++] = o;
    } else if (strcmp(interface, "wl_seat") == 0 && !app->input.seat) {
        input_bind_seat(&app->input, wl_registry_bind(registry, name, &wl_seat_interface, MIN(version, 9u)));
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    App *app = data;
    for (size_t i = 0; i < app->noutputs; i++) {
        Output *o = app->outputs[i];
        if (o->global != name) continue;
        for (size_t k = 0; k < app->npanels; k++) {
            Panel *p = app->panels[k];
            for (size_t e = 0; e < p->nentered; e++)
                if (p->entered[e] == o) p->entered[e--] = p->entered[--p->nentered];
        }
        free_output(o);
        app->outputs[i] = app->outputs[--app->noutputs];
        return;
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

bool app_connect(App *app, char *err, size_t errlen)
{
    app->display = wl_display_connect(NULL);
    if (!app->display) {
        snprintf(err, errlen, "cannot connect to the Wayland display, is WAYLAND_DISPLAY set?");
        return false;
    }
    input_init(&app->input, app);
    app->registry = wl_display_get_registry(app->display);
    wl_registry_add_listener(app->registry, &registry_listener, app);
    wl_display_roundtrip(app->display);
    wl_display_roundtrip(app->display);
    if (!app->compositor) {
        snprintf(err, errlen, "the compositor does not provide wl_compositor");
        return false;
    }
    if (!app->layer_shell) {
        snprintf(err, errlen, "the compositor does not support the wlr-layer-shell protocol, panels cannot be shown");
        return false;
    }
    return true;
}

bool app_init_egl(App *app, char *err, size_t errlen)
{
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    app->egl_display = get_platform_display ? get_platform_display(EGL_PLATFORM_WAYLAND_KHR, app->display, NULL)
                                            : eglGetDisplay((EGLNativeDisplayType)app->display);
    if (app->egl_display == EGL_NO_DISPLAY || !eglInitialize(app->egl_display, NULL, NULL)) {
        snprintf(err, errlen, "cannot initialize EGL: 0x%x", eglGetError());
        return false;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        snprintf(err, errlen, "EGL does not support OpenGL ES");
        return false;
    }
    const EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE,
    };
    EGLConfig configs[64];
    EGLint count = 0;
    if (!eglChooseConfig(app->egl_display, attribs, configs, 64, &count) || count == 0) {
        snprintf(err, errlen, "no suitable EGL config with an alpha channel");
        return false;
    }
    app->egl_config = configs[0];
    for (EGLint i = 0; i < count; i++) {
        EGLint r = 0, g = 0, b = 0, a = 0;
        eglGetConfigAttrib(app->egl_display, configs[i], EGL_RED_SIZE, &r);
        eglGetConfigAttrib(app->egl_display, configs[i], EGL_GREEN_SIZE, &g);
        eglGetConfigAttrib(app->egl_display, configs[i], EGL_BLUE_SIZE, &b);
        eglGetConfigAttrib(app->egl_display, configs[i], EGL_ALPHA_SIZE, &a);
        if (r == 8 && g == 8 && b == 8 && a == 8) {
            app->egl_config = configs[i];
            break;
        }
    }
    const EGLint ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    app->egl_context = eglCreateContext(app->egl_display, app->egl_config, EGL_NO_CONTEXT, ctx_attribs);
    if (app->egl_context == EGL_NO_CONTEXT) {
        snprintf(err, errlen, "cannot create an OpenGL ES 2 context: 0x%x", eglGetError());
        return false;
    }
    struct wl_surface *probe = wl_compositor_create_surface(app->compositor);
    struct wl_egl_window *win = wl_egl_window_create(probe, 1, 1);
    EGLSurface surf = eglCreateWindowSurface(app->egl_display, app->egl_config, (EGLNativeWindowType)win, NULL);
    bool ok = surf != EGL_NO_SURFACE && eglMakeCurrent(app->egl_display, surf, surf, app->egl_context);
    if (ok) ok = renderer_init_gl(err, errlen);
    else snprintf(err, errlen, "cannot make the OpenGL ES context current: 0x%x", eglGetError());
    eglMakeCurrent(app->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, app->egl_context);
    if (surf != EGL_NO_SURFACE) eglDestroySurface(app->egl_display, surf);
    wl_egl_window_destroy(win);
    wl_surface_destroy(probe);
    log_debug(debug_rendering, "EGL %s, %s", eglQueryString(app->egl_display, EGL_VENDOR), eglQueryString(app->egl_display, EGL_VERSION));
    return ok;
}

static void bury_panels(App *app)
{
    for (size_t i = 0; i < app->ngraveyard; i++) panel_free(app->graveyard[i]);
    app->ngraveyard = 0;
}

void app_disconnect(App *app)
{
    while (app->npanels) app_remove_panel(app, app->panels[app->npanels - 1]);
    bury_panels(app);
    free(app->graveyard);
    free(app->panels);
    rc_server_free(app->rc);
    app->rc = NULL;
    if (app->egl_display != EGL_NO_DISPLAY && app->egl_display) {
        eglMakeCurrent(app->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (app->egl_context) eglDestroyContext(app->egl_display, app->egl_context);
        eglTerminate(app->egl_display);
    }
    if (app->display) {
        input_destroy(&app->input);
        for (size_t i = 0; i < app->noutputs; i++) free_output(app->outputs[i]);
        free(app->outputs);
        for (size_t i = 0; i < ARRAY_LEN(global_specs); i++) {
            struct wl_proxy **slot = (struct wl_proxy **)((char *)app + global_specs[i].offset);
            if (*slot) wl_proxy_destroy(*slot);
        }
        if (app->registry) wl_registry_destroy(app->registry);
        wl_display_flush(app->display);
        wl_display_disconnect(app->display);
    }
    free(app->watches);
    free(app->listen_on);
    if (app->signal_fd > 0) close(app->signal_fd);
    config_free(&app->cfg);
}

void app_watch(App *app, int fd, short events, WatchFn fn, void *ctx)
{
    if (app->nwatches == app->watches_cap) {
        app->watches_cap = app->watches_cap ? app->watches_cap * 2 : 16;
        app->watches = xrealloc(app->watches, app->watches_cap * sizeof(Watch));
    }
    app->watches[app->nwatches++] = (Watch){.fd = fd, .events = events, .fn = fn, .ctx = ctx};
}

Panel *app_create_panel(App *app, const PanelSpec *spec, char *err, size_t errlen)
{
    Panel *p = panel_new(app, spec, err, errlen);
    if (!p) return NULL;
    app->panels = xrealloc(app->panels, (app->npanels + 1) * sizeof(Panel *));
    app->panels[app->npanels++] = p;
    app->active = p;
    return p;
}

void app_remove_panel(App *app, Panel *p)
{
    for (size_t i = 0; i < app->npanels; i++) {
        if (app->panels[i] != p) continue;
        memmove(app->panels + i, app->panels + i + 1, (app->npanels - i - 1) * sizeof(Panel *));
        app->npanels--;
        if (app->active == p) app->active = NULL;
        if (p->on_close) p->on_close(p->on_close_ctx, p);
        p->on_close = NULL;
        panel_hide(p);
        p->closing = true;
        app->graveyard = xrealloc(app->graveyard, (app->ngraveyard + 1) * sizeof(Panel *));
        app->graveyard[app->ngraveyard++] = p;
        if (!app->npanels) app->running = false;
        return;
    }
}

Panel *app_find_panel(App *app, uint64_t id)
{
    for (size_t i = 0; i < app->npanels; i++)
        if (app->panels[i]->id == id) return app->panels[i];
    return NULL;
}

Panel *app_panel_for_surface(App *app, struct wl_surface *surface)
{
    if (!surface) return NULL;
    for (size_t i = 0; i < app->npanels; i++)
        if (app->panels[i]->surface == surface) return app->panels[i];
    return NULL;
}

Output *app_find_output(App *app, const char *name)
{
    if (!name || !*name) return NULL;
    for (size_t i = 0; i < app->noutputs; i++)
        if (app->outputs[i]->name && strcmp(app->outputs[i]->name, name) == 0) return app->outputs[i];
    return NULL;
}

Output *app_output_for_wl(App *app, struct wl_output *wl)
{
    for (size_t i = 0; i < app->noutputs; i++)
        if (app->outputs[i]->wl == wl) return app->outputs[i];
    return NULL;
}

static void reap_children(App *app)
{
    for (;;) {
        int status = 0;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0) return;
        int code = WIFEXITED(status) ? WEXITSTATUS(status) : WIFSIGNALED(status) ? 128 + WTERMSIG(status) : status;
        for (size_t i = 0; i < app->npanels; i++) {
            if (app->panels[i]->pid == pid) {
                panel_child_exited(app->panels[i], code);
                break;
            }
        }
    }
}

static void on_signal(void *ctx, int fd, short revents)
{
    App *app = ctx;
    struct signalfd_siginfo info;
    while (read(fd, &info, sizeof info) == (ssize_t)sizeof info) {
        if (info.ssi_signo == SIGCHLD) {
            reap_children(app);
        } else {
            log_debug(debug_rendering, "received signal %u, exiting", info.ssi_signo);
            app->running = false;
        }
    }
}

static int setup_signals(void)
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGHUP);
    sigprocmask(SIG_BLOCK, &set, NULL);
    signal(SIGPIPE, SIG_IGN);
    return signalfd(-1, &set, SFD_NONBLOCK | SFD_CLOEXEC);
}

int app_run(App *app)
{
    app->signal_fd = setup_signals();
    app->running = app->npanels > 0;
    reap_children(app);
    struct pollfd *fds = NULL;
    size_t fds_cap = 0;
    while (app->running) {
        bury_panels(app);
        uint64_t now = now_ms();
        input_tick(&app->input, now);
        for (size_t i = 0; i < app->npanels; i++) panel_tick(app->panels[i], now);
        for (size_t i = 0; i < app->npanels; i++) panel_render(app->panels[i]);
        if (!app->running) break;

        while (wl_display_prepare_read(app->display) != 0) wl_display_dispatch_pending(app->display);
        if (wl_display_flush(app->display) < 0 && errno != EAGAIN) {
            wl_display_cancel_read(app->display);
            log_msg("lost the connection to the Wayland compositor");
            app->exit_code = 1;
            break;
        }

        app->nwatches = 0;
        app_watch(app, wl_display_get_fd(app->display), POLLIN, NULL, app);
        app_watch(app, app->signal_fd, POLLIN, on_signal, app);
        for (size_t i = 0; i < app->npanels; i++) panel_collect_watches(app->panels[i]);
        if (app->rc) rc_server_collect_watches(app->rc);

        uint64_t deadline = input_next_deadline(&app->input);
        for (size_t i = 0; i < app->npanels; i++) deadline = MIN(deadline, panel_next_deadline(app->panels[i]));
        int timeout = deadline == UINT64_MAX ? -1 : (int)MIN(deadline > now ? deadline - now : 0, 60000u);

        if (fds_cap < app->nwatches) {
            fds_cap = app->nwatches * 2;
            fds = xrealloc(fds, fds_cap * sizeof(struct pollfd));
        }
        for (size_t i = 0; i < app->nwatches; i++) fds[i] = (struct pollfd){.fd = app->watches[i].fd, .events = app->watches[i].events};
        int n = poll(fds, app->nwatches, timeout);
        if (n < 0 && errno != EINTR) {
            wl_display_cancel_read(app->display);
            log_msg("poll failed: %s", strerror(errno));
            app->exit_code = 1;
            break;
        }
        if (n > 0 && (fds[0].revents & POLLIN)) {
            if (wl_display_read_events(app->display) != 0) {
                log_msg("Wayland error: %s", strerror(errno));
                app->exit_code = 1;
                break;
            }
        } else {
            wl_display_cancel_read(app->display);
            if (n > 0 && (fds[0].revents & (POLLERR | POLLHUP))) {
                log_msg("lost the connection to the Wayland compositor");
                app->exit_code = 1;
                break;
            }
        }
        for (size_t i = 1; i < app->nwatches && n > 0; i++)
            if (fds[i].revents) app->watches[i].fn(app->watches[i].ctx, fds[i].fd, fds[i].revents);
        if (wl_display_dispatch_pending(app->display) < 0) {
            log_msg("Wayland protocol error: %s", strerror(wl_display_get_error(app->display)));
            app->exit_code = 1;
            break;
        }
    }
    free(fds);
    return app->exit_code;
}

int app_list_outputs(bool json)
{
    App app = {0};
    config_defaults(&app.cfg);
    app.display = wl_display_connect(NULL);
    if (!app.display) {
        fprintf(stderr, "ghostty-panel: cannot connect to the Wayland display\n");
        config_free(&app.cfg);
        return 1;
    }
    input_init(&app.input, &app);
    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);
    wl_display_roundtrip(app.display);
    wl_display_roundtrip(app.display);
    Buf out = {0};
    if (json) buf_append_str(&out, "[");
    for (size_t i = 0; i < app.noutputs; i++) {
        Output *o = app.outputs[i];
        const char *name = o->name ? o->name : "";
        const char *desc = o->description ? o->description : "";
        if (json) {
            buf_append_str(&out, i ? ",{\"name\":" : "{\"name\":");
            json_write_string(&out, name);
            buf_append_str(&out, ",\"description\":");
            json_write_string(&out, desc);
            buf_appendf(&out, ",\"width\":%d,\"height\":%d,\"scale\":%d,\"logical_width\":%d,\"logical_height\":%d}", o->width, o->height,
                        o->scale, o->logical_width, o->logical_height);
        } else {
            buf_appendf(&out, "%s: %s (%dx%d, scale %d)\n", name, desc, o->width, o->height, o->scale);
        }
    }
    if (json) buf_append_str(&out, "]\n");
    fputs(buf_cstr(&out), stdout);
    buf_free(&out);
    app_disconnect(&app);
    return 0;
}
