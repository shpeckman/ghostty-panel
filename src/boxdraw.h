// src/boxdraw.h
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "font.h"

enum {
    SPRITE_BASE = 0x110000,
    SPRITE_UNDERLINE_SINGLE = SPRITE_BASE,
    SPRITE_UNDERLINE_DOUBLE,
    SPRITE_UNDERLINE_CURLY,
    SPRITE_UNDERLINE_DOTTED,
    SPRITE_UNDERLINE_DASHED,
    SPRITE_STRIKETHROUGH,
    SPRITE_OVERLINE,
    SPRITE_CURSOR_BEAM,
    SPRITE_CURSOR_UNDERLINE,
    SPRITE_CURSOR_HOLLOW,
};

bool boxdraw_supports(uint32_t cp);
void boxdraw_render(uint32_t cp, const FontMetrics *m, int width, int height, uint8_t *alpha);
