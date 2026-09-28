// src/cli.h
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "util.h"

typedef enum {
    OPT_STR,
    OPT_INT,
    OPT_FLAG,
    OPT_CHOICE,
    OPT_LIST,
} OptKind;

typedef struct {
    const char *names;
    OptKind kind;
    size_t offset;
    const char *const *choices;
    const char *def;
    const char *json;
    uint32_t tag;
    const char *help;
} OptSpec;

typedef struct {
    const OptSpec *specs;
    size_t count;
    const char *usage;
    const char *help;
    const char *positional_json;
} OptTable;

enum {
    CLI_ERROR = -1,
    CLI_HELP = -2,
};

void cli_defaults(const OptTable *t, void *out);
int cli_parse(const OptTable *t, void *out, int argc, char **argv, uint64_t *seen, char *err, size_t errlen);
void cli_free(const OptTable *t, void *out);
void cli_print_help(const OptTable *t, const char *prog, FILE *f);
void cli_payload(const OptTable *t, const void *opts, const StrList *positional, Buf *json);

typedef enum {
    EDGE_TOP,
    EDGE_BOTTOM,
    EDGE_LEFT,
    EDGE_RIGHT,
    EDGE_BACKGROUND,
    EDGE_CENTER,
    EDGE_CENTER_SIZED,
    EDGE_NONE,
} Edge;

typedef enum {
    LAYER_BACKGROUND,
    LAYER_BOTTOM,
    LAYER_TOP,
    LAYER_OVERLAY,
} Layer;

typedef enum {
    FOCUS_NOT_ALLOWED,
    FOCUS_EXCLUSIVE,
    FOCUS_ON_DEMAND,
} FocusPolicy;

enum {
    LSC_X_SIZE = 1u << 0,
    LSC_Y_SIZE = 1u << 1,
    LSC_MARGIN_TOP = 1u << 2,
    LSC_MARGIN_LEFT = 1u << 3,
    LSC_MARGIN_BOTTOM = 1u << 4,
    LSC_MARGIN_RIGHT = 1u << 5,
    LSC_EDGE = 1u << 6,
    LSC_LAYER = 1u << 7,
    LSC_OUTPUT = 1u << 8,
    LSC_FOCUS = 1u << 9,
    LSC_EXCLUSIVE_ZONE = 1u << 10,
    LSC_OVERRIDE_EXCLUSIVE_ZONE = 1u << 11,
    LSC_HIDE_ON_FOCUS_LOSS = 1u << 12,
};

typedef struct {
    Layer layer;
    Edge edge;
    uint32_t x_cells;
    uint32_t x_px;
    uint32_t y_cells;
    uint32_t y_px;
    int32_t margin_top;
    int32_t margin_left;
    int32_t margin_bottom;
    int32_t margin_right;
    FocusPolicy focus_policy;
    int32_t exclusive_zone;
    bool override_exclusive_zone;
    bool hide_on_focus_loss;
    char output_name[128];
} LayerConfig;

typedef struct {
    char *lines;
    char *columns;
    long margin_top;
    long margin_left;
    long margin_bottom;
    long margin_right;
    int edge;
    int layer;
    StrList config;
    StrList override;
    char *output_name;
    char *app_id;
    char *name;
    int focus_policy;
    bool hide_on_focus_loss;
    bool grab_keyboard;
    long exclusive_zone;
    bool override_exclusive_zone;
    bool single_instance;
    char *instance_group;
    bool wait_for_single_instance_window_close;
    char *listen_on;
    bool toggle_visibility;
    bool move_to_active_monitor;
    bool start_as_hidden;
    bool detach;
    char *detached_log;
    bool debug_rendering;
    bool debug_input;
} PanelOptions;

extern const OptTable panel_options_table;

LayerConfig layer_config_from_options(const PanelOptions *o);
uint32_t layer_config_fields_seen(uint64_t seen);
void layer_config_merge(LayerConfig *dst, const LayerConfig *src, uint32_t fields);
bool layer_config_equal(const LayerConfig *a, const LayerConfig *b);
bool layer_config_from_settings(const StrList *settings, const LayerConfig *base, bool incremental, LayerConfig *out, char *err, size_t errlen);
