// src/panel.c
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app.h"
#include "pty.h"
#include "rc.h"

static const struct wl_surface_listener surface_listener;
static const struct zwlr_layer_surface_v1_listener layer_listener;
static const struct wp_fractional_scale_v1_listener fractional_listener;
static const struct wl_callback_listener frame_listener;

static void effect_write_pty(GhosttyTerminal t, void *userdata, const uint8_t *data, size_t len)
{
    panel_write(userdata, data, len);
}

static bool effect_size(GhosttyTerminal t, void *userdata, GhosttySizeReportSize *out)
{
    Panel *p = userdata;
    const FontMetrics *m = fonts_metrics(p->fonts);
    out->rows = p->rows;
    out->columns = p->cols;
    out->cell_width = (uint32_t)m->cell_w;
    out->cell_height = (uint32_t)m->cell_h;
    return true;
}

static bool effect_device_attributes(GhosttyTerminal t, void *userdata, GhosttyDeviceAttributes *out)
{
    out->primary.conformance_level = GHOSTTY_DA_CONFORMANCE_VT220;
    out->primary.features[0] = GHOSTTY_DA_FEATURE_COLUMNS_132;
    out->primary.features[1] = GHOSTTY_DA_FEATURE_SELECTIVE_ERASE;
    out->primary.features[2] = GHOSTTY_DA_FEATURE_ANSI_COLOR;
    out->primary.num_features = 3;
    out->secondary.device_type = GHOSTTY_DA_DEVICE_TYPE_VT220;
    out->secondary.firmware_version = 1;
    out->secondary.rom_cartridge = 0;
    out->tertiary.unit_id = 0;
    return true;
}

static GhosttyString effect_xtversion(GhosttyTerminal t, void *userdata)
{
    static const char name[] = "ghostty-panel";
    return (GhosttyString){.ptr = (const uint8_t *)name, .len = sizeof name - 1};
}

static bool effect_color_scheme(GhosttyTerminal t, void *userdata, GhosttyColorScheme *out)
{
    return false;
}

static double initial_scale(App *app, const LayerConfig *lsc)
{
    Output *o = app_find_output(app, lsc->output_name);
    if (!o && app->noutputs) o = app->outputs[0];
    return o && o->scale > 0 ? o->scale : 1.0;
}

static int padding_px(const Panel *p)
{
    return (int)lround(MAX(0.0, p->cfg.window_padding_width) * 96.0 * p->scale / 72.0);
}

static void configure_terminal(Panel *p)
{
    const Config *c = &p->cfg;
    ghostty_terminal_set(p->term, GHOSTTY_TERMINAL_OPT_USERDATA, p);
    ghostty_terminal_set(p->term, GHOSTTY_TERMINAL_OPT_WRITE_PTY, (const void *)effect_write_pty);
    ghostty_terminal_set(p->term, GHOSTTY_TERMINAL_OPT_SIZE, (const void *)effect_size);
    ghostty_terminal_set(p->term, GHOSTTY_TERMINAL_OPT_DEVICE_ATTRIBUTES, (const void *)effect_device_attributes);
    ghostty_terminal_set(p->term, GHOSTTY_TERMINAL_OPT_XTVERSION, (const void *)effect_xtversion);
    ghostty_terminal_set(p->term, GHOSTTY_TERMINAL_OPT_COLOR_SCHEME, (const void *)effect_color_scheme);
    ghostty_terminal_set(p->term, GHOSTTY_TERMINAL_OPT_COLOR_FOREGROUND, &c->foreground);
    ghostty_terminal_set(p->term, GHOSTTY_TERMINAL_OPT_COLOR_BACKGROUND, &c->background);
    ghostty_terminal_set(p->term, GHOSTTY_TERMINAL_OPT_COLOR_PALETTE, c->palette);
    if (c->cursor.special == COLOR_SPECIAL_NONE) ghostty_terminal_set(p->term, GHOSTTY_TERMINAL_OPT_COLOR_CURSOR, &c->cursor.rgb);
    static const GhosttyTerminalCursorStyle shapes[] = {
        [CURSOR_BLOCK] = GHOSTTY_TERMINAL_CURSOR_STYLE_BLOCK,
        [CURSOR_BEAM] = GHOSTTY_TERMINAL_CURSOR_STYLE_BAR,
        [CURSOR_UNDERLINE] = GHOSTTY_TERMINAL_CURSOR_STYLE_UNDERLINE,
    };
    GhosttyTerminalCursorStyle shape = shapes[c->cursor_shape];
    bool blink = c->cursor_blink_interval > 0;
    ghostty_terminal_set(p->term, GHOSTTY_TERMINAL_OPT_DEFAULT_CURSOR_STYLE, &shape);
    ghostty_terminal_set(p->term, GHOSTTY_TERMINAL_OPT_DEFAULT_CURSOR_BLINK, &blink);
    size_t scrollback = (size_t)MAX(0, c->scrollback_lines);
    ghostty_terminal_set(p->term, GHOSTTY_TERMINAL_OPT_SCROLLBACK_MAX_LINES, &scrollback);
    GhosttyString term = {.ptr = (const uint8_t *)c->term, .len = strlen(c->term)};
    ghostty_terminal_set(p->term, GHOSTTY_TERMINAL_OPT_TERMINFO_NAME, &term);
    uint64_t storage = 64ull * 1024 * 1024;
    bool enabled = true;
    ghostty_terminal_set(p->term, GHOSTTY_TERMINAL_OPT_KITTY_IMAGE_STORAGE_LIMIT, &storage);
    ghostty_terminal_set(p->term, GHOSTTY_TERMINAL_OPT_KITTY_IMAGE_MEDIUM_FILE, &enabled);
    const char *tmp = getenv("TMPDIR");
    if (!tmp || !*tmp) tmp = "/tmp";
    GhosttyString tmp_dir = {.ptr = (const uint8_t *)tmp, .len = strlen(tmp)};
    ghostty_terminal_set(p->term, GHOSTTY_TERMINAL_OPT_KITTY_IMAGE_MEDIUM_TEMP_FILE, &tmp_dir);
    ghostty_terminal_set(p->term, GHOSTTY_TERMINAL_OPT_KITTY_IMAGE_MEDIUM_SHARED_MEM, &enabled);
}

static void calculate_size(Panel *p, uint32_t *width, uint32_t *height)
{
    const LayerConfig *c = &p->lsc;
    const FontMetrics *m = fonts_metrics(p->fonts);
    Output *o = app_find_output(p->app, c->output_name);
    if (!o && p->nentered) o = p->entered[0];
    if (!o && p->app->noutputs) o = p->app->outputs[0];
    int32_t mon_w = o ? (o->logical_width ? o->logical_width : o->width / MAX(1, o->scale)) : 0;
    int32_t mon_h = o ? (o->logical_height ? o->logical_height : o->height / MAX(1, o->scale)) : 0;
    mon_w = MAX(0, mon_w - c->margin_left - c->margin_right);
    mon_h = MAX(0, mon_h - c->margin_top - c->margin_bottom);
    double s = p->scale;
    double spacing = 2.0 * padding_px(p) / s;
    if (c->layer == LAYER_BACKGROUND || c->edge == EDGE_BACKGROUND || c->edge == EDGE_CENTER) {
        if (!*width) *width = (uint32_t)mon_w;
        if (!*height) *height = (uint32_t)mon_h;
        return;
    }
    double xsz = c->x_px ? c->x_px * s : (double)m->cell_w * c->x_cells;
    double ysz = c->y_px ? c->y_px * s : (double)m->cell_h * c->y_cells;
    if (c->edge == EDGE_LEFT || c->edge == EDGE_RIGHT) {
        if (!*height) *height = (uint32_t)mon_h;
        *width = (uint32_t)(1.0 + spacing + xsz / s);
    } else if (c->edge == EDGE_TOP || c->edge == EDGE_BOTTOM) {
        if (!*width) *width = (uint32_t)mon_w;
        *height = (uint32_t)(1.0 + spacing + ysz / s);
    } else {
        *width = (uint32_t)(1.0 + spacing + xsz / s);
        *height = (uint32_t)(1.0 + spacing + ysz / s);
    }
}

static enum zwlr_layer_shell_v1_layer wl_layer(const LayerConfig *c)
{
    static const enum zwlr_layer_shell_v1_layer layers[] = {
        [LAYER_BACKGROUND] = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
        [LAYER_BOTTOM] = ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
        [LAYER_TOP] = ZWLR_LAYER_SHELL_V1_LAYER_TOP,
        [LAYER_OVERLAY] = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
    };
    return c->edge == EDGE_BACKGROUND ? ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND : layers[c->layer];
}

static void layer_set_properties(Panel *p, bool during_creation, uint32_t width, uint32_t height)
{
    if (!p->layer) return;
    const LayerConfig *c = &p->lsc;
    const uint32_t all = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                         ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    uint32_t anchor = all;
    int32_t exclusive = c->exclusive_zone;
    uint32_t pw = 0, ph = 0;
    static const enum zwlr_layer_surface_v1_keyboard_interactivity focus[] = {
        [FOCUS_NOT_ALLOWED] = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE,
        [FOCUS_EXCLUSIVE] = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE,
        [FOCUS_ON_DEMAND] = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND,
    };
    enum zwlr_layer_surface_v1_keyboard_interactivity interactivity = focus[c->focus_policy];
    if (interactivity == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND &&
        zwlr_layer_surface_v1_get_version(p->layer) < ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND_SINCE_VERSION)
        interactivity = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE;
    if (wl_layer(c) == ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND) {
        exclusive = -1;
    } else {
        switch (c->edge) {
        case EDGE_TOP:
            anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
            ph = height;
            if (!c->override_exclusive_zone) exclusive = (int32_t)height;
            break;
        case EDGE_BOTTOM:
            anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
            ph = height;
            if (!c->override_exclusive_zone) exclusive = (int32_t)height;
            break;
        case EDGE_LEFT:
            anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
            pw = width;
            if (!c->override_exclusive_zone) exclusive = (int32_t)width;
            break;
        case EDGE_RIGHT:
            anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT | ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
            pw = width;
            if (!c->override_exclusive_zone) exclusive = (int32_t)width;
            break;
        case EDGE_CENTER_SIZED:
            anchor = 0;
            pw = width;
            ph = height;
            break;
        case EDGE_NONE:
            anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
            pw = width;
            ph = height;
            break;
        case EDGE_CENTER:
        case EDGE_BACKGROUND:
            break;
        }
    }
    p->requested_width = pw;
    p->requested_height = ph;
    zwlr_layer_surface_v1_set_size(p->layer, pw, ph);
    zwlr_layer_surface_v1_set_anchor(p->layer, anchor);
    zwlr_layer_surface_v1_set_exclusive_zone(p->layer, exclusive);
    zwlr_layer_surface_v1_set_margin(p->layer, c->margin_top, c->margin_right, c->margin_bottom, c->margin_left);
    if (!during_creation && zwlr_layer_surface_v1_get_version(p->layer) >= ZWLR_LAYER_SURFACE_V1_SET_LAYER_SINCE_VERSION)
        zwlr_layer_surface_v1_set_layer(p->layer, wl_layer(c));
    zwlr_layer_surface_v1_set_keyboard_interactivity(p->layer, interactivity);
    log_debug(debug_rendering, "panel %llu: layer size %ux%u anchor %u exclusive %d", (unsigned long long)p->id, pw, ph, anchor, exclusive);
}

static bool spawn_child(Panel *p, const StrList *argv, const char *cwd, const StrList *env, char *err, size_t errlen)
{
    StrList args = {0};
    if (argv && argv->len) {
        strlist_copy(&args, argv);
    } else {
        strlist_push_owned(&args, default_shell(p->cfg.shell));
    }
    StrList vars = {0};
    if (env) strlist_copy(&vars, env);
    strlist_push_owned(&vars, xasprintf("TERM=%s", p->cfg.term));
    strlist_push(&vars, "COLORTERM=truecolor");
    strlist_push(&vars, "TERM_PROGRAM=ghostty-panel");
    strlist_push_owned(&vars, xasprintf("GHOSTTY_PANEL_WINDOW_ID=%llu", (unsigned long long)p->id));
    if (p->app->listen_on) strlist_push_owned(&vars, xasprintf("GHOSTTY_PANEL_LISTEN_ON=%s", p->app->listen_on));
    const FontMetrics *m = fonts_metrics(p->fonts);
    PtySpawn spec = {
        .argv = args.items,
        .cwd = cwd,
        .env = &vars,
        .cols = p->cols,
        .rows = p->rows,
        .width_px = (uint32_t)(p->cols * m->cell_w),
        .height_px = (uint32_t)(p->rows * m->cell_h),
    };
    p->pty_fd = pty_spawn(&spec, &p->pid, err, errlen);
    strlist_clear(&args);
    strlist_clear(&vars);
    return p->pty_fd >= 0;
}

static void update_grid(Panel *p)
{
    const FontMetrics *m = fonts_metrics(p->fonts);
    int pad = padding_px(p);
    int cols = (p->buf_w - 2 * pad) / m->cell_w;
    int rows = (p->buf_h - 2 * pad) / m->cell_h;
    cols = CLAMP(cols, 1, 65535);
    rows = CLAMP(rows, 1, 65535);
    p->pad_x = pad;
    p->pad_y = pad;
    if (cols == p->cols && rows == p->rows) {
        ghostty_terminal_resize(p->term, p->cols, p->rows, (uint32_t)m->cell_w, (uint32_t)m->cell_h);
        return;
    }
    p->cols = (uint16_t)cols;
    p->rows = (uint16_t)rows;
    ghostty_terminal_resize(p->term, p->cols, p->rows, (uint32_t)m->cell_w, (uint32_t)m->cell_h);
    if (p->pty_fd >= 0) pty_resize(p->pty_fd, p->cols, p->rows, (uint32_t)(cols * m->cell_w), (uint32_t)(rows * m->cell_h));
    log_debug(debug_rendering, "panel %llu: grid %dx%d cell %dx%d", (unsigned long long)p->id, cols, rows, m->cell_w, m->cell_h);
}

static bool uses_viewport(const Panel *p)
{
    return p->fractional && p->viewport;
}

static void apply_buffer_size(Panel *p)
{
    if (uses_viewport(p)) {
        p->buf_w = MAX(1, (int)lround(p->width * p->scale));
        p->buf_h = MAX(1, (int)lround(p->height * p->scale));
    } else {
        int32_t s = MAX(1, (int32_t)lround(p->scale));
        p->buf_w = (int)(p->width * (uint32_t)s);
        p->buf_h = (int)(p->height * (uint32_t)s);
    }
    if (p->egl_window) wl_egl_window_resize(p->egl_window, p->buf_w, p->buf_h, 0, 0);
    update_grid(p);
    p->dirty = true;
}

static void set_scale(Panel *p, double scale)
{
    if (scale <= 0 || fabs(scale - p->scale) < 1e-6) return;
    char err[256];
    Fonts *fonts = fonts_new(&p->cfg, scale, err, sizeof err);
    if (!fonts) {
        log_msg("%s", err);
        return;
    }
    fonts_free(p->fonts);
    p->fonts = fonts;
    p->scale = scale;
    if (p->renderer) renderer_reset_glyphs(p->renderer);
    log_debug(debug_rendering, "panel %llu: scale %.3f", (unsigned long long)p->id, scale);
    if (!p->layer) return;
    uint32_t w = p->width, h = p->height;
    calculate_size(p, &w, &h);
    layer_set_properties(p, false, w, h);
    if (p->configured) {
        p->width = w;
        p->height = h;
        apply_buffer_size(p);
    } else {
        wl_surface_commit(p->surface);
    }
}

static void recompute_integer_scale(Panel *p)
{
    if (p->app->fractional_scale) return;
    int32_t s = p->buffer_scale;
    if (!s) {
        s = 1;
        for (size_t i = 0; i < p->nentered; i++) s = MAX(s, p->entered[i]->scale);
    }
    set_scale(p, s);
}

static void ensure_egl_surface(Panel *p)
{
    if (p->egl_surface) return;
    p->egl_window = wl_egl_window_create(p->surface, p->buf_w, p->buf_h);
    p->egl_surface = eglCreateWindowSurface(p->app->egl_display, p->app->egl_config, (EGLNativeWindowType)p->egl_window, NULL);
    if (p->egl_surface == EGL_NO_SURFACE) {
        log_msg("eglCreateWindowSurface failed: 0x%x", eglGetError());
        wl_egl_window_destroy(p->egl_window);
        p->egl_window = NULL;
        return;
    }
    eglMakeCurrent(p->app->egl_display, p->egl_surface, p->egl_surface, p->app->egl_context);
    eglSwapInterval(p->app->egl_display, 0);
}

static void layer_configure(void *data, struct zwlr_layer_surface_v1 *layer, uint32_t serial, uint32_t width, uint32_t height)
{
    Panel *p = data;
    log_debug(debug_rendering, "panel %llu: configure %ux%u", (unsigned long long)p->id, width, height);
    calculate_size(p, &width, &height);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    bool first = !p->configured;
    p->configured = true;
    if (first || width != p->width || height != p->height) {
        p->width = MAX(1u, width);
        p->height = MAX(1u, height);
        apply_buffer_size(p);
        bool fixed_w = p->requested_width != 0, fixed_h = p->requested_height != 0;
        if ((fixed_w && p->requested_width != p->width) || (fixed_h && p->requested_height != p->height))
            layer_set_properties(p, false, p->width, p->height);
    }
    if (first) ensure_egl_surface(p);
    p->dirty = true;
    panel_render(p);
}

static void layer_closed(void *data, struct zwlr_layer_surface_v1 *layer)
{
    Panel *p = data;
    log_debug(debug_rendering, "panel %llu: closed by compositor", (unsigned long long)p->id);
    app_remove_panel(p->app, p);
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
    .configure = layer_configure,
    .closed = layer_closed,
};

static void fractional_preferred_scale(void *data, struct wp_fractional_scale_v1 *f, uint32_t scale)
{
    Panel *p = data;
    set_scale(p, scale / 120.0);
}

static const struct wp_fractional_scale_v1_listener fractional_listener = {
    .preferred_scale = fractional_preferred_scale,
};

static void surface_enter(void *data, struct wl_surface *surface, struct wl_output *output)
{
    Panel *p = data;
    Output *o = app_output_for_wl(p->app, output);
    if (!o || p->nentered >= ARRAY_LEN(p->entered)) return;
    p->entered[p->nentered++] = o;
    recompute_integer_scale(p);
}

static void surface_leave(void *data, struct wl_surface *surface, struct wl_output *output)
{
    Panel *p = data;
    for (size_t i = 0; i < p->nentered; i++) {
        if (p->entered[i]->wl == output) {
            p->entered[i] = p->entered[--p->nentered];
            break;
        }
    }
    recompute_integer_scale(p);
}

static void surface_preferred_buffer_scale(void *data, struct wl_surface *surface, int32_t factor)
{
    Panel *p = data;
    p->buffer_scale = factor;
    recompute_integer_scale(p);
}

static void surface_preferred_buffer_transform(void *data, struct wl_surface *surface, uint32_t transform)
{
}

static const struct wl_surface_listener surface_listener = {
    .enter = surface_enter,
    .leave = surface_leave,
    .preferred_buffer_scale = surface_preferred_buffer_scale,
    .preferred_buffer_transform = surface_preferred_buffer_transform,
};

static void frame_done(void *data, struct wl_callback *cb, uint32_t time)
{
    Panel *p = data;
    wl_callback_destroy(cb);
    p->frame_cb = NULL;
    panel_render(p);
}

static const struct wl_callback_listener frame_listener = {
    .done = frame_done,
};

Panel *panel_new(App *app, const PanelSpec *spec, char *err, size_t errlen)
{
    Panel *p = xcalloc(1, sizeof(Panel));
    p->app = app;
    p->id = ++app->next_panel_id;
    config_copy(&p->cfg, spec->cfg);
    p->lsc = spec->lsc;
    p->app_id = xstrdup(spec->app_id && *spec->app_id ? spec->app_id : "ghostty-panel");
    p->name = spec->name && *spec->name ? xstrdup(spec->name) : NULL;
    p->hold = spec->hold;
    p->grab_keyboard = spec->grab_keyboard;
    p->pty_fd = -1;
    p->blink_on = true;
    p->scale = initial_scale(app, &p->lsc);
    p->fonts = fonts_new(&p->cfg, p->scale, err, errlen);
    if (!p->fonts) {
        panel_free(p);
        return NULL;
    }
    const FontMetrics *m = fonts_metrics(p->fonts);
    uint32_t w = 0, h = 0;
    calculate_size(p, &w, &h);
    p->buf_w = (int)lround(MAX(w, 1u) * p->scale);
    p->buf_h = (int)lround(MAX(h, 1u) * p->scale);
    p->cols = (uint16_t)CLAMP(p->buf_w / m->cell_w, 1, 65535);
    p->rows = (uint16_t)CLAMP(p->buf_h / m->cell_h, 1, 65535);
    if (ghostty_terminal_new(NULL, &p->term, p->cols, p->rows) != GHOSTTY_SUCCESS ||
        ghostty_render_state_new(NULL, &p->render_state) != GHOSTTY_SUCCESS ||
        ghostty_render_state_row_iterator_new(NULL, &p->row_iter) != GHOSTTY_SUCCESS ||
        ghostty_render_state_row_cells_new(NULL, &p->row_cells) != GHOSTTY_SUCCESS ||
        ghostty_kitty_graphics_placement_iterator_new(NULL, &p->placements) != GHOSTTY_SUCCESS ||
        ghostty_key_encoder_new(NULL, &p->key_encoder) != GHOSTTY_SUCCESS ||
        ghostty_key_event_new(NULL, &p->key_event) != GHOSTTY_SUCCESS ||
        ghostty_mouse_encoder_new(NULL, &p->mouse_encoder) != GHOSTTY_SUCCESS ||
        ghostty_mouse_event_new(NULL, &p->mouse_event) != GHOSTTY_SUCCESS) {
        snprintf(err, errlen, "cannot create the terminal");
        panel_free(p);
        return NULL;
    }
    configure_terminal(p);
    ghostty_terminal_resize(p->term, p->cols, p->rows, (uint32_t)m->cell_w, (uint32_t)m->cell_h);
    if (!spawn_child(p, spec->argv, spec->cwd, spec->env, err, errlen)) {
        panel_free(p);
        return NULL;
    }
    if (!spec->start_hidden) panel_show(p);
    return p;
}

static void destroy_surface(Panel *p)
{
    if (p->frame_cb) wl_callback_destroy(p->frame_cb);
    p->frame_cb = NULL;
    if (p->egl_surface) {
        eglMakeCurrent(p->app->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, p->app->egl_context);
        eglDestroySurface(p->app->egl_display, p->egl_surface);
    }
    p->egl_surface = EGL_NO_SURFACE;
    if (p->egl_window) wl_egl_window_destroy(p->egl_window);
    p->egl_window = NULL;
    if (p->inhibitor) zwp_keyboard_shortcuts_inhibitor_v1_destroy(p->inhibitor);
    p->inhibitor = NULL;
    if (p->viewport) wp_viewport_destroy(p->viewport);
    p->viewport = NULL;
    if (p->fractional) wp_fractional_scale_v1_destroy(p->fractional);
    p->fractional = NULL;
    if (p->layer) zwlr_layer_surface_v1_destroy(p->layer);
    p->layer = NULL;
    if (p->surface) wl_surface_destroy(p->surface);
    p->surface = NULL;
    p->configured = false;
    p->nentered = 0;
    p->buffer_scale = 0;
    input_panel_gone(&p->app->input, p);
}

void panel_show(Panel *p)
{
    p->visible = true;
    if (p->surface) return;
    App *app = p->app;
    p->surface = wl_compositor_create_surface(app->compositor);
    wl_surface_add_listener(p->surface, &surface_listener, p);
    if (app->fractional_scale) {
        p->fractional = wp_fractional_scale_manager_v1_get_fractional_scale(app->fractional_scale, p->surface);
        wp_fractional_scale_v1_add_listener(p->fractional, &fractional_listener, p);
    }
    if (app->viewporter && app->fractional_scale) p->viewport = wp_viewporter_get_viewport(app->viewporter, p->surface);
    Output *o = app_find_output(app, p->lsc.output_name);
    if (!o && p->lsc.output_name[0]) log_msg("no output named %s, letting the compositor choose", p->lsc.output_name);
    p->layer = zwlr_layer_shell_v1_get_layer_surface(app->layer_shell, p->surface, o ? o->wl : NULL, wl_layer(&p->lsc), p->app_id);
    zwlr_layer_surface_v1_add_listener(p->layer, &layer_listener, p);
    uint32_t w = 0, h = 0;
    calculate_size(p, &w, &h);
    layer_set_properties(p, true, w, h);
    if (p->grab_keyboard && app->shortcuts_inhibit && app->input.seat)
        p->inhibitor = zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(app->shortcuts_inhibit, p->surface, app->input.seat);
    wl_surface_commit(p->surface);
    log_debug(debug_rendering, "panel %llu: mapped", (unsigned long long)p->id);
}

void panel_hide(Panel *p)
{
    p->visible = false;
    if (!p->surface) return;
    if (p->focused) panel_set_focus(p, false);
    destroy_surface(p);
    log_debug(debug_rendering, "panel %llu: unmapped", (unsigned long long)p->id);
}

void panel_toggle(Panel *p)
{
    if (p->visible) panel_hide(p);
    else panel_show(p);
}

void panel_set_layer_config(Panel *p, const LayerConfig *lsc)
{
    bool output_changed = strcmp(lsc->output_name, p->lsc.output_name) != 0;
    p->lsc = *lsc;
    if (!p->layer) return;
    if (output_changed) {
        destroy_surface(p);
        panel_show(p);
        return;
    }
    uint32_t w = 0, h = 0;
    calculate_size(p, &w, &h);
    layer_set_properties(p, false, w, h);
    wl_surface_commit(p->surface);
}

void panel_free(Panel *p)
{
    if (!p) return;
    p->closing = true;
    destroy_surface(p);
    if (p->pid > 0 && !p->child_exited) kill(p->pid, SIGHUP);
    if (p->pty_fd >= 0) close(p->pty_fd);
    if (p->renderer) {
        eglMakeCurrent(p->app->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, p->app->egl_context);
        renderer_free(p->renderer);
    }
    if (p->placements) ghostty_kitty_graphics_placement_iterator_free(p->placements);
    if (p->mouse_event) ghostty_mouse_event_free(p->mouse_event);
    if (p->mouse_encoder) ghostty_mouse_encoder_free(p->mouse_encoder);
    if (p->key_event) ghostty_key_event_free(p->key_event);
    if (p->key_encoder) ghostty_key_encoder_free(p->key_encoder);
    if (p->row_cells) ghostty_render_state_row_cells_free(p->row_cells);
    if (p->row_iter) ghostty_render_state_row_iterator_free(p->row_iter);
    if (p->render_state) ghostty_render_state_free(p->render_state);
    if (p->term) ghostty_terminal_free(p->term);
    fonts_free(p->fonts);
    buf_free(&p->pty_out);
    config_free(&p->cfg);
    free(p->app_id);
    free(p->name);
    free(p);
}

static void flush_pty(Panel *p)
{
    while (p->pty_out.len && p->pty_fd >= 0) {
        ssize_t n = write(p->pty_fd, p->pty_out.data, p->pty_out.len);
        if (n > 0) {
            buf_consume(&p->pty_out, (size_t)n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && errno != EAGAIN) buf_consume(&p->pty_out, p->pty_out.len);
        break;
    }
}

void panel_write(Panel *p, const void *data, size_t len)
{
    if (p->pty_fd < 0 || p->child_exited || !len) return;
    buf_append(&p->pty_out, data, len);
    flush_pty(p);
}

static void read_pty(Panel *p)
{
    uint8_t buf[65536];
    size_t total = 0;
    while (p->pty_fd >= 0 && total < 4u * 1024 * 1024) {
        ssize_t n = read(p->pty_fd, buf, sizeof buf);
        if (n > 0) {
            ghostty_terminal_vt_write(p->term, buf, (size_t)n);
            total += (size_t)n;
            p->dirty = true;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && errno == EAGAIN) break;
        p->pty_eof = true;
        break;
    }
}

static void on_pty(void *ctx, int fd, short revents)
{
    Panel *p = ctx;
    if (p->closing) return;
    if (revents & POLLOUT) flush_pty(p);
    if (revents & (POLLIN | POLLHUP | POLLERR)) read_pty(p);
}

void panel_collect_watches(Panel *p)
{
    if (p->pty_fd < 0 || p->pty_eof) return;
    app_watch(p->app, p->pty_fd, (short)(POLLIN | (p->pty_out.len ? POLLOUT : 0)), on_pty, p);
}

void panel_child_exited(Panel *p, int status)
{
    p->child_exited = true;
    if (p->pty_fd >= 0 && !p->pty_eof) read_pty(p);
    log_debug(debug_rendering, "panel %llu: child exited with status %d", (unsigned long long)p->id, status);
    if (!p->hold) app_remove_panel(p->app, p);
}

void panel_render(Panel *p)
{
    if (!p->dirty || !p->configured || !p->egl_surface || p->frame_cb || p->closing) return;
    App *app = p->app;
    if (!eglMakeCurrent(app->egl_display, p->egl_surface, p->egl_surface, app->egl_context)) {
        log_msg("eglMakeCurrent failed: 0x%x", eglGetError());
        return;
    }
    if (!p->renderer) p->renderer = renderer_new();
    ghostty_render_state_update(p->render_state, p->term);
    RenderFrame frame = {
        .terminal = p->term,
        .state = p->render_state,
        .rows = p->row_iter,
        .cells = p->row_cells,
        .placements = p->placements,
        .fonts = p->fonts,
        .cfg = &p->cfg,
        .width = p->buf_w,
        .height = p->buf_h,
        .pad_x = p->pad_x,
        .pad_y = p->pad_y,
        .focused = p->focused,
        .cursor_blink_on = p->blink_on,
    };
    renderer_draw(p->renderer, &frame);
    if (uses_viewport(p)) wp_viewport_set_destination(p->viewport, (int32_t)p->width, (int32_t)p->height);
    else wl_surface_set_buffer_scale(p->surface, MAX(1, (int32_t)lround(p->scale)));
    p->frame_cb = wl_surface_frame(p->surface);
    wl_callback_add_listener(p->frame_cb, &frame_listener, p);
    if (!eglSwapBuffers(app->egl_display, p->egl_surface)) log_msg("eglSwapBuffers failed: 0x%x", eglGetError());
    p->dirty = false;
    log_debug(debug_rendering, "panel %llu: frame %dx%d", (unsigned long long)p->id, p->buf_w, p->buf_h);
}

bool panel_mode(Panel *p, GhosttyMode mode)
{
    GhosttyTerminalModeConfig cfg = {.mode = mode, .value = false};
    return ghostty_terminal_get(p->term, GHOSTTY_TERMINAL_DATA_MODE, &cfg) == GHOSTTY_SUCCESS && cfg.value;
}

void panel_set_focus(Panel *p, bool focused)
{
    if (p->focused == focused) return;
    p->focused = focused;
    p->dirty = true;
    p->blink_on = true;
    p->blink_next = now_ms() + (uint64_t)(p->cfg.cursor_blink_interval * 1000.0);
    if (focused) p->app->active = p;
    if (panel_mode(p, GHOSTTY_MODE_FOCUS_EVENT)) {
        char buf[8];
        size_t n = 0;
        if (ghostty_focus_encode(focused ? GHOSTTY_FOCUS_GAINED : GHOSTTY_FOCUS_LOST, buf, sizeof buf, &n) == GHOSTTY_SUCCESS)
            panel_write(p, buf, n);
    }
    if (!focused && p->lsc.hide_on_focus_loss) panel_hide(p);
}

void panel_outputs_changed(Panel *p)
{
    recompute_integer_scale(p);
}

uint64_t panel_next_deadline(Panel *p)
{
    if (!p->focused || p->cfg.cursor_blink_interval <= 0 || !p->configured) return UINT64_MAX;
    return p->blink_next;
}

void panel_tick(Panel *p, uint64_t now)
{
    if (panel_next_deadline(p) > now) return;
    p->blink_on = !p->blink_on;
    p->blink_next = now + (uint64_t)(p->cfg.cursor_blink_interval * 1000.0);
    p->dirty = true;
}

void panel_scroll_to_bottom(Panel *p)
{
    bool active = true;
    if (ghostty_terminal_get(p->term, GHOSTTY_TERMINAL_DATA_VIEWPORT_ACTIVE, &active) == GHOSTTY_SUCCESS && !active) {
        ghostty_terminal_scroll_viewport(p->term, (GhosttyTerminalScrollViewport){.tag = GHOSTTY_SCROLL_VIEWPORT_BOTTOM});
        p->dirty = true;
    }
}
