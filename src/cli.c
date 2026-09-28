// src/cli.c
#include "cli.h"

#include <stdlib.h>
#include <string.h>

#include "json.h"

static const char *const edge_choices[] = {"top", "bottom", "left", "right", "background", "center", "center-sized", "none", NULL};
static const char *const layer_choices[] = {"background", "bottom", "top", "overlay", NULL};
static const char *const focus_choices[] = {"not-allowed", "exclusive", "on-demand", NULL};

#define PO(field) offsetof(PanelOptions, field)

static const OptSpec panel_specs[] = {
    {"--lines", OPT_STR, PO(lines), NULL, "1", "lines", LSC_Y_SIZE,
     "The number of lines shown in the panel. Ignored for background, centered, and vertical panels. "
     "With the suffix px it sets the height of the panel in pixels instead of lines."},
    {"--columns", OPT_STR, PO(columns), NULL, "1", "columns", LSC_X_SIZE,
     "The number of columns shown in the panel. Ignored for background, centered, and horizontal panels. "
     "With the suffix px it sets the width of the panel in pixels instead of columns."},
    {"--margin-top", OPT_INT, PO(margin_top), NULL, "0", "margin_top", LSC_MARGIN_TOP,
     "Top margin for the panel, in pixels. Has no effect for bottom edge panels."},
    {"--margin-left", OPT_INT, PO(margin_left), NULL, "0", "margin_left", LSC_MARGIN_LEFT,
     "Left margin for the panel, in pixels. Has no effect for right edge panels."},
    {"--margin-bottom", OPT_INT, PO(margin_bottom), NULL, "0", "margin_bottom", LSC_MARGIN_BOTTOM,
     "Bottom margin for the panel, in pixels. Has no effect for top edge panels."},
    {"--margin-right", OPT_INT, PO(margin_right), NULL, "0", "margin_right", LSC_MARGIN_RIGHT,
     "Right margin for the panel, in pixels. Has no effect for left edge panels."},
    {"--edge", OPT_CHOICE, PO(edge), edge_choices, "top", "edge", LSC_EDGE,
     "Which edge of the screen to place the panel on. background makes the panel the desktop wallpaper. "
     "center anchors the panel to all sides, shrink it with the margins. none anchors it to the top left corner, "
     "place it with the margins and size it with --lines and --columns. center-sized is like none but centered."},
    {"--layer", OPT_CHOICE, PO(layer), layer_choices, "bottom", "layer", LSC_LAYER,
     "The layer shell layer the panel is drawn on. Ignored and set to background when --edge=background."},
    {"--config -c", OPT_LIST, PO(config), NULL, NULL, "config", 0,
     "Path to a config file to use for the panel. Can be given multiple times. NONE disables loading the default config file."},
    {"--override -o", OPT_LIST, PO(override), NULL, NULL, "override", 0,
     "Override an individual configuration option, can be given multiple times. Syntax: name=value, e.g. -o font_size=20"},
    {"--output-name", OPT_STR, PO(output_name), NULL, NULL, "output_name", LSC_OUTPUT,
     "The output (monitor) to show the panel on, by name. Use list to print the available outputs, listjson for JSON."},
    {"--class --app-id", OPT_STR, PO(app_id), NULL, "ghostty-panel", "app_id", 0,
     "The namespace of the layer shell surface."},
    {"--name --os-window-tag", OPT_STR, PO(name), NULL, NULL, "name", 0,
     "Accepted for compatibility with the kitty panel kitten, has no effect on Wayland."},
    {"--focus-policy", OPT_CHOICE, PO(focus_policy), focus_choices, "not-allowed", "focus_policy", LSC_FOCUS,
     "The keyboard interactivity of the panel."},
    {"--hide-on-focus-loss", OPT_FLAG, PO(hide_on_focus_loss), NULL, NULL, "hide_on_focus_loss", LSC_HIDE_ON_FOCUS_LOSS,
     "Hide the panel when it loses keyboard focus. Forces --focus-policy=on-demand."},
    {"--grab-keyboard", OPT_FLAG, PO(grab_keyboard), NULL, NULL, "grab_keyboard", 0,
     "Grab the keyboard, so global shortcuts are passed to the panel. Needs compositor support for the "
     "keyboard-shortcuts-inhibit protocol."},
    {"--exclusive-zone", OPT_INT, PO(exclusive_zone), NULL, "-1", "exclusive_zone", LSC_EXCLUSIVE_ZONE,
     "Request the given exclusive zone for the panel. Only effective for top, bottom, left and right edges when "
     "--override-exclusive-zone is also given."},
    {"--override-exclusive-zone", OPT_FLAG, PO(override_exclusive_zone), NULL, NULL, "override_exclusive_zone", LSC_OVERRIDE_EXCLUSIVE_ZONE,
     "Override the default exclusive zone for top, bottom, left and right edge panels."},
    {"--single-instance -1", OPT_FLAG, PO(single_instance), NULL, NULL, "single_instance", 0,
     "Run a single instance of the panel. New invocations create a new panel in the existing instance."},
    {"--instance-group", OPT_STR, PO(instance_group), NULL, NULL, "instance_group", 0,
     "Used with --single-instance. Invocations with the same group share one instance."},
    {"--wait-for-single-instance-window-close", OPT_FLAG, PO(wait_for_single_instance_window_close), NULL, NULL,
     "wait_for_single_instance_window_close", 0,
     "With --single-instance, do not exit until the panel opened in the existing instance is closed."},
    {"--listen-on", OPT_STR, PO(listen_on), NULL, NULL, "listen_on", 0,
     "Listen on the given socket address for remote control commands, e.g. unix:/tmp/panel, unix:@abstract or "
     "tcp:localhost:12345. Needs allow_remote_control set to yes, socket or socket-only."},
    {"--toggle-visibility", OPT_FLAG, PO(toggle_visibility), NULL, NULL, "toggle_visibility", 0,
     "With --single-instance, toggle the visibility of the existing panels instead of creating a new one."},
    {"--move-to-active-monitor", OPT_FLAG, PO(move_to_active_monitor), NULL, NULL, "move_to_active_monitor", 0,
     "Accepted for compatibility with the kitty panel kitten. Wayland cannot move a panel to another output."},
    {"--start-as-hidden", OPT_FLAG, PO(start_as_hidden), NULL, NULL, "start_as_hidden", 0,
     "Start the panel hidden, useful with --toggle-visibility."},
    {"--detach", OPT_FLAG, PO(detach), NULL, NULL, "detach", 0,
     "Detach from the controlling terminal and run in the background."},
    {"--detached-log", OPT_STR, PO(detached_log), NULL, NULL, "detached_log", 0,
     "Path to a log file for stdout and stderr when using --detach."},
    {"--debug-rendering", OPT_FLAG, PO(debug_rendering), NULL, NULL, "debug_rendering", 0,
     "Log rendering related debug information."},
    {"--debug-input", OPT_FLAG, PO(debug_input), NULL, NULL, "debug_input", 0,
     "Log input related debug information."},
};

const OptTable panel_options_table = {
    .specs = panel_specs,
    .count = ARRAY_LEN(panel_specs),
    .usage = "[options] [cmdline-to-run ...]",
    .help = "Use a command line program to draw a GPU accelerated panel on your Wayland desktop.",
    .positional_json = "args",
};

static bool name_matches(const char *names, const char *arg, size_t arglen)
{
    const char *p = names;
    while (*p) {
        const char *end = strchr(p, ' ');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (n == arglen && strncmp(p, arg, n) == 0) return true;
        if (!end) break;
        p = end + 1;
    }
    return false;
}

static const OptSpec *find_spec(const OptTable *t, const char *arg, size_t arglen, size_t *index)
{
    for (size_t i = 0; i < t->count; i++) {
        if (name_matches(t->specs[i].names, arg, arglen)) {
            *index = i;
            return &t->specs[i];
        }
    }
    return NULL;
}

static int choice_index(const char *const *choices, const char *value)
{
    for (int i = 0; choices[i]; i++)
        if (strcmp(choices[i], value) == 0) return i;
    return -1;
}

static void primary_name(const OptSpec *s, char *out, size_t outlen)
{
    const char *end = strchr(s->names, ' ');
    size_t n = end ? (size_t)(end - s->names) : strlen(s->names);
    snprintf(out, outlen, "%.*s", (int)MIN(n, outlen - 1), s->names);
}

static bool set_value(const OptSpec *s, void *out, const char *value, char *err, size_t errlen)
{
    char *field = (char *)out + s->offset;
    char name[64];
    primary_name(s, name, sizeof name);
    switch (s->kind) {
    case OPT_STR: {
        char **dst = (char **)field;
        free(*dst);
        *dst = value ? xstrdup(value) : NULL;
        return true;
    }
    case OPT_INT: {
        long v;
        if (!parse_long(value, &v)) {
            snprintf(err, errlen, "%s requires an integer, got: %s", name, value);
            return false;
        }
        *(long *)field = v;
        return true;
    }
    case OPT_FLAG: {
        static const char *const truthy[] = {"y", "yes", "true", "1", NULL};
        static const char *const falsy[] = {"n", "no", "false", "0", NULL};
        if (!value || choice_index(truthy, value) >= 0) {
            *(bool *)field = true;
            return true;
        }
        if (choice_index(falsy, value) >= 0) {
            *(bool *)field = false;
            return true;
        }
        snprintf(err, errlen, "%s is a flag, invalid value: %s", name, value);
        return false;
    }
    case OPT_CHOICE: {
        int idx = choice_index(s->choices, value);
        if (idx < 0) {
            Buf b = {0};
            for (int i = 0; s->choices[i]; i++) buf_appendf(&b, "%s%s", i ? ", " : "", s->choices[i]);
            snprintf(err, errlen, "%s must be one of: %s (got: %s)", name, buf_cstr(&b), value);
            buf_free(&b);
            return false;
        }
        *(int *)field = idx;
        return true;
    }
    case OPT_LIST:
        strlist_push((StrList *)field, value);
        return true;
    }
    return false;
}

void cli_defaults(const OptTable *t, void *out)
{
    char err[256];
    for (size_t i = 0; i < t->count; i++) {
        const OptSpec *s = &t->specs[i];
        char *field = (char *)out + s->offset;
        switch (s->kind) {
        case OPT_STR:
            *(char **)field = s->def ? xstrdup(s->def) : NULL;
            break;
        case OPT_INT:
        case OPT_CHOICE:
            if (s->def) set_value(s, out, s->def, err, sizeof err);
            else if (s->kind == OPT_INT) *(long *)field = 0;
            else *(int *)field = 0;
            break;
        case OPT_FLAG:
            *(bool *)field = false;
            break;
        case OPT_LIST:
            *(StrList *)field = (StrList){0};
            break;
        }
    }
}

void cli_free(const OptTable *t, void *out)
{
    for (size_t i = 0; i < t->count; i++) {
        const OptSpec *s = &t->specs[i];
        char *field = (char *)out + s->offset;
        if (s->kind == OPT_STR) {
            free(*(char **)field);
            *(char **)field = NULL;
        } else if (s->kind == OPT_LIST) {
            strlist_clear((StrList *)field);
        }
    }
}

int cli_parse(const OptTable *t, void *out, int argc, char **argv, uint64_t *seen, char *err, size_t errlen)
{
    int i = 0;
    while (i < argc) {
        const char *arg = argv[i];
        if (strcmp(arg, "--") == 0) return i + 1;
        if (arg[0] != '-' || arg[1] == 0) return i;
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) return CLI_HELP;
        const char *eq = arg[1] == '-' ? strchr(arg, '=') : NULL;
        size_t namelen = eq ? (size_t)(eq - arg) : strlen(arg);
        const char *attached = NULL;
        if (arg[1] != '-' && namelen > 2) {
            namelen = 2;
            attached = arg + 2;
            if (*attached == '=') attached++;
        }
        size_t index = 0;
        const OptSpec *s = find_spec(t, arg, namelen, &index);
        if (!s) {
            snprintf(err, errlen, "unknown option: %.*s", (int)namelen, arg);
            return CLI_ERROR;
        }
        const char *value = eq ? eq + 1 : attached;
        i++;
        if (!value && s->kind != OPT_FLAG) {
            if (i >= argc) {
                snprintf(err, errlen, "option %.*s requires a value", (int)namelen, arg);
                return CLI_ERROR;
            }
            value = argv[i++];
        }
        if (!set_value(s, out, value, err, errlen)) return CLI_ERROR;
        if (seen) *seen |= 1ull << index;
    }
    return i;
}

static void print_wrapped(FILE *f, const char *text, int indent, int width)
{
    int col = indent;
    fprintf(f, "%*s", indent, "");
    const char *p = text;
    while (*p) {
        const char *end = p;
        while (*end && *end != ' ') end++;
        int wlen = (int)(end - p);
        if (col > indent && col + 1 + wlen > width) {
            fprintf(f, "\n%*s", indent, "");
            col = indent;
        } else if (col > indent) {
            fputc(' ', f);
            col++;
        }
        fprintf(f, "%.*s", wlen, p);
        col += wlen;
        p = *end ? end + 1 : end;
    }
    fputc('\n', f);
}

void cli_print_help(const OptTable *t, const char *prog, FILE *f)
{
    fprintf(f, "Usage: %s %s\n\n", prog, t->usage);
    print_wrapped(f, t->help, 0, 80);
    fputs("\nOptions:\n", f);
    for (size_t i = 0; i < t->count; i++) {
        const OptSpec *s = &t->specs[i];
        Buf names = {0};
        const char *p = s->names;
        while (*p) {
            const char *end = strchr(p, ' ');
            size_t n = end ? (size_t)(end - p) : strlen(p);
            if (names.len) buf_append_str(&names, ", ");
            buf_append(&names, p, n);
            if (!end) break;
            p = end + 1;
        }
        if (s->kind != OPT_FLAG) buf_append_str(&names, s->kind == OPT_CHOICE ? " CHOICE" : " VALUE");
        fprintf(f, "  %s\n", buf_cstr(&names));
        buf_free(&names);
        print_wrapped(f, s->help, 6, 80);
        if (s->kind == OPT_CHOICE) {
            Buf c = {0};
            for (int k = 0; s->choices[k]; k++) buf_appendf(&c, "%s%s", k ? ", " : "", s->choices[k]);
            fprintf(f, "      Choices: %s\n", buf_cstr(&c));
            buf_free(&c);
        }
        if (s->def) fprintf(f, "      Default: %s\n", s->def);
    }
}

void cli_payload(const OptTable *t, const void *opts, const StrList *positional, Buf *json)
{
    buf_append_str(json, "{");
    bool first = true;
    for (size_t i = 0; i < t->count; i++) {
        const OptSpec *s = &t->specs[i];
        if (!s->json) continue;
        const char *field = (const char *)opts + s->offset;
        buf_appendf(json, "%s", first ? "" : ",");
        first = false;
        json_write_string(json, s->json);
        buf_append_str(json, ":");
        switch (s->kind) {
        case OPT_STR: {
            const char *v = *(char *const *)field;
            if (v) json_write_string(json, v);
            else buf_append_str(json, "null");
            break;
        }
        case OPT_INT:
            buf_appendf(json, "%ld", *(const long *)field);
            break;
        case OPT_FLAG:
            buf_append_str(json, *(const bool *)field ? "true" : "false");
            break;
        case OPT_CHOICE:
            json_write_string(json, s->choices[*(const int *)field]);
            break;
        case OPT_LIST:
            json_write_string_list(json, (const StrList *)field);
            break;
        }
    }
    if (t->positional_json && positional) {
        buf_appendf(json, "%s", first ? "" : ",");
        json_write_string(json, t->positional_json);
        buf_append_str(json, ":");
        json_write_string_list(json, positional);
    }
    buf_append_str(json, "}");
}

static void dual_distance(const char *spec, uint32_t min_cells, uint32_t *cells, uint32_t *px)
{
    long v;
    *cells = min_cells;
    *px = 0;
    if (parse_long(spec, &v)) {
        *cells = (uint32_t)MAX(v, 0);
        return;
    }
    size_t n = strlen(spec);
    char tmp[64];
    if (n > 2 && n < sizeof tmp && strcmp(spec + n - 2, "px") == 0) {
        memcpy(tmp, spec, n - 2);
        tmp[n - 2] = 0;
        if (parse_long(tmp, &v)) *px = (uint32_t)MAX(v, 0);
    } else if (n > 1 && n < sizeof tmp && spec[n - 1] == 'c') {
        memcpy(tmp, spec, n - 1);
        tmp[n - 1] = 0;
        if (parse_long(tmp, &v)) *cells = (uint32_t)MAX(v, 0);
    }
}

LayerConfig layer_config_from_options(const PanelOptions *o)
{
    LayerConfig c = {0};
    c.edge = (Edge)o->edge;
    c.layer = c.edge == EDGE_BACKGROUND ? LAYER_BACKGROUND : (Layer)o->layer;
    c.focus_policy = o->hide_on_focus_loss ? FOCUS_ON_DEMAND : (FocusPolicy)o->focus_policy;
    dual_distance(o->columns ? o->columns : "1", 1, &c.x_cells, &c.x_px);
    dual_distance(o->lines ? o->lines : "1", 1, &c.y_cells, &c.y_px);
    c.margin_top = (int32_t)MAX(o->margin_top, 0);
    c.margin_left = (int32_t)MAX(o->margin_left, 0);
    c.margin_bottom = (int32_t)MAX(o->margin_bottom, 0);
    c.margin_right = (int32_t)MAX(o->margin_right, 0);
    c.exclusive_zone = (int32_t)o->exclusive_zone;
    c.override_exclusive_zone = o->override_exclusive_zone;
    c.hide_on_focus_loss = o->hide_on_focus_loss;
    snprintf(c.output_name, sizeof c.output_name, "%s", o->output_name ? o->output_name : "");
    return c;
}

uint32_t layer_config_fields_seen(uint64_t seen)
{
    uint32_t fields = 0;
    for (size_t i = 0; i < ARRAY_LEN(panel_specs); i++)
        if (seen & (1ull << i)) fields |= panel_specs[i].tag;
    return fields;
}

void layer_config_merge(LayerConfig *dst, const LayerConfig *src, uint32_t fields)
{
    if (fields & LSC_X_SIZE) {
        dst->x_cells = src->x_cells;
        dst->x_px = src->x_px;
    }
    if (fields & LSC_Y_SIZE) {
        dst->y_cells = src->y_cells;
        dst->y_px = src->y_px;
    }
    if (fields & LSC_MARGIN_TOP) dst->margin_top = src->margin_top;
    if (fields & LSC_MARGIN_LEFT) dst->margin_left = src->margin_left;
    if (fields & LSC_MARGIN_BOTTOM) dst->margin_bottom = src->margin_bottom;
    if (fields & LSC_MARGIN_RIGHT) dst->margin_right = src->margin_right;
    if (fields & LSC_EDGE) dst->edge = src->edge;
    if (fields & LSC_LAYER) dst->layer = src->layer;
    if (fields & LSC_OUTPUT) memcpy(dst->output_name, src->output_name, sizeof dst->output_name);
    if (fields & LSC_FOCUS) dst->focus_policy = src->focus_policy;
    if (fields & LSC_EXCLUSIVE_ZONE) dst->exclusive_zone = src->exclusive_zone;
    if (fields & LSC_OVERRIDE_EXCLUSIVE_ZONE) dst->override_exclusive_zone = src->override_exclusive_zone;
    if (fields & LSC_HIDE_ON_FOCUS_LOSS) dst->hide_on_focus_loss = src->hide_on_focus_loss;
    if ((fields & LSC_EDGE) && src->edge == EDGE_BACKGROUND) dst->layer = LAYER_BACKGROUND;
    if (dst->hide_on_focus_loss) dst->focus_policy = FOCUS_ON_DEMAND;
}

bool layer_config_equal(const LayerConfig *a, const LayerConfig *b)
{
    return a->layer == b->layer && a->edge == b->edge && a->x_cells == b->x_cells && a->x_px == b->x_px && a->y_cells == b->y_cells &&
           a->y_px == b->y_px && a->margin_top == b->margin_top && a->margin_left == b->margin_left &&
           a->margin_bottom == b->margin_bottom && a->margin_right == b->margin_right && a->focus_policy == b->focus_policy &&
           a->exclusive_zone == b->exclusive_zone && a->override_exclusive_zone == b->override_exclusive_zone &&
           a->hide_on_focus_loss == b->hide_on_focus_loss && strcmp(a->output_name, b->output_name) == 0;
}

bool layer_config_from_settings(const StrList *settings, const LayerConfig *base, bool incremental, LayerConfig *out, char *err, size_t errlen)
{
    StrList args = {0};
    for (size_t i = 0; i < settings->len; i++) {
        const char *s = settings->items[i];
        strlist_push_owned(&args, strncmp(s, "--", 2) == 0 ? xstrdup(s) : xasprintf("--%s", s));
    }
    PanelOptions opts;
    cli_defaults(&panel_options_table, &opts);
    uint64_t seen = 0;
    char perr[256] = {0};
    int rc = cli_parse(&panel_options_table, &opts, (int)args.len, args.items, &seen, perr, sizeof perr);
    bool ok = true;
    if (rc == CLI_ERROR) {
        snprintf(err, errlen, "Invalid panel options specified: %s", perr);
        ok = false;
    } else if (rc == CLI_HELP || rc < (int)args.len) {
        snprintf(err, errlen, "Invalid panel options specified: %s", rc == CLI_HELP ? "--help" : args.items[rc]);
        ok = false;
    } else {
        LayerConfig parsed = layer_config_from_options(&opts);
        if (incremental && base) {
            *out = *base;
            layer_config_merge(out, &parsed, layer_config_fields_seen(seen));
        } else {
            *out = parsed;
        }
    }
    cli_free(&panel_options_table, &opts);
    strlist_clear(&args);
    return ok;
}
