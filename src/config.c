// src/config.c
#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    CONF_STR,
    CONF_DOUBLE,
    CONF_INT,
    CONF_COLOR,
    CONF_OPTIONAL_COLOR,
    CONF_CHOICE,
} ConfKind;

typedef struct {
    const char *key;
    ConfKind kind;
    size_t offset;
    const char *const *choices;
    const char *def;
} ConfSpec;

static const char *const cursor_shape_choices[] = {"block", "beam", "underline", NULL};
static const char *const remote_control_choices[] = {"no", "yes", "socket", "socket-only", NULL};

#define CO(field) offsetof(Config, field)

static const ConfSpec conf_specs[] = {
    {"font_family", CONF_STR, CO(font_family), NULL, "default"},
    {"bold_font", CONF_STR, CO(bold_font), NULL, "auto"},
    {"italic_font", CONF_STR, CO(italic_font), NULL, "auto"},
    {"bold_italic_font", CONF_STR, CO(bold_italic_font), NULL, "auto"},
    {"font_size", CONF_DOUBLE, CO(font_size), NULL, "11.0"},
    {"foreground", CONF_COLOR, CO(foreground), NULL, "#dddddd"},
    {"background", CONF_COLOR, CO(background), NULL, "#000000"},
    {"cursor", CONF_OPTIONAL_COLOR, CO(cursor), NULL, "#cccccc"},
    {"cursor_text_color", CONF_OPTIONAL_COLOR, CO(cursor_text_color), NULL, "#111111"},
    {"background_opacity", CONF_DOUBLE, CO(background_opacity), NULL, "1.0"},
    {"cursor_shape", CONF_CHOICE, CO(cursor_shape), cursor_shape_choices, "block"},
    {"cursor_blink_interval", CONF_DOUBLE, CO(cursor_blink_interval), NULL, "0.5"},
    {"window_padding_width", CONF_DOUBLE, CO(window_padding_width), NULL, "0"},
    {"scrollback_lines", CONF_INT, CO(scrollback_lines), NULL, "2000"},
    {"shell", CONF_STR, CO(shell), NULL, "."},
    {"term", CONF_STR, CO(term), NULL, "xterm-256color"},
    {"allow_remote_control", CONF_CHOICE, CO(allow_remote_control), remote_control_choices, "no"},
    {"listen_on", CONF_STR, CO(listen_on), NULL, "none"},
};

static bool parse_color(const char *value, GhosttyColorRgb *out)
{
    return ghostty_color_parse(value, strlen(value), out) == GHOSTTY_SUCCESS;
}

static bool set_spec(Config *c, const ConfSpec *s, const char *value, char *err, size_t errlen)
{
    char *field = (char *)c + s->offset;
    switch (s->kind) {
    case CONF_STR:
        free(*(char **)field);
        *(char **)field = xstrdup(value);
        return true;
    case CONF_DOUBLE:
        if (!parse_double(value, (double *)field)) {
            snprintf(err, errlen, "%s: not a number: %s", s->key, value);
            return false;
        }
        return true;
    case CONF_INT:
        if (!parse_long(value, (long *)field)) {
            snprintf(err, errlen, "%s: not an integer: %s", s->key, value);
            return false;
        }
        return true;
    case CONF_COLOR:
        if (!parse_color(value, (GhosttyColorRgb *)field)) {
            snprintf(err, errlen, "%s: invalid color: %s", s->key, value);
            return false;
        }
        return true;
    case CONF_OPTIONAL_COLOR: {
        OptionalColor *oc = (OptionalColor *)field;
        if (strcmp(value, "none") == 0) {
            oc->special = COLOR_SPECIAL_DEFAULT;
            return true;
        }
        if (strcmp(value, "background") == 0) {
            oc->special = COLOR_SPECIAL_BACKGROUND;
            return true;
        }
        if (!parse_color(value, &oc->rgb)) {
            snprintf(err, errlen, "%s: invalid color: %s", s->key, value);
            return false;
        }
        oc->special = COLOR_SPECIAL_NONE;
        return true;
    }
    case CONF_CHOICE:
        for (int i = 0; s->choices[i]; i++) {
            if (strcmp(s->choices[i], value) == 0) {
                *(int *)field = i;
                return true;
            }
        }
        snprintf(err, errlen, "%s: invalid value: %s", s->key, value);
        return false;
    }
    return false;
}

bool config_set(Config *c, const char *key, const char *value, char *err, size_t errlen)
{
    for (size_t i = 0; i < ARRAY_LEN(conf_specs); i++)
        if (strcmp(conf_specs[i].key, key) == 0) return set_spec(c, &conf_specs[i], value, err, errlen);
    long index;
    if (strncmp(key, "color", 5) == 0 && parse_long(key + 5, &index) && index >= 0 && index < 256) {
        if (!parse_color(value, &c->palette[index])) {
            snprintf(err, errlen, "%s: invalid color: %s", key, value);
            return false;
        }
        c->palette_set[index] = true;
        return true;
    }
    snprintf(err, errlen, "unknown option: %s", key);
    return false;
}

void config_defaults(Config *c)
{
    *c = (Config){0};
    char err[256];
    for (size_t i = 0; i < ARRAY_LEN(conf_specs); i++) set_spec(c, &conf_specs[i], conf_specs[i].def, err, sizeof err);
    ghostty_color_palette_default(c->palette);
}

void config_free(Config *c)
{
    for (size_t i = 0; i < ARRAY_LEN(conf_specs); i++) {
        if (conf_specs[i].kind != CONF_STR) continue;
        char **field = (char **)((char *)c + conf_specs[i].offset);
        free(*field);
        *field = NULL;
    }
}

void config_copy(Config *dst, const Config *src)
{
    *dst = *src;
    for (size_t i = 0; i < ARRAY_LEN(conf_specs); i++) {
        if (conf_specs[i].kind != CONF_STR) continue;
        char **field = (char **)((char *)dst + conf_specs[i].offset);
        if (*field) *field = xstrdup(*field);
    }
}

static void apply_line(Config *c, char *line, const char *origin, int lineno)
{
    char *s = trim(line);
    if (!*s || *s == '#') return;
    char *value = s;
    while (*value && !isspace((unsigned char)*value)) value++;
    if (*value) *value++ = 0;
    value = trim(value);
    char err[512];
    if (!config_set(c, s, value, err, sizeof err)) log_msg("%s:%d: %s", origin, lineno, err);
}

static void load_file(Config *c, const char *path, bool required)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        if (required) log_msg("cannot open config file %s: %s", path, strerror(errno));
        return;
    }
    char *line = NULL;
    size_t cap = 0;
    int lineno = 0;
    while (getline(&line, &cap, f) >= 0) apply_line(c, line, path, ++lineno);
    free(line);
    fclose(f);
}

char *config_default_path(void)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return xasprintf("%s/ghostty-panel/ghostty-panel.conf", xdg);
    const char *home = getenv("HOME");
    return xasprintf("%s/.config/ghostty-panel/ghostty-panel.conf", home ? home : "");
}

bool config_load(Config *c, const StrList *files, const StrList *overrides)
{
    if (files->len == 0) {
        char *path = config_default_path();
        load_file(c, path, false);
        free(path);
    }
    for (size_t i = 0; i < files->len; i++) {
        if (strcmp(files->items[i], "NONE") == 0) continue;
        load_file(c, files->items[i], true);
    }
    bool ok = true;
    for (size_t i = 0; i < overrides->len; i++) {
        char *copy = xstrdup(overrides->items[i]);
        char *eq = strchr(copy, '=');
        char *key = copy;
        char *value = "";
        if (eq) {
            *eq = 0;
            value = eq + 1;
        } else {
            char *sp = copy;
            while (*sp && !isspace((unsigned char)*sp)) sp++;
            if (*sp) {
                *sp = 0;
                value = sp + 1;
            }
        }
        char err[512];
        if (!config_set(c, trim(key), trim(value), err, sizeof err)) {
            log_msg("--override %s: %s", overrides->items[i], err);
            ok = false;
        }
        free(copy);
    }
    return ok;
}
