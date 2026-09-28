// src/font.h
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

typedef struct {
    int cell_w;
    int cell_h;
    int baseline;
    int underline_pos;
    int underline_thickness;
    int strike_pos;
    int strike_thickness;
    int line_light;
    int line_heavy;
    double scale;
} FontMetrics;

typedef struct {
    int width;
    int height;
    bool color;
    uint8_t *pixels;
} GlyphBitmap;

enum {
    STYLE_BOLD = 1,
    STYLE_ITALIC = 2,
};

typedef struct Fonts Fonts;

Fonts *fonts_new(const Config *cfg, double scale, char *err, size_t errlen);
void fonts_free(Fonts *f);
const FontMetrics *fonts_metrics(const Fonts *f);
bool fonts_render(Fonts *f, const uint32_t *cps, size_t n, int style, int cells, GlyphBitmap *out);
