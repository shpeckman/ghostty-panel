// src/app.h
#pragma once

#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include <EGL/egl.h>
#include <ghostty/vt.h>
#include <wayland-client.h>
#include <wayland-egl-core.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon.h>

#include "cli.h"
#include "config.h"
#include "cursor-shape-v1-client-protocol.h"
#include "font.h"
#include "fractional-scale-v1-client-protocol.h"
#include "keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h"
#include "render.h"
#include "util.h"
#include "viewporter-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"

typedef struct App App;
typedef struct Panel Panel;
typedef struct RcServer RcServer;

typedef void (*WatchFn)(void *ctx, int fd, short revents);

typedef struct {
    int fd;
    short events;
    WatchFn fn;
    void *ctx;
} Watch;

typedef struct {
    App *app;
    struct wl_output *wl;
    struct zxdg_output_v1 *xdg;
    uint32_t global;
    char *name;
    char *description;
    int32_t scale;
    int32_t width;
    int32_t height;
    int32_t logical_width;
    int32_t logical_height;
} Output;

typedef struct {
    App *app;
    struct wl_seat *seat;
    struct wl_keyboard *keyboard;
    struct wl_pointer *pointer;
    struct wp_cursor_shape_device_v1 *cursor_device;
    struct xkb_context *xkb;
    struct xkb_keymap *keymap;
    struct xkb_state *state;
    struct xkb_compose_table *compose_table;
    struct xkb_compose_state *compose;
    xkb_mod_index_t mod_shift;
    xkb_mod_index_t mod_ctrl;
    xkb_mod_index_t mod_alt;
    xkb_mod_index_t mod_super;
    xkb_mod_index_t mod_caps;
    xkb_mod_index_t mod_num;
    Panel *keyboard_focus;
    Panel *pointer_focus;
    int32_t repeat_rate;
    int32_t repeat_delay;
    uint32_t repeat_key;
    uint64_t repeat_next;
    double pointer_x;
    double pointer_y;
    uint32_t buttons;
    double scroll_v;
    double scroll_h;
    int32_t scroll_v120;
    int32_t scroll_h120;
    bool scroll_discrete;
} Input;

typedef struct {
    LayerConfig lsc;
    const char *app_id;
    const char *name;
    const StrList *argv;
    const char *cwd;
    const StrList *env;
    const Config *cfg;
    bool hold;
    bool start_hidden;
    bool grab_keyboard;
} PanelSpec;

struct Panel {
    App *app;
    uint64_t id;
    Config cfg;
    LayerConfig lsc;
    char *app_id;
    char *name;
    bool hold;
    bool grab_keyboard;
    bool visible;
    bool closing;

    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer;
    struct wp_fractional_scale_v1 *fractional;
    struct wp_viewport *viewport;
    struct zwp_keyboard_shortcuts_inhibitor_v1 *inhibitor;
    struct wl_callback *frame_cb;
    struct wl_egl_window *egl_window;
    EGLSurface egl_surface;
    bool configured;
    uint32_t width;
    uint32_t height;
    uint32_t requested_width;
    uint32_t requested_height;
    int32_t buffer_scale;
    double scale;
    int buf_w;
    int buf_h;
    Output *entered[16];
    size_t nentered;

    Fonts *fonts;
    Renderer *renderer;
    int pad_x;
    int pad_y;
    uint16_t cols;
    uint16_t rows;

    GhosttyTerminal term;
    GhosttyRenderState render_state;
    GhosttyRenderStateRowIterator row_iter;
    GhosttyRenderStateRowCells row_cells;
    GhosttyKittyGraphicsPlacementIterator placements;
    GhosttyKeyEncoder key_encoder;
    GhosttyKeyEvent key_event;
    GhosttyMouseEncoder mouse_encoder;
    GhosttyMouseEvent mouse_event;

    int pty_fd;
    pid_t pid;
    bool pty_eof;
    bool child_exited;
    Buf pty_out;

    bool dirty;
    bool focused;
    bool blink_on;
    uint64_t blink_next;

    void (*on_close)(void *ctx, Panel *p);
    void *on_close_ctx;
};

struct App {
    Config cfg;
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct wp_fractional_scale_manager_v1 *fractional_scale;
    struct wp_viewporter *viewporter;
    struct wp_cursor_shape_manager_v1 *cursor_shape;
    struct zwp_keyboard_shortcuts_inhibit_manager_v1 *shortcuts_inhibit;
    struct zxdg_output_manager_v1 *xdg_output_manager;
    Output **outputs;
    size_t noutputs;
    Input input;

    EGLDisplay egl_display;
    EGLConfig egl_config;
    EGLContext egl_context;

    Panel **panels;
    size_t npanels;
    Panel **graveyard;
    size_t ngraveyard;
    uint64_t next_panel_id;
    Panel *active;

    RcServer *rc;
    char *listen_on;
    int signal_fd;
    Watch *watches;
    size_t nwatches;
    size_t watches_cap;
    bool running;
    bool persistent;
    int exit_code;
};

bool app_connect(App *app, char *err, size_t errlen);
bool app_init_egl(App *app, char *err, size_t errlen);
void app_disconnect(App *app);
int app_run(App *app);
void app_watch(App *app, int fd, short events, WatchFn fn, void *ctx);
Panel *app_create_panel(App *app, const PanelSpec *spec, char *err, size_t errlen);
void app_remove_panel(App *app, Panel *p);
Panel *app_find_panel(App *app, uint64_t id);
Panel *app_panel_for_surface(App *app, struct wl_surface *surface);
Output *app_find_output(App *app, const char *name);
Output *app_output_for_wl(App *app, struct wl_output *wl);
int app_list_outputs(bool json);

Panel *panel_new(App *app, const PanelSpec *spec, char *err, size_t errlen);
void panel_free(Panel *p);
void panel_show(Panel *p);
void panel_hide(Panel *p);
void panel_toggle(Panel *p);
void panel_set_layer_config(Panel *p, const LayerConfig *lsc);
void panel_collect_watches(Panel *p);
void panel_child_exited(Panel *p, int status);
void panel_render(Panel *p);
void panel_write(Panel *p, const void *data, size_t len);
void panel_set_focus(Panel *p, bool focused);
void panel_outputs_changed(Panel *p);
uint64_t panel_next_deadline(Panel *p);
void panel_tick(Panel *p, uint64_t now);
void panel_scroll_to_bottom(Panel *p);
bool panel_mode(Panel *p, GhosttyMode mode);

void input_init(Input *in, App *app);
void input_bind_seat(Input *in, struct wl_seat *seat);
void input_destroy(Input *in);
void input_panel_gone(Input *in, Panel *p);
uint64_t input_next_deadline(Input *in);
void input_tick(Input *in, uint64_t now);
