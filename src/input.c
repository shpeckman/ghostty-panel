// src/input.c
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "app.h"

static const GhosttyKey evdev_keys[] = {
    [KEY_1] = GHOSTTY_KEY_DIGIT_1,
    [KEY_2] = GHOSTTY_KEY_DIGIT_2,
    [KEY_3] = GHOSTTY_KEY_DIGIT_3,
    [KEY_4] = GHOSTTY_KEY_DIGIT_4,
    [KEY_5] = GHOSTTY_KEY_DIGIT_5,
    [KEY_6] = GHOSTTY_KEY_DIGIT_6,
    [KEY_7] = GHOSTTY_KEY_DIGIT_7,
    [KEY_8] = GHOSTTY_KEY_DIGIT_8,
    [KEY_9] = GHOSTTY_KEY_DIGIT_9,
    [KEY_0] = GHOSTTY_KEY_DIGIT_0,
    [KEY_MINUS] = GHOSTTY_KEY_MINUS,
    [KEY_EQUAL] = GHOSTTY_KEY_EQUAL,
    [KEY_Q] = GHOSTTY_KEY_Q,
    [KEY_W] = GHOSTTY_KEY_W,
    [KEY_E] = GHOSTTY_KEY_E,
    [KEY_R] = GHOSTTY_KEY_R,
    [KEY_T] = GHOSTTY_KEY_T,
    [KEY_Y] = GHOSTTY_KEY_Y,
    [KEY_U] = GHOSTTY_KEY_U,
    [KEY_I] = GHOSTTY_KEY_I,
    [KEY_O] = GHOSTTY_KEY_O,
    [KEY_P] = GHOSTTY_KEY_P,
    [KEY_LEFTBRACE] = GHOSTTY_KEY_BRACKET_LEFT,
    [KEY_RIGHTBRACE] = GHOSTTY_KEY_BRACKET_RIGHT,
    [KEY_A] = GHOSTTY_KEY_A,
    [KEY_S] = GHOSTTY_KEY_S,
    [KEY_D] = GHOSTTY_KEY_D,
    [KEY_F] = GHOSTTY_KEY_F,
    [KEY_G] = GHOSTTY_KEY_G,
    [KEY_H] = GHOSTTY_KEY_H,
    [KEY_J] = GHOSTTY_KEY_J,
    [KEY_K] = GHOSTTY_KEY_K,
    [KEY_L] = GHOSTTY_KEY_L,
    [KEY_SEMICOLON] = GHOSTTY_KEY_SEMICOLON,
    [KEY_APOSTROPHE] = GHOSTTY_KEY_QUOTE,
    [KEY_GRAVE] = GHOSTTY_KEY_BACKQUOTE,
    [KEY_BACKSLASH] = GHOSTTY_KEY_BACKSLASH,
    [KEY_Z] = GHOSTTY_KEY_Z,
    [KEY_X] = GHOSTTY_KEY_X,
    [KEY_C] = GHOSTTY_KEY_C,
    [KEY_V] = GHOSTTY_KEY_V,
    [KEY_B] = GHOSTTY_KEY_B,
    [KEY_N] = GHOSTTY_KEY_N,
    [KEY_M] = GHOSTTY_KEY_M,
    [KEY_COMMA] = GHOSTTY_KEY_COMMA,
    [KEY_DOT] = GHOSTTY_KEY_PERIOD,
    [KEY_SLASH] = GHOSTTY_KEY_SLASH,
    [KEY_SPACE] = GHOSTTY_KEY_SPACE,
    [KEY_102ND] = GHOSTTY_KEY_INTL_BACKSLASH,
    [KEY_RO] = GHOSTTY_KEY_INTL_RO,
    [KEY_YEN] = GHOSTTY_KEY_INTL_YEN,
};

static const GhosttyKey ascii_keys[128] = {
    [' '] = GHOSTTY_KEY_SPACE,
    ['\''] = GHOSTTY_KEY_QUOTE,
    [','] = GHOSTTY_KEY_COMMA,
    ['-'] = GHOSTTY_KEY_MINUS,
    ['.'] = GHOSTTY_KEY_PERIOD,
    ['/'] = GHOSTTY_KEY_SLASH,
    ['0'] = GHOSTTY_KEY_DIGIT_0,
    ['1'] = GHOSTTY_KEY_DIGIT_1,
    ['2'] = GHOSTTY_KEY_DIGIT_2,
    ['3'] = GHOSTTY_KEY_DIGIT_3,
    ['4'] = GHOSTTY_KEY_DIGIT_4,
    ['5'] = GHOSTTY_KEY_DIGIT_5,
    ['6'] = GHOSTTY_KEY_DIGIT_6,
    ['7'] = GHOSTTY_KEY_DIGIT_7,
    ['8'] = GHOSTTY_KEY_DIGIT_8,
    ['9'] = GHOSTTY_KEY_DIGIT_9,
    [';'] = GHOSTTY_KEY_SEMICOLON,
    ['='] = GHOSTTY_KEY_EQUAL,
    ['['] = GHOSTTY_KEY_BRACKET_LEFT,
    ['\\'] = GHOSTTY_KEY_BACKSLASH,
    [']'] = GHOSTTY_KEY_BRACKET_RIGHT,
    ['`'] = GHOSTTY_KEY_BACKQUOTE,
    ['a'] = GHOSTTY_KEY_A,
    ['b'] = GHOSTTY_KEY_B,
    ['c'] = GHOSTTY_KEY_C,
    ['d'] = GHOSTTY_KEY_D,
    ['e'] = GHOSTTY_KEY_E,
    ['f'] = GHOSTTY_KEY_F,
    ['g'] = GHOSTTY_KEY_G,
    ['h'] = GHOSTTY_KEY_H,
    ['i'] = GHOSTTY_KEY_I,
    ['j'] = GHOSTTY_KEY_J,
    ['k'] = GHOSTTY_KEY_K,
    ['l'] = GHOSTTY_KEY_L,
    ['m'] = GHOSTTY_KEY_M,
    ['n'] = GHOSTTY_KEY_N,
    ['o'] = GHOSTTY_KEY_O,
    ['p'] = GHOSTTY_KEY_P,
    ['q'] = GHOSTTY_KEY_Q,
    ['r'] = GHOSTTY_KEY_R,
    ['s'] = GHOSTTY_KEY_S,
    ['t'] = GHOSTTY_KEY_T,
    ['u'] = GHOSTTY_KEY_U,
    ['v'] = GHOSTTY_KEY_V,
    ['w'] = GHOSTTY_KEY_W,
    ['x'] = GHOSTTY_KEY_X,
    ['y'] = GHOSTTY_KEY_Y,
    ['z'] = GHOSTTY_KEY_Z,
};

typedef struct {
    xkb_keysym_t sym;
    GhosttyKey key;
} KeysymMap;

static const KeysymMap functional_keys[] = {
    {XKB_KEY_BackSpace, GHOSTTY_KEY_BACKSPACE},
    {XKB_KEY_Tab, GHOSTTY_KEY_TAB},
    {XKB_KEY_ISO_Left_Tab, GHOSTTY_KEY_TAB},
    {XKB_KEY_Return, GHOSTTY_KEY_ENTER},
    {XKB_KEY_Escape, GHOSTTY_KEY_ESCAPE},
    {XKB_KEY_Delete, GHOSTTY_KEY_DELETE},
    {XKB_KEY_Insert, GHOSTTY_KEY_INSERT},
    {XKB_KEY_Home, GHOSTTY_KEY_HOME},
    {XKB_KEY_End, GHOSTTY_KEY_END},
    {XKB_KEY_Page_Up, GHOSTTY_KEY_PAGE_UP},
    {XKB_KEY_Page_Down, GHOSTTY_KEY_PAGE_DOWN},
    {XKB_KEY_Up, GHOSTTY_KEY_ARROW_UP},
    {XKB_KEY_Down, GHOSTTY_KEY_ARROW_DOWN},
    {XKB_KEY_Left, GHOSTTY_KEY_ARROW_LEFT},
    {XKB_KEY_Right, GHOSTTY_KEY_ARROW_RIGHT},
    {XKB_KEY_Shift_L, GHOSTTY_KEY_SHIFT_LEFT},
    {XKB_KEY_Shift_R, GHOSTTY_KEY_SHIFT_RIGHT},
    {XKB_KEY_Control_L, GHOSTTY_KEY_CONTROL_LEFT},
    {XKB_KEY_Control_R, GHOSTTY_KEY_CONTROL_RIGHT},
    {XKB_KEY_Alt_L, GHOSTTY_KEY_ALT_LEFT},
    {XKB_KEY_Alt_R, GHOSTTY_KEY_ALT_RIGHT},
    {XKB_KEY_ISO_Level3_Shift, GHOSTTY_KEY_ALT_RIGHT},
    {XKB_KEY_Super_L, GHOSTTY_KEY_META_LEFT},
    {XKB_KEY_Super_R, GHOSTTY_KEY_META_RIGHT},
    {XKB_KEY_Meta_L, GHOSTTY_KEY_META_LEFT},
    {XKB_KEY_Meta_R, GHOSTTY_KEY_META_RIGHT},
    {XKB_KEY_Caps_Lock, GHOSTTY_KEY_CAPS_LOCK},
    {XKB_KEY_Num_Lock, GHOSTTY_KEY_NUM_LOCK},
    {XKB_KEY_Scroll_Lock, GHOSTTY_KEY_SCROLL_LOCK},
    {XKB_KEY_Print, GHOSTTY_KEY_PRINT_SCREEN},
    {XKB_KEY_Pause, GHOSTTY_KEY_PAUSE},
    {XKB_KEY_Menu, GHOSTTY_KEY_CONTEXT_MENU},
    {XKB_KEY_KP_Enter, GHOSTTY_KEY_NUMPAD_ENTER},
    {XKB_KEY_KP_0, GHOSTTY_KEY_NUMPAD_0},
    {XKB_KEY_KP_1, GHOSTTY_KEY_NUMPAD_1},
    {XKB_KEY_KP_2, GHOSTTY_KEY_NUMPAD_2},
    {XKB_KEY_KP_3, GHOSTTY_KEY_NUMPAD_3},
    {XKB_KEY_KP_4, GHOSTTY_KEY_NUMPAD_4},
    {XKB_KEY_KP_5, GHOSTTY_KEY_NUMPAD_5},
    {XKB_KEY_KP_6, GHOSTTY_KEY_NUMPAD_6},
    {XKB_KEY_KP_7, GHOSTTY_KEY_NUMPAD_7},
    {XKB_KEY_KP_8, GHOSTTY_KEY_NUMPAD_8},
    {XKB_KEY_KP_9, GHOSTTY_KEY_NUMPAD_9},
    {XKB_KEY_KP_Decimal, GHOSTTY_KEY_NUMPAD_DECIMAL},
    {XKB_KEY_KP_Separator, GHOSTTY_KEY_NUMPAD_SEPARATOR},
    {XKB_KEY_KP_Add, GHOSTTY_KEY_NUMPAD_ADD},
    {XKB_KEY_KP_Subtract, GHOSTTY_KEY_NUMPAD_SUBTRACT},
    {XKB_KEY_KP_Multiply, GHOSTTY_KEY_NUMPAD_MULTIPLY},
    {XKB_KEY_KP_Divide, GHOSTTY_KEY_NUMPAD_DIVIDE},
    {XKB_KEY_KP_Equal, GHOSTTY_KEY_NUMPAD_EQUAL},
    {XKB_KEY_KP_Home, GHOSTTY_KEY_NUMPAD_HOME},
    {XKB_KEY_KP_End, GHOSTTY_KEY_NUMPAD_END},
    {XKB_KEY_KP_Up, GHOSTTY_KEY_NUMPAD_UP},
    {XKB_KEY_KP_Down, GHOSTTY_KEY_NUMPAD_DOWN},
    {XKB_KEY_KP_Left, GHOSTTY_KEY_NUMPAD_LEFT},
    {XKB_KEY_KP_Right, GHOSTTY_KEY_NUMPAD_RIGHT},
    {XKB_KEY_KP_Page_Up, GHOSTTY_KEY_NUMPAD_PAGE_UP},
    {XKB_KEY_KP_Page_Down, GHOSTTY_KEY_NUMPAD_PAGE_DOWN},
    {XKB_KEY_KP_Insert, GHOSTTY_KEY_NUMPAD_INSERT},
    {XKB_KEY_KP_Delete, GHOSTTY_KEY_NUMPAD_DELETE},
    {XKB_KEY_KP_Begin, GHOSTTY_KEY_NUMPAD_BEGIN},
    {XKB_KEY_XF86AudioMute, GHOSTTY_KEY_AUDIO_VOLUME_MUTE},
    {XKB_KEY_XF86AudioLowerVolume, GHOSTTY_KEY_AUDIO_VOLUME_DOWN},
    {XKB_KEY_XF86AudioRaiseVolume, GHOSTTY_KEY_AUDIO_VOLUME_UP},
    {XKB_KEY_XF86AudioPlay, GHOSTTY_KEY_MEDIA_PLAY_PAUSE},
    {XKB_KEY_XF86AudioStop, GHOSTTY_KEY_MEDIA_STOP},
    {XKB_KEY_XF86AudioNext, GHOSTTY_KEY_MEDIA_TRACK_NEXT},
    {XKB_KEY_XF86AudioPrev, GHOSTTY_KEY_MEDIA_TRACK_PREVIOUS},
    {XKB_KEY_XF86Copy, GHOSTTY_KEY_COPY},
    {XKB_KEY_XF86Cut, GHOSTTY_KEY_CUT},
    {XKB_KEY_XF86Paste, GHOSTTY_KEY_PASTE},
    {XKB_KEY_Help, GHOSTTY_KEY_HELP},
};

typedef struct {
    uint32_t code;
    GhosttyMouseButton button;
} ButtonMap;

static const ButtonMap button_map[] = {
    {BTN_LEFT, GHOSTTY_MOUSE_BUTTON_LEFT},
    {BTN_RIGHT, GHOSTTY_MOUSE_BUTTON_RIGHT},
    {BTN_MIDDLE, GHOSTTY_MOUSE_BUTTON_MIDDLE},
    {BTN_SIDE, GHOSTTY_MOUSE_BUTTON_EIGHT},
    {BTN_EXTRA, GHOSTTY_MOUSE_BUTTON_NINE},
    {BTN_BACK, GHOSTTY_MOUSE_BUTTON_EIGHT},
    {BTN_FORWARD, GHOSTTY_MOUSE_BUTTON_NINE},
};

static GhosttyMods current_mods(Input *in)
{
    GhosttyMods mods = 0;
    if (!in->state) return mods;
    const struct {
        xkb_mod_index_t index;
        GhosttyMods bit;
    } map[] = {
        {in->mod_shift, GHOSTTY_MODS_SHIFT},
        {in->mod_ctrl, GHOSTTY_MODS_CTRL},
        {in->mod_alt, GHOSTTY_MODS_ALT},
        {in->mod_super, GHOSTTY_MODS_SUPER},
        {in->mod_caps, GHOSTTY_MODS_CAPS_LOCK},
        {in->mod_num, GHOSTTY_MODS_NUM_LOCK},
    };
    for (size_t i = 0; i < ARRAY_LEN(map); i++)
        if (map[i].index != XKB_MOD_INVALID && xkb_state_mod_index_is_active(in->state, map[i].index, XKB_STATE_MODS_EFFECTIVE) > 0)
            mods |= map[i].bit;
    return mods;
}

static GhosttyMods consumed_mods(Input *in, xkb_keycode_t kc)
{
    GhosttyMods mods = 0;
    const struct {
        xkb_mod_index_t index;
        GhosttyMods bit;
    } map[] = {
        {in->mod_shift, GHOSTTY_MODS_SHIFT},
        {in->mod_ctrl, GHOSTTY_MODS_CTRL},
        {in->mod_alt, GHOSTTY_MODS_ALT},
        {in->mod_super, GHOSTTY_MODS_SUPER},
    };
    for (size_t i = 0; i < ARRAY_LEN(map); i++)
        if (map[i].index != XKB_MOD_INVALID && xkb_state_mod_index_is_consumed(in->state, kc, map[i].index) > 0) mods |= map[i].bit;
    return mods;
}

static uint32_t unshifted_codepoint(Input *in, xkb_keycode_t kc)
{
    const xkb_keysym_t *syms = NULL;
    xkb_layout_index_t layout = xkb_state_key_get_layout(in->state, kc);
    if (layout == XKB_LAYOUT_INVALID) layout = 0;
    int n = xkb_keymap_key_get_syms_by_level(in->keymap, kc, layout, 0, &syms);
    return n > 0 ? xkb_keysym_to_utf32(syms[0]) : 0;
}

static GhosttyKey key_for(uint32_t evdev, xkb_keysym_t sym, uint32_t unshifted)
{
    for (size_t i = 0; i < ARRAY_LEN(functional_keys); i++)
        if (functional_keys[i].sym == sym) return functional_keys[i].key;
    if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F25) return (GhosttyKey)(GHOSTTY_KEY_F1 + (int)(sym - XKB_KEY_F1));
    if (unshifted && unshifted < ARRAY_LEN(ascii_keys) && ascii_keys[unshifted]) return ascii_keys[unshifted];
    return evdev < ARRAY_LEN(evdev_keys) ? evdev_keys[evdev] : GHOSTTY_KEY_UNIDENTIFIED;
}

static void send_key(Input *in, Panel *p, uint32_t key, GhosttyKeyAction action)
{
    if (!in->state || p->child_exited) return;
    xkb_keycode_t kc = key + 8;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(in->state, kc);
    char text[64] = {0};
    size_t text_len = 0;
    bool composing = false;
    if (action != GHOSTTY_KEY_ACTION_RELEASE && in->compose) {
        if (action == GHOSTTY_KEY_ACTION_PRESS) xkb_compose_state_feed(in->compose, sym);
        switch (xkb_compose_state_get_status(in->compose)) {
        case XKB_COMPOSE_COMPOSING:
            composing = true;
            break;
        case XKB_COMPOSE_COMPOSED:
            text_len = (size_t)MAX(0, xkb_compose_state_get_utf8(in->compose, text, sizeof text));
            xkb_compose_state_reset(in->compose);
            break;
        case XKB_COMPOSE_CANCELLED:
            xkb_compose_state_reset(in->compose);
            composing = true;
            break;
        default:
            break;
        }
    }
    if (!composing && !text_len && action != GHOSTTY_KEY_ACTION_RELEASE) {
        int n = xkb_keysym_to_utf8(sym, text, sizeof text);
        text_len = n > 0 ? (size_t)n - 1 : 0;
        if (text_len == 1 && ((unsigned char)text[0] < 0x20 || text[0] == 0x7f)) text_len = 0;
    }
    uint32_t unshifted = unshifted_codepoint(in, kc);
    GhosttyKey gkey = key_for(key, sym, unshifted);
    GhosttyMods mods = current_mods(in);
    ghostty_key_encoder_setopt_from_terminal(p->key_encoder, p->term);
    ghostty_key_event_set_key(p->key_event, gkey);
    ghostty_key_event_set_action(p->key_event, action);
    ghostty_key_event_set_mods(p->key_event, mods);
    ghostty_key_event_set_consumed_mods(p->key_event, consumed_mods(in, kc));
    ghostty_key_event_set_unshifted_codepoint(p->key_event, unshifted);
    ghostty_key_event_set_composing(p->key_event, composing);
    ghostty_key_event_set_utf8(p->key_event, text_len ? text : NULL, text_len);
    char out[128];
    size_t written = 0;
    if (ghostty_key_encoder_encode(p->key_encoder, p->key_event, out, sizeof out, &written) == GHOSTTY_SUCCESS && written) {
        panel_write(p, out, written);
        if (action != GHOSTTY_KEY_ACTION_RELEASE) panel_scroll_to_bottom(p);
    }
    if (action == GHOSTTY_KEY_ACTION_PRESS) {
        p->blink_on = true;
        p->blink_next = now_ms() + (uint64_t)(p->cfg.cursor_blink_interval * 1000.0);
        p->dirty = true;
    }
    log_debug(debug_input, "key code=%u sym=0x%x action=%d mods=0x%x text=%.*s encoded=%zu", key, sym, action, mods, (int)text_len,
              text, written);
}

static void keyboard_keymap(void *data, struct wl_keyboard *kb, uint32_t format, int32_t fd, uint32_t size)
{
    Input *in = data;
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }
    char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return;
    struct xkb_keymap *keymap = xkb_keymap_new_from_buffer(in->xkb, map, strnlen(map, size), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    if (!keymap) {
        log_msg("failed to compile the keymap");
        return;
    }
    log_debug(debug_input, "keymap received (%u bytes)", size);
    struct xkb_state *state = xkb_state_new(keymap);
    if (!state) {
        xkb_keymap_unref(keymap);
        return;
    }
    xkb_state_unref(in->state);
    xkb_keymap_unref(in->keymap);
    in->keymap = keymap;
    in->state = state;
    in->mod_shift = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
    in->mod_ctrl = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CTRL);
    in->mod_alt = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_ALT);
    in->mod_super = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_LOGO);
    in->mod_caps = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CAPS);
    in->mod_num = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_NUM);
    if (!in->compose_table) {
        const char *locale = getenv("LC_ALL");
        if (!locale || !*locale) locale = getenv("LC_CTYPE");
        if (!locale || !*locale) locale = getenv("LANG");
        if (!locale || !*locale) locale = "C";
        in->compose_table = xkb_compose_table_new_from_locale(in->xkb, locale, XKB_COMPOSE_COMPILE_NO_FLAGS);
        if (in->compose_table) in->compose = xkb_compose_state_new(in->compose_table, XKB_COMPOSE_STATE_NO_FLAGS);
    }
}

static void keyboard_enter(void *data, struct wl_keyboard *kb, uint32_t serial, struct wl_surface *surface, struct wl_array *keys)
{
    Input *in = data;
    Panel *p = app_panel_for_surface(in->app, surface);
    log_debug(debug_input, "keyboard enter panel=%llu pressed=%zu", p ? (unsigned long long)p->id : 0ull, keys->size / sizeof(uint32_t));
    in->keyboard_focus = p;
    if (p) panel_set_focus(p, true);
}

static void keyboard_leave(void *data, struct wl_keyboard *kb, uint32_t serial, struct wl_surface *surface)
{
    Input *in = data;
    Panel *p = in->keyboard_focus;
    log_debug(debug_input, "keyboard leave panel=%llu", p ? (unsigned long long)p->id : 0ull);
    in->keyboard_focus = NULL;
    in->repeat_key = 0;
    if (p) panel_set_focus(p, false);
}

static void keyboard_key(void *data, struct wl_keyboard *kb, uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
    Input *in = data;
    Panel *p = in->keyboard_focus;
    if (!p) return;
    bool pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
    send_key(in, p, key, pressed ? GHOSTTY_KEY_ACTION_PRESS : GHOSTTY_KEY_ACTION_RELEASE);
    if (pressed && in->keymap && in->repeat_rate > 0 && xkb_keymap_key_repeats(in->keymap, key + 8)) {
        in->repeat_key = key;
        in->repeat_next = now_ms() + (uint64_t)in->repeat_delay;
    } else if (!pressed && in->repeat_key == key) {
        in->repeat_key = 0;
    }
}

static void keyboard_modifiers(void *data, struct wl_keyboard *kb, uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked,
                               uint32_t group)
{
    Input *in = data;
    if (in->state) xkb_state_update_mask(in->state, depressed, latched, locked, 0, 0, group);
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *kb, int32_t rate, int32_t delay)
{
    Input *in = data;
    in->repeat_rate = rate;
    in->repeat_delay = delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

static void mouse_setup(Input *in, Panel *p)
{
    const FontMetrics *m = fonts_metrics(p->fonts);
    ghostty_mouse_encoder_setopt_from_terminal(p->mouse_encoder, p->term);
    GhosttyMouseEncoderSize size = {
        .size = sizeof(GhosttyMouseEncoderSize),
        .screen_width = (uint32_t)p->buf_w,
        .screen_height = (uint32_t)p->buf_h,
        .cell_width = (uint32_t)m->cell_w,
        .cell_height = (uint32_t)m->cell_h,
        .padding_top = (uint32_t)p->pad_y,
        .padding_bottom = (uint32_t)p->pad_y,
        .padding_left = (uint32_t)p->pad_x,
        .padding_right = (uint32_t)p->pad_x,
    };
    ghostty_mouse_encoder_setopt(p->mouse_encoder, GHOSTTY_MOUSE_ENCODER_OPT_SIZE, &size);
    bool any = in->buttons != 0;
    ghostty_mouse_encoder_setopt(p->mouse_encoder, GHOSTTY_MOUSE_ENCODER_OPT_ANY_BUTTON_PRESSED, &any);
    bool track = true;
    ghostty_mouse_encoder_setopt(p->mouse_encoder, GHOSTTY_MOUSE_ENCODER_OPT_TRACK_LAST_CELL, &track);
    ghostty_mouse_event_set_mods(p->mouse_event, current_mods(in));
    ghostty_mouse_event_set_position(p->mouse_event, (GhosttyMousePosition){
                                                         .x = (float)(in->pointer_x * p->scale),
                                                         .y = (float)(in->pointer_y * p->scale),
                                                     });
}

static void mouse_send(Input *in, Panel *p, GhosttyMouseAction action, GhosttyMouseButton button)
{
    if (p->child_exited) return;
    mouse_setup(in, p);
    ghostty_mouse_event_set_action(p->mouse_event, action);
    if (button == GHOSTTY_MOUSE_BUTTON_UNKNOWN) ghostty_mouse_event_clear_button(p->mouse_event);
    else ghostty_mouse_event_set_button(p->mouse_event, button);
    char out[128];
    size_t written = 0;
    if (ghostty_mouse_encoder_encode(p->mouse_encoder, p->mouse_event, out, sizeof out, &written) == GHOSTTY_SUCCESS && written)
        panel_write(p, out, written);
    log_debug(debug_input, "mouse action=%d button=%d at %.1f,%.1f encoded=%zu", action, button, in->pointer_x, in->pointer_y, written);
}

static GhosttyMouseButton held_button(Input *in)
{
    for (size_t i = 0; i < ARRAY_LEN(button_map); i++)
        if (in->buttons & (1u << i)) return button_map[i].button;
    return GHOSTTY_MOUSE_BUTTON_UNKNOWN;
}

static void pointer_enter(void *data, struct wl_pointer *ptr, uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y)
{
    Input *in = data;
    in->pointer_focus = app_panel_for_surface(in->app, surface);
    in->pointer_x = wl_fixed_to_double(x);
    in->pointer_y = wl_fixed_to_double(y);
    if (in->cursor_device) wp_cursor_shape_device_v1_set_shape(in->cursor_device, serial, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT);
}

static void pointer_leave(void *data, struct wl_pointer *ptr, uint32_t serial, struct wl_surface *surface)
{
    Input *in = data;
    in->pointer_focus = NULL;
    in->buttons = 0;
}

static void pointer_motion(void *data, struct wl_pointer *ptr, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
    Input *in = data;
    in->pointer_x = wl_fixed_to_double(x);
    in->pointer_y = wl_fixed_to_double(y);
    if (in->pointer_focus) mouse_send(in, in->pointer_focus, GHOSTTY_MOUSE_ACTION_MOTION, held_button(in));
}

static void pointer_button(void *data, struct wl_pointer *ptr, uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
    Input *in = data;
    Panel *p = in->pointer_focus;
    for (size_t i = 0; i < ARRAY_LEN(button_map); i++) {
        if (button_map[i].code != button) continue;
        bool pressed = state == WL_POINTER_BUTTON_STATE_PRESSED;
        if (pressed) in->buttons |= 1u << i;
        else in->buttons &= ~(1u << i);
        if (p) mouse_send(in, p, pressed ? GHOSTTY_MOUSE_ACTION_PRESS : GHOSTTY_MOUSE_ACTION_RELEASE, button_map[i].button);
        return;
    }
}

static void pointer_axis(void *data, struct wl_pointer *ptr, uint32_t time, uint32_t axis, wl_fixed_t value)
{
    Input *in = data;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) in->scroll_v += wl_fixed_to_double(value);
    else in->scroll_h += wl_fixed_to_double(value);
}

static void pointer_axis_discrete(void *data, struct wl_pointer *ptr, uint32_t axis, int32_t discrete)
{
    Input *in = data;
    in->scroll_discrete = true;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) in->scroll_v120 += discrete * 120;
    else in->scroll_h120 += discrete * 120;
}

static void pointer_axis_value120(void *data, struct wl_pointer *ptr, uint32_t axis, int32_t value120)
{
    Input *in = data;
    in->scroll_discrete = true;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) in->scroll_v120 += value120;
    else in->scroll_h120 += value120;
}

static void scroll_lines(Input *in, Panel *p, int steps, bool horizontal)
{
    if (!steps) return;
    GhosttyTerminalScreen screen = GHOSTTY_TERMINAL_SCREEN_PRIMARY;
    ghostty_terminal_get(p->term, GHOSTTY_TERMINAL_DATA_ACTIVE_SCREEN, &screen);
    bool tracking = false;
    ghostty_terminal_get(p->term, GHOSTTY_TERMINAL_DATA_MOUSE_TRACKING, &tracking);
    if (tracking) {
        GhosttyMouseButton b = horizontal ? (steps < 0 ? GHOSTTY_MOUSE_BUTTON_SIX : GHOSTTY_MOUSE_BUTTON_SEVEN)
                                          : (steps < 0 ? GHOSTTY_MOUSE_BUTTON_FOUR : GHOSTTY_MOUSE_BUTTON_FIVE);
        for (int i = 0; i < abs(steps); i++) {
            mouse_send(in, p, GHOSTTY_MOUSE_ACTION_PRESS, b);
            mouse_send(in, p, GHOSTTY_MOUSE_ACTION_RELEASE, b);
        }
        return;
    }
    if (horizontal) return;
    if (screen == GHOSTTY_TERMINAL_SCREEN_ALTERNATE && panel_mode(p, GHOSTTY_MODE_ALT_SCROLL)) {
        GhosttyKey key = steps < 0 ? GHOSTTY_KEY_ARROW_UP : GHOSTTY_KEY_ARROW_DOWN;
        ghostty_key_encoder_setopt_from_terminal(p->key_encoder, p->term);
        ghostty_key_event_set_key(p->key_event, key);
        ghostty_key_event_set_action(p->key_event, GHOSTTY_KEY_ACTION_PRESS);
        ghostty_key_event_set_mods(p->key_event, 0);
        ghostty_key_event_set_consumed_mods(p->key_event, 0);
        ghostty_key_event_set_unshifted_codepoint(p->key_event, 0);
        ghostty_key_event_set_composing(p->key_event, false);
        ghostty_key_event_set_utf8(p->key_event, NULL, 0);
        char out[32];
        size_t written = 0;
        if (ghostty_key_encoder_encode(p->key_encoder, p->key_event, out, sizeof out, &written) == GHOSTTY_SUCCESS)
            for (int i = 0; i < abs(steps); i++) panel_write(p, out, written);
        return;
    }
    ghostty_terminal_scroll_viewport(p->term, (GhosttyTerminalScrollViewport){.tag = GHOSTTY_SCROLL_VIEWPORT_DELTA, .value = {.delta = steps * 3}});
    p->dirty = true;
}

static void pointer_frame(void *data, struct wl_pointer *ptr)
{
    Input *in = data;
    Panel *p = in->pointer_focus;
    int v = 0, h = 0;
    if (in->scroll_discrete) {
        v = in->scroll_v120 / 120;
        h = in->scroll_h120 / 120;
        in->scroll_v120 -= v * 120;
        in->scroll_h120 -= h * 120;
        in->scroll_v = in->scroll_h = 0;
    } else {
        const double unit = 15.0;
        v = (int)(in->scroll_v / unit);
        h = (int)(in->scroll_h / unit);
        in->scroll_v -= v * unit;
        in->scroll_h -= h * unit;
    }
    in->scroll_discrete = false;
    if (!p) return;
    scroll_lines(in, p, v, false);
    scroll_lines(in, p, h, true);
}

static void pointer_axis_source(void *data, struct wl_pointer *ptr, uint32_t source)
{
}

static void pointer_axis_stop(void *data, struct wl_pointer *ptr, uint32_t time, uint32_t axis)
{
}

static void pointer_axis_relative_direction(void *data, struct wl_pointer *ptr, uint32_t axis, uint32_t direction)
{
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
    .axis_value120 = pointer_axis_value120,
    .axis_relative_direction = pointer_axis_relative_direction,
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
    Input *in = data;
    bool has_keyboard = caps & WL_SEAT_CAPABILITY_KEYBOARD;
    bool has_pointer = caps & WL_SEAT_CAPABILITY_POINTER;
    if (has_keyboard && !in->keyboard) {
        in->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(in->keyboard, &keyboard_listener, in);
    } else if (!has_keyboard && in->keyboard) {
        wl_keyboard_destroy(in->keyboard);
        in->keyboard = NULL;
    }
    if (has_pointer && !in->pointer) {
        in->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(in->pointer, &pointer_listener, in);
        if (in->app->cursor_shape) in->cursor_device = wp_cursor_shape_manager_v1_get_pointer(in->app->cursor_shape, in->pointer);
    } else if (!has_pointer && in->pointer) {
        if (in->cursor_device) wp_cursor_shape_device_v1_destroy(in->cursor_device);
        in->cursor_device = NULL;
        wl_pointer_destroy(in->pointer);
        in->pointer = NULL;
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

void input_init(Input *in, App *app)
{
    *in = (Input){.app = app, .repeat_rate = 25, .repeat_delay = 600};
    in->xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    in->mod_shift = in->mod_ctrl = in->mod_alt = in->mod_super = in->mod_caps = in->mod_num = XKB_MOD_INVALID;
}

void input_bind_seat(Input *in, struct wl_seat *seat)
{
    in->seat = seat;
    wl_seat_add_listener(seat, &seat_listener, in);
}

void input_destroy(Input *in)
{
    if (in->cursor_device) wp_cursor_shape_device_v1_destroy(in->cursor_device);
    if (in->keyboard) wl_keyboard_destroy(in->keyboard);
    if (in->pointer) wl_pointer_destroy(in->pointer);
    if (in->seat) wl_seat_destroy(in->seat);
    xkb_compose_state_unref(in->compose);
    xkb_compose_table_unref(in->compose_table);
    xkb_state_unref(in->state);
    xkb_keymap_unref(in->keymap);
    xkb_context_unref(in->xkb);
    *in = (Input){0};
}

void input_panel_gone(Input *in, Panel *p)
{
    if (in->keyboard_focus == p) {
        in->keyboard_focus = NULL;
        in->repeat_key = 0;
    }
    if (in->pointer_focus == p) in->pointer_focus = NULL;
}

uint64_t input_next_deadline(Input *in)
{
    return in->repeat_key && in->keyboard_focus ? in->repeat_next : UINT64_MAX;
}

void input_tick(Input *in, uint64_t now)
{
    if (!in->repeat_key || !in->keyboard_focus || now < in->repeat_next) return;
    send_key(in, in->keyboard_focus, in->repeat_key, GHOSTTY_KEY_ACTION_REPEAT);
    in->repeat_next = now + (uint64_t)MAX(1, 1000 / MAX(1, in->repeat_rate));
}
