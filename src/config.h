// src/config.h
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <ghostty/vt.h>

#include "util.h"

typedef enum {
    CURSOR_BLOCK,
    CURSOR_BEAM,
    CURSOR_UNDERLINE,
} CursorShape;

typedef enum {
    REMOTE_CONTROL_NO,
    REMOTE_CONTROL_YES,
    REMOTE_CONTROL_SOCKET,
    REMOTE_CONTROL_SOCKET_ONLY,
} RemoteControl;

typedef enum {
    COLOR_SPECIAL_NONE,
    COLOR_SPECIAL_DEFAULT,
    COLOR_SPECIAL_BACKGROUND,
} ColorSpecial;

typedef struct {
    ColorSpecial special;
    GhosttyColorRgb rgb;
} OptionalColor;

typedef struct {
    char *font_family;
    char *bold_font;
    char *italic_font;
    char *bold_italic_font;
    double font_size;
    GhosttyColorRgb foreground;
    GhosttyColorRgb background;
    OptionalColor cursor;
    OptionalColor cursor_text_color;
    double background_opacity;
    int cursor_shape;
    double cursor_blink_interval;
    double window_padding_width;
    long scrollback_lines;
    char *shell;
    char *term;
    int allow_remote_control;
    char *listen_on;
    GhosttyColorRgb palette[256];
    bool palette_set[256];
} Config;

void config_defaults(Config *c);
void config_free(Config *c);
void config_copy(Config *dst, const Config *src);
bool config_set(Config *c, const char *key, const char *value, char *err, size_t errlen);
bool config_load(Config *c, const StrList *files, const StrList *overrides);
char *config_default_path(void);
