// src/render.h
#pragma once

#include <stdbool.h>

#include <ghostty/vt.h>

#include "config.h"
#include "font.h"

typedef struct Renderer Renderer;

typedef struct {
    GhosttyTerminal terminal;
    GhosttyRenderState state;
    GhosttyRenderStateRowIterator rows;
    GhosttyRenderStateRowCells cells;
    GhosttyKittyGraphicsPlacementIterator placements;
    Fonts *fonts;
    const Config *cfg;
    int width;
    int height;
    int pad_x;
    int pad_y;
    bool focused;
    bool cursor_blink_on;
} RenderFrame;

bool renderer_init_gl(char *err, size_t errlen);
Renderer *renderer_new(void);
void renderer_free(Renderer *r);
void renderer_reset_glyphs(Renderer *r);
void renderer_draw(Renderer *r, const RenderFrame *frame);
