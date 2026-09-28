// src/boxdraw.c
#include "boxdraw.h"

#include <math.h>
#include <string.h>

#include "util.h"

enum { L, R, U, D };

typedef enum {
    BOX_ARMS,
    BOX_DASH,
    BOX_ARC,
    BOX_DIAGONAL,
} BoxKind;

typedef struct {
    uint8_t kind;
    uint8_t a;
} BoxGlyph;

#define ARMS(l, r, u, d) {BOX_ARMS, (l) | (r) << 2 | (u) << 4 | (d) << 6}
#define DASH(n, vertical, heavy) {BOX_DASH, (n) | (vertical) << 4 | (heavy) << 5}
#define ARC(corner) {BOX_ARC, corner}
#define DIAGONAL(k) {BOX_DIAGONAL, k}

static const BoxGlyph box_glyphs[128] = {
    ARMS(1, 1, 0, 0), ARMS(2, 2, 0, 0), ARMS(0, 0, 1, 1), ARMS(0, 0, 2, 2),
    DASH(3, 0, 0), DASH(3, 0, 1), DASH(3, 1, 0), DASH(3, 1, 1),
    DASH(4, 0, 0), DASH(4, 0, 1), DASH(4, 1, 0), DASH(4, 1, 1),
    ARMS(0, 1, 0, 1), ARMS(0, 2, 0, 1), ARMS(0, 1, 0, 2), ARMS(0, 2, 0, 2),
    ARMS(1, 0, 0, 1), ARMS(2, 0, 0, 1), ARMS(1, 0, 0, 2), ARMS(2, 0, 0, 2),
    ARMS(0, 1, 1, 0), ARMS(0, 2, 1, 0), ARMS(0, 1, 2, 0), ARMS(0, 2, 2, 0),
    ARMS(1, 0, 1, 0), ARMS(2, 0, 1, 0), ARMS(1, 0, 2, 0), ARMS(2, 0, 2, 0),
    ARMS(0, 1, 1, 1), ARMS(0, 2, 1, 1), ARMS(0, 1, 2, 1), ARMS(0, 1, 1, 2),
    ARMS(0, 1, 2, 2), ARMS(0, 2, 2, 1), ARMS(0, 2, 1, 2), ARMS(0, 2, 2, 2),
    ARMS(1, 0, 1, 1), ARMS(2, 0, 1, 1), ARMS(1, 0, 2, 1), ARMS(1, 0, 1, 2),
    ARMS(1, 0, 2, 2), ARMS(2, 0, 2, 1), ARMS(2, 0, 1, 2), ARMS(2, 0, 2, 2),
    ARMS(1, 1, 0, 1), ARMS(2, 1, 0, 1), ARMS(1, 2, 0, 1), ARMS(2, 2, 0, 1),
    ARMS(1, 1, 0, 2), ARMS(2, 1, 0, 2), ARMS(1, 2, 0, 2), ARMS(2, 2, 0, 2),
    ARMS(1, 1, 1, 0), ARMS(2, 1, 1, 0), ARMS(1, 2, 1, 0), ARMS(2, 2, 1, 0),
    ARMS(1, 1, 2, 0), ARMS(2, 1, 2, 0), ARMS(1, 2, 2, 0), ARMS(2, 2, 2, 0),
    ARMS(1, 1, 1, 1), ARMS(2, 1, 1, 1), ARMS(1, 2, 1, 1), ARMS(2, 2, 1, 1),
    ARMS(1, 1, 2, 1), ARMS(1, 1, 1, 2), ARMS(1, 1, 2, 2), ARMS(2, 1, 2, 1),
    ARMS(1, 2, 2, 1), ARMS(2, 1, 1, 2), ARMS(1, 2, 1, 2), ARMS(2, 2, 2, 1),
    ARMS(2, 2, 1, 2), ARMS(2, 1, 2, 2), ARMS(1, 2, 2, 2), ARMS(2, 2, 2, 2),
    DASH(2, 0, 0), DASH(2, 0, 1), DASH(2, 1, 0), DASH(2, 1, 1),
    ARMS(3, 3, 0, 0), ARMS(0, 0, 3, 3), ARMS(0, 3, 0, 1), ARMS(0, 1, 0, 3),
    ARMS(0, 3, 0, 3), ARMS(3, 0, 0, 1), ARMS(1, 0, 0, 3), ARMS(3, 0, 0, 3),
    ARMS(0, 3, 1, 0), ARMS(0, 1, 3, 0), ARMS(0, 3, 3, 0), ARMS(3, 0, 1, 0),
    ARMS(1, 0, 3, 0), ARMS(3, 0, 3, 0), ARMS(0, 3, 1, 1), ARMS(0, 1, 3, 3),
    ARMS(0, 3, 3, 3), ARMS(3, 0, 1, 1), ARMS(1, 0, 3, 3), ARMS(3, 0, 3, 3),
    ARMS(3, 3, 0, 1), ARMS(1, 1, 0, 3), ARMS(3, 3, 0, 3), ARMS(3, 3, 1, 0),
    ARMS(1, 1, 3, 0), ARMS(3, 3, 3, 0), ARMS(3, 3, 1, 1), ARMS(1, 1, 3, 3),
    ARMS(3, 3, 3, 3), ARC(0), ARC(1), ARC(2),
    ARC(3), DIAGONAL(1), DIAGONAL(2), DIAGONAL(3),
    ARMS(1, 0, 0, 0), ARMS(0, 0, 1, 0), ARMS(0, 1, 0, 0), ARMS(0, 0, 0, 1),
    ARMS(2, 0, 0, 0), ARMS(0, 0, 2, 0), ARMS(0, 2, 0, 0), ARMS(0, 0, 0, 2),
    ARMS(1, 2, 0, 0), ARMS(0, 0, 1, 2), ARMS(2, 1, 0, 0), ARMS(0, 0, 2, 1),
};

typedef enum {
    BLOCK_RECT,
    BLOCK_SHADE,
    BLOCK_QUADRANTS,
} BlockKind;

typedef struct {
    uint8_t kind;
    uint8_t x0, y0, x1, y1;
} BlockGlyph;

#define RECT(x0, y0, x1, y1) {BLOCK_RECT, x0, y0, x1, y1}
#define SHADE(level) {BLOCK_SHADE, level, 0, 0, 0}
#define QUADS(mask) {BLOCK_QUADRANTS, mask, 0, 0, 0}

enum { QUL = 1, QUR = 2, QLL = 4, QLR = 8 };

static const BlockGlyph block_glyphs[32] = {
    RECT(0, 0, 8, 4), RECT(0, 7, 8, 8), RECT(0, 6, 8, 8), RECT(0, 5, 8, 8),
    RECT(0, 4, 8, 8), RECT(0, 3, 8, 8), RECT(0, 2, 8, 8), RECT(0, 1, 8, 8),
    RECT(0, 0, 8, 8), RECT(0, 0, 7, 8), RECT(0, 0, 6, 8), RECT(0, 0, 5, 8),
    RECT(0, 0, 4, 8), RECT(0, 0, 3, 8), RECT(0, 0, 2, 8), RECT(0, 0, 1, 8),
    RECT(4, 0, 8, 8), SHADE(1), SHADE(2), SHADE(3),
    RECT(0, 0, 8, 1), RECT(7, 0, 8, 8), QUADS(QLL), QUADS(QLR),
    QUADS(QUL), QUADS(QUL | QLL | QLR), QUADS(QUL | QLR), QUADS(QUL | QUR | QLL),
    QUADS(QUL | QUR | QLR), QUADS(QUR), QUADS(QUR | QLL), QUADS(QUR | QLL | QLR),
};

typedef struct {
    uint8_t *a;
    int w;
    int h;
} Canvas;

static void fill(Canvas *c, int x0, int y0, int x1, int y1, uint8_t v)
{
    x0 = CLAMP(x0, 0, c->w);
    x1 = CLAMP(x1, 0, c->w);
    y0 = CLAMP(y0, 0, c->h);
    y1 = CLAMP(y1, 0, c->h);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if (c->a[y * c->w + x] < v) c->a[y * c->w + x] = v;
}

static void cover(Canvas *c, int x, int y, double coverage)
{
    if (x < 0 || y < 0 || x >= c->w || y >= c->h || coverage <= 0) return;
    uint8_t v = (uint8_t)lround(CLAMP(coverage, 0.0, 1.0) * 255.0);
    if (c->a[y * c->w + x] < v) c->a[y * c->w + x] = v;
}

static int weight_thickness(int weight, const FontMetrics *m)
{
    return weight == 2 ? m->line_heavy : m->line_light;
}

static void fill_arm(Canvas *c, bool horizontal, bool negative, int length, int reach, int c0, int c1)
{
    int a0 = negative ? 0 : reach;
    int a1 = negative ? reach : length;
    if (horizontal) fill(c, a0, c0, a1, c1, 255);
    else fill(c, c0, a0, c1, a1, 255);
}

static void draw_arms(Canvas *c, uint8_t packed, const FontMetrics *m)
{
    int wt[4] = {packed & 3, (packed >> 2) & 3, (packed >> 4) & 3, (packed >> 6) & 3};
    int t1 = m->line_light;
    for (int dir = 0; dir < 4; dir++) {
        if (!wt[dir]) continue;
        bool horizontal = dir == L || dir == R;
        bool negative = dir == L || dir == U;
        int length = horizontal ? c->w : c->h;
        int cross = horizontal ? c->h : c->w;
        int side_a = horizontal ? U : L;
        int side_b = horizontal ? D : R;
        int wa = wt[side_a], wb = wt[side_b], wo = wt[dir ^ 1];
        int tp = MAX(wa && wa != 3 ? weight_thickness(wa, m) : 0, wb && wb != 3 ? weight_thickness(wb, m) : 0);
        bool perp_double = wa == 3 || wb == 3;
        int ps = (length - tp) / 2;
        int ds = (length - 3 * t1) / 2;
        int far_edge = negative ? (perp_double ? ds + 3 * t1 : ps + tp) : (perp_double ? ds : ps);
        if (wt[dir] != 3) {
            int t = weight_thickness(wt[dir], m);
            int cs = (cross - t) / 2;
            int reach;
            if (wa || wb) {
                reach = far_edge;
            } else {
                int own = (length - t) / 2;
                reach = negative ? own + t : own;
            }
            fill_arm(c, horizontal, negative, length, reach, cs, cs + t);
            continue;
        }
        int cs = (cross - 3 * t1) / 2;
        for (int line = 0; line < 2; line++) {
            int ws = wt[line ? side_b : side_a];
            int wother = wt[line ? side_a : side_b];
            int reach;
            if (ws == 3) reach = negative ? ds + t1 : ds + 2 * t1;
            else if (wo == 3) reach = negative ? length : 0;
            else if (wother == 3) reach = negative ? ds + 3 * t1 : ds;
            else if (ws || wother) reach = negative ? ps + tp : ps;
            else reach = negative ? (length + 3 * t1) / 2 : (length - 3 * t1) / 2;
            int c0 = line ? cs + 2 * t1 : cs;
            fill_arm(c, horizontal, negative, length, reach, c0, c0 + t1);
        }
    }
}

static void draw_dash(Canvas *c, uint8_t a, const FontMetrics *m)
{
    int n = a & 15;
    bool vertical = a & 16;
    int t = (a & 32) ? m->line_heavy : m->line_light;
    int length = vertical ? c->h : c->w;
    int cross = vertical ? c->w : c->h;
    int cs = (cross - t) / 2;
    double seg = (double)length / n;
    double gap = MAX(1.0, seg * 0.3);
    for (int i = 0; i < n; i++) {
        int s0 = (int)lround(i * seg + gap / 2);
        int s1 = (int)lround((i + 1) * seg - gap / 2);
        if (vertical) fill(c, cs, s0, cs + t, s1, 255);
        else fill(c, s0, cs, s1, cs + t, 255);
    }
}

static void draw_arc(Canvas *c, int corner, const FontMetrics *m)
{
    int t = m->line_light;
    int vx = (c->w - t) / 2;
    int hy = (c->h - t) / 2;
    double cx = vx + t / 2.0;
    double cy = hy + t / 2.0;
    double r = MIN(c->w - cx, c->h - cy);
    r = MIN(r, MIN(cx, cy));
    int sx = (corner == 0 || corner == 3) ? 1 : -1;
    int sy = (corner == 0 || corner == 1) ? 1 : -1;
    double ccx = cx + sx * r;
    double ccy = cy + sy * r;
    for (int y = 0; y < c->h; y++) {
        for (int x = 0; x < c->w; x++) {
            double px = x + 0.5, py = y + 0.5;
            if ((px - ccx) * sx > 0.5 || (py - ccy) * sy > 0.5) continue;
            double d = fabs(hypot(px - ccx, py - ccy) - r);
            cover(c, x, y, t / 2.0 + 0.5 - d);
        }
    }
    int ex = (int)lround(ccx);
    int ey = (int)lround(ccy);
    if (sx > 0) fill(c, ex, hy, c->w, hy + t, 255);
    else fill(c, 0, hy, ex, hy + t, 255);
    if (sy > 0) fill(c, vx, ey, vx + t, c->h, 255);
    else fill(c, vx, 0, vx + t, ey, 255);
}

static void draw_line_aa(Canvas *c, double x0, double y0, double x1, double y1, double t)
{
    double dx = x1 - x0, dy = y1 - y0;
    double len = hypot(dx, dy);
    if (len <= 0) return;
    for (int y = 0; y < c->h; y++) {
        for (int x = 0; x < c->w; x++) {
            double px = x + 0.5 - x0, py = y + 0.5 - y0;
            double proj = (px * dx + py * dy) / len;
            if (proj < -t || proj > len + t) continue;
            double dist = fabs(px * dy - py * dx) / len;
            cover(c, x, y, t / 2.0 + 0.5 - dist);
        }
    }
}

static void draw_diagonal(Canvas *c, int kind, const FontMetrics *m)
{
    double t = m->line_light;
    if (kind & 1) draw_line_aa(c, c->w, 0, 0, c->h, t);
    if (kind & 2) draw_line_aa(c, 0, 0, c->w, c->h, t);
}

static void draw_block(Canvas *c, const BlockGlyph *b)
{
    switch (b->kind) {
    case BLOCK_RECT:
        fill(c, (c->w * b->x0 + 4) / 8, (c->h * b->y0 + 4) / 8, (c->w * b->x1 + 4) / 8, (c->h * b->y1 + 4) / 8, 255);
        break;
    case BLOCK_SHADE:
        fill(c, 0, 0, c->w, c->h, (uint8_t)(b->x0 * 64));
        break;
    case BLOCK_QUADRANTS: {
        int mx = c->w / 2, my = c->h / 2;
        if (b->x0 & QUL) fill(c, 0, 0, mx, my, 255);
        if (b->x0 & QUR) fill(c, mx, 0, c->w, my, 255);
        if (b->x0 & QLL) fill(c, 0, my, mx, c->h, 255);
        if (b->x0 & QLR) fill(c, mx, my, c->w, c->h, 255);
        break;
    }
    }
}

static void draw_braille(Canvas *c, uint32_t bits)
{
    static const uint8_t dot_col[8] = {0, 0, 0, 1, 1, 1, 0, 1};
    static const uint8_t dot_row[8] = {0, 1, 2, 0, 1, 2, 3, 3};
    double r = MAX(0.75, MIN(c->w / 4.0, c->h / 8.0) * 0.8);
    for (int i = 0; i < 8; i++) {
        if (!(bits & (1u << i))) continue;
        double cx = c->w * (dot_col[i] * 2 + 1) / 4.0;
        double cy = c->h * (dot_row[i] * 2 + 1) / 8.0;
        for (int y = (int)(cy - r - 1); y <= (int)(cy + r + 1); y++)
            for (int x = (int)(cx - r - 1); x <= (int)(cx + r + 1); x++)
                cover(c, x, y, r + 0.5 - hypot(x + 0.5 - cx, y + 0.5 - cy));
    }
}

static void draw_sprite(Canvas *c, uint32_t cp, const FontMetrics *m)
{
    int ut = MAX(1, m->underline_thickness);
    int upos = MIN(m->underline_pos, c->h - ut);
    int bar = MAX(1, (int)lround(1.5 * m->scale));
    switch (cp) {
    case SPRITE_UNDERLINE_SINGLE:
        fill(c, 0, upos, c->w, upos + ut, 255);
        break;
    case SPRITE_UNDERLINE_DOUBLE: {
        int second = upos + 2 * ut;
        int first = upos;
        if (second + ut > c->h) {
            second = c->h - ut;
            first = MAX(0, second - 2 * ut);
        }
        fill(c, 0, first, c->w, first + ut, 255);
        fill(c, 0, second, c->w, second + ut, 255);
        break;
    }
    case SPRITE_UNDERLINE_CURLY: {
        double amp = MAX(1.0, MIN((double)ut * 1.5, (c->h - upos - ut) / 1.0));
        double mid = MIN(upos + ut / 2.0 + amp / 2.0, c->h - amp - ut / 2.0);
        for (int x = 0; x < c->w; x++) {
            double cy = mid - amp * cos(2.0 * M_PI * (x + 0.5) / c->w);
            for (int y = 0; y < c->h; y++) cover(c, x, y, ut / 2.0 + 0.5 - fabs(y + 0.5 - cy));
        }
        break;
    }
    case SPRITE_UNDERLINE_DOTTED:
        for (int x = 0; x < c->w; x += 2 * ut) fill(c, x, upos, x + ut, upos + ut, 255);
        break;
    case SPRITE_UNDERLINE_DASHED:
        fill(c, 0, upos, c->w * 3 / 8, upos + ut, 255);
        fill(c, c->w / 2, upos, c->w * 7 / 8, upos + ut, 255);
        break;
    case SPRITE_STRIKETHROUGH:
        fill(c, 0, m->strike_pos, c->w, m->strike_pos + MAX(1, m->strike_thickness), 255);
        break;
    case SPRITE_OVERLINE:
        fill(c, 0, 0, c->w, ut, 255);
        break;
    case SPRITE_CURSOR_BEAM:
        fill(c, 0, 0, bar, c->h, 255);
        break;
    case SPRITE_CURSOR_UNDERLINE:
        fill(c, 0, c->h - MAX(bar, ut), c->w, c->h, 255);
        break;
    case SPRITE_CURSOR_HOLLOW: {
        int t = MAX(1, m->line_light);
        fill(c, 0, 0, c->w, t, 255);
        fill(c, 0, c->h - t, c->w, c->h, 255);
        fill(c, 0, 0, t, c->h, 255);
        fill(c, c->w - t, 0, c->w, c->h, 255);
        break;
    }
    }
}

bool boxdraw_supports(uint32_t cp)
{
    return (cp >= 0x2500 && cp <= 0x259F) || (cp >= 0x2800 && cp <= 0x28FF) || (cp >= SPRITE_BASE && cp <= SPRITE_CURSOR_HOLLOW);
}

void boxdraw_render(uint32_t cp, const FontMetrics *m, int width, int height, uint8_t *alpha)
{
    Canvas c = {.a = alpha, .w = width, .h = height};
    memset(alpha, 0, (size_t)width * (size_t)height);
    if (cp >= SPRITE_BASE) {
        draw_sprite(&c, cp, m);
    } else if (cp >= 0x2800) {
        draw_braille(&c, cp - 0x2800);
    } else if (cp >= 0x2580) {
        draw_block(&c, &block_glyphs[cp - 0x2580]);
    } else {
        const BoxGlyph *g = &box_glyphs[cp - 0x2500];
        switch (g->kind) {
        case BOX_ARMS: draw_arms(&c, g->a, m); break;
        case BOX_DASH: draw_dash(&c, g->a, m); break;
        case BOX_ARC: draw_arc(&c, g->a, m); break;
        case BOX_DIAGONAL: draw_diagonal(&c, g->a, m); break;
        }
    }
}
