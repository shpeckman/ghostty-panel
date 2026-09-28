// src/render.c
#include "render.h"

#include <GLES2/gl2.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "boxdraw.h"
#include "util.h"

typedef struct {
    float x, y, u, v;
    uint8_t r, g, b, a;
    float kind;
} Vertex;

typedef struct {
    uint8_t r, g, b, a;
} Rgba;

enum {
    KIND_SOLID = 0,
    KIND_GRAY = 1,
    KIND_RGBA = 2,
};

enum {
    ATLAS_GRAY,
    ATLAS_COLOR,
    ATLAS_NONE,
};

typedef struct {
    GLuint tex;
    int size;
    int x;
    int y;
    int row_h;
    bool color;
} Atlas;

typedef struct {
    uint64_t key;
    uint16_t x, y, w, h;
    uint8_t atlas;
    bool used;
} GlyphEntry;

typedef struct {
    uint32_t id;
    uint64_t generation;
    GLuint tex;
    bool seen;
} ImageTexture;

typedef struct {
    Vertex *v;
    size_t len;
    size_t cap;
} VertexList;

struct Renderer {
    Atlas atlas[2];
    GlyphEntry *glyphs;
    size_t glyph_cap;
    size_t glyph_len;
    bool atlas_full;
    VertexList bg;
    VertexList fg;
    ImageTexture *images;
    size_t nimages;
    size_t images_cap;
};

static GLuint program;
static GLuint vbo;
static GLuint ibo;
static GLint u_size;
static int max_texture_size;

enum { QUADS_PER_BATCH = 16000 };

static const char *vertex_src =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "attribute vec4 a_color;\n"
    "attribute float a_kind;\n"
    "uniform vec2 u_size;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_color;\n"
    "varying float v_kind;\n"
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    v_color = a_color;\n"
    "    v_kind = a_kind;\n"
    "    vec2 p = a_pos / u_size * 2.0 - 1.0;\n"
    "    gl_Position = vec4(p.x, -p.y, 0.0, 1.0);\n"
    "}\n";

static const char *fragment_src =
    "precision mediump float;\n"
    "uniform sampler2D u_gray;\n"
    "uniform sampler2D u_rgba;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_color;\n"
    "varying float v_kind;\n"
    "void main() {\n"
    "    if (v_kind < 0.5) gl_FragColor = v_color;\n"
    "    else if (v_kind < 1.5) gl_FragColor = v_color * texture2D(u_gray, v_uv).a;\n"
    "    else gl_FragColor = texture2D(u_rgba, v_uv) * v_color.a;\n"
    "}\n";

static GLuint compile(GLenum type, const char *src, char *err, size_t errlen)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        snprintf(err, errlen, "shader compilation failed: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool renderer_init_gl(char *err, size_t errlen)
{
    if (program) return true;
    GLuint vs = compile(GL_VERTEX_SHADER, vertex_src, err, errlen);
    if (!vs) return false;
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragment_src, err, errlen);
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }
    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "a_pos");
    glBindAttribLocation(program, 1, "a_uv");
    glBindAttribLocation(program, 2, "a_color");
    glBindAttribLocation(program, 3, "a_kind");
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        glGetProgramInfoLog(program, sizeof log, NULL, log);
        snprintf(err, errlen, "shader link failed: %s", log);
        glDeleteProgram(program);
        program = 0;
        return false;
    }
    glUseProgram(program);
    u_size = glGetUniformLocation(program, "u_size");
    glUniform1i(glGetUniformLocation(program, "u_gray"), 0);
    glUniform1i(glGetUniformLocation(program, "u_rgba"), 1);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);

    uint16_t *indices = xmalloc(sizeof(uint16_t) * 6 * QUADS_PER_BATCH);
    for (int q = 0; q < QUADS_PER_BATCH; q++) {
        uint16_t b = (uint16_t)(q * 4);
        uint16_t *p = indices + q * 6;
        p[0] = b;
        p[1] = b + 1;
        p[2] = b + 2;
        p[3] = b + 2;
        p[4] = b + 1;
        p[5] = b + 3;
    }
    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(sizeof(uint16_t) * 6 * QUADS_PER_BATCH), indices, GL_STATIC_DRAW);
    free(indices);
    glGenBuffers(1, &vbo);
    return true;
}

static void atlas_init(Atlas *a, bool color)
{
    a->color = color;
    a->size = MIN(2048, max_texture_size > 0 ? max_texture_size : 2048);
    a->x = a->y = a->row_h = 0;
    glGenTextures(1, &a->tex);
    glBindTexture(GL_TEXTURE_2D, a->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GLenum format = color ? GL_RGBA : GL_ALPHA;
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)format, a->size, a->size, 0, format, GL_UNSIGNED_BYTE, NULL);
}

static bool atlas_alloc(Atlas *a, int w, int h, int *x, int *y)
{
    if (w > a->size || h > a->size) return false;
    if (a->x + w > a->size) {
        a->x = 0;
        a->y += a->row_h;
        a->row_h = 0;
    }
    if (a->y + h > a->size) return false;
    *x = a->x;
    *y = a->y;
    a->x += w;
    a->row_h = MAX(a->row_h, h);
    return true;
}

Renderer *renderer_new(void)
{
    Renderer *r = xcalloc(1, sizeof(Renderer));
    atlas_init(&r->atlas[ATLAS_GRAY], false);
    atlas_init(&r->atlas[ATLAS_COLOR], true);
    return r;
}

static void free_images(Renderer *r)
{
    for (size_t i = 0; i < r->nimages; i++) glDeleteTextures(1, &r->images[i].tex);
    free(r->images);
    r->images = NULL;
    r->nimages = r->images_cap = 0;
}

void renderer_free(Renderer *r)
{
    if (!r) return;
    for (int i = 0; i < 2; i++) glDeleteTextures(1, &r->atlas[i].tex);
    free_images(r);
    free(r->glyphs);
    free(r->bg.v);
    free(r->fg.v);
    free(r);
}

void renderer_reset_glyphs(Renderer *r)
{
    free(r->glyphs);
    r->glyphs = NULL;
    r->glyph_cap = r->glyph_len = 0;
    for (int i = 0; i < 2; i++) r->atlas[i].x = r->atlas[i].y = r->atlas[i].row_h = 0;
    r->atlas_full = false;
}

static GlyphEntry *glyph_slot(Renderer *r, uint64_t key)
{
    if ((r->glyph_len + 1) * 10 >= r->glyph_cap * 7) {
        size_t old_cap = r->glyph_cap;
        GlyphEntry *old = r->glyphs;
        r->glyph_cap = old_cap ? old_cap * 2 : 1024;
        r->glyphs = xcalloc(r->glyph_cap, sizeof(GlyphEntry));
        for (size_t i = 0; i < old_cap; i++) {
            if (!old[i].used) continue;
            size_t j = old[i].key & (r->glyph_cap - 1);
            while (r->glyphs[j].used) j = (j + 1) & (r->glyph_cap - 1);
            r->glyphs[j] = old[i];
        }
        free(old);
    }
    size_t j = key & (r->glyph_cap - 1);
    while (r->glyphs[j].used && r->glyphs[j].key != key) j = (j + 1) & (r->glyph_cap - 1);
    return &r->glyphs[j];
}

static const GlyphEntry *glyph_get(Renderer *r, Fonts *fonts, const uint32_t *cps, size_t n, int style, int cells)
{
    uint64_t key = hash_bytes(cps, n * sizeof(uint32_t), (uint64_t)style << 8 | (uint64_t)cells);
    GlyphEntry *e = glyph_slot(r, key);
    if (e->used) return e;
    GlyphBitmap bm;
    if (!fonts_render(fonts, cps, n, style, cells, &bm)) {
        *e = (GlyphEntry){.key = key, .atlas = ATLAS_NONE, .used = true};
        r->glyph_len++;
        return e;
    }
    Atlas *a = &r->atlas[bm.color ? ATLAS_COLOR : ATLAS_GRAY];
    int x, y;
    if (!atlas_alloc(a, bm.width, bm.height, &x, &y)) {
        r->atlas_full = true;
        return NULL;
    }
    glBindTexture(GL_TEXTURE_2D, a->tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, bm.width, bm.height, bm.color ? GL_RGBA : GL_ALPHA, GL_UNSIGNED_BYTE, bm.pixels);
    *e = (GlyphEntry){
        .key = key,
        .x = (uint16_t)x,
        .y = (uint16_t)y,
        .w = (uint16_t)bm.width,
        .h = (uint16_t)bm.height,
        .atlas = bm.color ? ATLAS_COLOR : ATLAS_GRAY,
        .used = true,
    };
    r->glyph_len++;
    return e;
}

static void push_quad(VertexList *l, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Rgba c, float kind)
{
    if (l->len + 4 > l->cap) {
        l->cap = l->cap ? l->cap * 2 : 4096;
        l->v = xrealloc(l->v, l->cap * sizeof(Vertex));
    }
    Vertex *v = l->v + l->len;
    v[0] = (Vertex){x0, y0, u0, v0, c.r, c.g, c.b, c.a, kind};
    v[1] = (Vertex){x1, y0, u1, v0, c.r, c.g, c.b, c.a, kind};
    v[2] = (Vertex){x0, y1, u0, v1, c.r, c.g, c.b, c.a, kind};
    v[3] = (Vertex){x1, y1, u1, v1, c.r, c.g, c.b, c.a, kind};
    l->len += 4;
}

static Rgba premul(GhosttyColorRgb c, double alpha)
{
    alpha = CLAMP(alpha, 0.0, 1.0);
    return (Rgba){
        (uint8_t)lround(c.r * alpha),
        (uint8_t)lround(c.g * alpha),
        (uint8_t)lround(c.b * alpha),
        (uint8_t)lround(255 * alpha),
    };
}

static void push_glyph(Renderer *r, const GlyphEntry *g, float x, float y, Rgba color)
{
    if (!g || g->atlas == ATLAS_NONE) return;
    float s = (float)r->atlas[g->atlas].size;
    push_quad(&r->fg, x, y, x + g->w, y + g->h, g->x / s, g->y / s, (g->x + g->w) / s, (g->y + g->h) / s, color,
              g->atlas == ATLAS_COLOR ? KIND_RGBA : KIND_GRAY);
}

static void draw_list(VertexList *l)
{
    if (!l->len) return;
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    for (size_t start = 0; start < l->len; start += QUADS_PER_BATCH * 4) {
        size_t count = MIN(l->len - start, (size_t)QUADS_PER_BATCH * 4);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count * sizeof(Vertex)), l->v + start, GL_STREAM_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, x));
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, u));
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void *)offsetof(Vertex, r));
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, kind));
        glDrawElements(GL_TRIANGLES, (GLsizei)(count / 4 * 6), GL_UNSIGNED_SHORT, NULL);
    }
    l->len = 0;
}

static ImageTexture *image_texture(Renderer *r, GhosttyKittyGraphicsImage image, uint32_t id)
{
    uint64_t generation = 0;
    ghostty_kitty_graphics_image_get(image, GHOSTTY_KITTY_IMAGE_DATA_GENERATION, &generation);
    ImageTexture *t = NULL;
    for (size_t i = 0; i < r->nimages; i++)
        if (r->images[i].id == id) t = &r->images[i];
    if (t && t->generation == generation) {
        t->seen = true;
        return t;
    }
    uint32_t w = 0, h = 0;
    GhosttyKittyImageFormat fmt = GHOSTTY_KITTY_IMAGE_FORMAT_RGBA;
    const uint8_t *data = NULL;
    size_t len = 0;
    ghostty_kitty_graphics_image_get(image, GHOSTTY_KITTY_IMAGE_DATA_WIDTH, &w);
    ghostty_kitty_graphics_image_get(image, GHOSTTY_KITTY_IMAGE_DATA_HEIGHT, &h);
    ghostty_kitty_graphics_image_get(image, GHOSTTY_KITTY_IMAGE_DATA_FORMAT, &fmt);
    ghostty_kitty_graphics_image_get(image, GHOSTTY_KITTY_IMAGE_DATA_DATA_PTR, &data);
    ghostty_kitty_graphics_image_get(image, GHOSTTY_KITTY_IMAGE_DATA_DATA_LEN, &len);
    int channels = 0;
    switch (fmt) {
    case GHOSTTY_KITTY_IMAGE_FORMAT_RGBA:
    case GHOSTTY_KITTY_IMAGE_FORMAT_PNG: channels = 4; break;
    case GHOSTTY_KITTY_IMAGE_FORMAT_RGB: channels = 3; break;
    case GHOSTTY_KITTY_IMAGE_FORMAT_GRAY_ALPHA: channels = 2; break;
    case GHOSTTY_KITTY_IMAGE_FORMAT_GRAY: channels = 1; break;
    default: return NULL;
    }
    size_t pixels = (size_t)w * h;
    if (!w || !h || !data || len < pixels * (size_t)channels || (int)MAX(w, h) > max_texture_size) return NULL;
    uint8_t *rgba = xmalloc(pixels * 4);
    for (size_t i = 0; i < pixels; i++) {
        const uint8_t *s = data + i * (size_t)channels;
        uint8_t r8, g8, b8, a8;
        switch (channels) {
        case 4: r8 = s[0], g8 = s[1], b8 = s[2], a8 = s[3]; break;
        case 3: r8 = s[0], g8 = s[1], b8 = s[2], a8 = 255; break;
        case 2: r8 = g8 = b8 = s[0], a8 = s[1]; break;
        default: r8 = g8 = b8 = s[0], a8 = 255; break;
        }
        rgba[i * 4 + 0] = (uint8_t)((r8 * a8 + 127) / 255);
        rgba[i * 4 + 1] = (uint8_t)((g8 * a8 + 127) / 255);
        rgba[i * 4 + 2] = (uint8_t)((b8 * a8 + 127) / 255);
        rgba[i * 4 + 3] = a8;
    }
    if (!t) {
        if (r->nimages == r->images_cap) {
            r->images_cap = r->images_cap ? r->images_cap * 2 : 8;
            r->images = xrealloc(r->images, r->images_cap * sizeof(ImageTexture));
        }
        t = &r->images[r->nimages++];
        *t = (ImageTexture){.id = id};
        glGenTextures(1, &t->tex);
    }
    t->generation = generation;
    t->seen = true;
    glBindTexture(GL_TEXTURE_2D, t->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);
    return t;
}

static void draw_images(Renderer *r, const RenderFrame *f, GhosttyKittyPlacementLayer layer)
{
    GhosttyKittyGraphics graphics = NULL;
    if (!f->placements || ghostty_terminal_get(f->terminal, GHOSTTY_TERMINAL_DATA_KITTY_GRAPHICS, &graphics) != GHOSTTY_SUCCESS || !graphics)
        return;
    GhosttyKittyGraphicsPlacementIterator it = f->placements;
    ghostty_kitty_graphics_placement_iterator_set(it, GHOSTTY_KITTY_GRAPHICS_PLACEMENT_ITERATOR_OPTION_LAYER, &layer);
    if (ghostty_kitty_graphics_get(graphics, GHOSTTY_KITTY_GRAPHICS_DATA_PLACEMENT_ITERATOR, &it) != GHOSTTY_SUCCESS) return;
    const FontMetrics *m = fonts_metrics(f->fonts);
    VertexList quad = {0};
    while (ghostty_kitty_graphics_placement_next(it)) {
        uint32_t id = 0;
        ghostty_kitty_graphics_placement_get(it, GHOSTTY_KITTY_GRAPHICS_PLACEMENT_DATA_IMAGE_ID, &id);
        GhosttyKittyGraphicsImage image = ghostty_kitty_graphics_image(graphics, id);
        if (!image) continue;
        int32_t col = 0, row = 0;
        if (ghostty_kitty_graphics_placement_viewport_pos(it, image, f->terminal, &col, &row) != GHOSTTY_SUCCESS) continue;
        uint32_t cols = 0, rows = 0;
        if (ghostty_kitty_graphics_placement_grid_size(it, image, f->terminal, &cols, &rows) != GHOSTTY_SUCCESS || !cols || !rows) continue;
        uint32_t sx = 0, sy = 0, sw = 0, sh = 0;
        if (ghostty_kitty_graphics_placement_source_rect(it, image, &sx, &sy, &sw, &sh) != GHOSTTY_SUCCESS) continue;
        uint32_t xoff = 0, yoff = 0, iw = 0, ih = 0;
        ghostty_kitty_graphics_placement_get(it, GHOSTTY_KITTY_GRAPHICS_PLACEMENT_DATA_X_OFFSET, &xoff);
        ghostty_kitty_graphics_placement_get(it, GHOSTTY_KITTY_GRAPHICS_PLACEMENT_DATA_Y_OFFSET, &yoff);
        ghostty_kitty_graphics_image_get(image, GHOSTTY_KITTY_IMAGE_DATA_WIDTH, &iw);
        ghostty_kitty_graphics_image_get(image, GHOSTTY_KITTY_IMAGE_DATA_HEIGHT, &ih);
        ImageTexture *t = image_texture(r, image, id);
        if (!t || !iw || !ih) continue;
        float x0 = (float)(f->pad_x + col * m->cell_w + (int)xoff);
        float y0 = (float)(f->pad_y + row * m->cell_h + (int)yoff);
        float x1 = x0 + (float)(cols * (uint32_t)m->cell_w);
        float y1 = y0 + (float)(rows * (uint32_t)m->cell_h);
        push_quad(&quad, x0, y0, x1, y1, (float)sx / iw, (float)sy / ih, (float)(sx + sw) / iw, (float)(sy + sh) / ih,
                  (Rgba){255, 255, 255, 255}, KIND_RGBA);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, t->tex);
        draw_list(&quad);
    }
    free(quad.v);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, r->atlas[ATLAS_COLOR].tex);
    glActiveTexture(GL_TEXTURE0);
}

static void evict_images(Renderer *r)
{
    size_t out = 0;
    for (size_t i = 0; i < r->nimages; i++) {
        if (r->images[i].seen) {
            r->images[i].seen = false;
            r->images[out++] = r->images[i];
        } else {
            glDeleteTextures(1, &r->images[i].tex);
        }
    }
    r->nimages = out;
}

static GhosttyColorRgb resolve_style_color(const GhosttyStyleColor *c, const GhosttyRenderStateColors *colors, GhosttyColorRgb fallback)
{
    switch (c->tag) {
    case GHOSTTY_STYLE_COLOR_PALETTE: return colors->palette[c->value.palette];
    case GHOSTTY_STYLE_COLOR_RGB: return c->value.rgb;
    default: return fallback;
    }
}

typedef struct {
    bool visible;
    uint16_t x;
    uint16_t y;
    GhosttyRenderStateCursorVisualStyle style;
    GhosttyColorRgb color;
    bool color_from_cell;
} CursorInfo;

static CursorInfo cursor_info(const RenderFrame *f, const GhosttyRenderStateColors *colors)
{
    CursorInfo c = {0};
    bool visible = false, in_viewport = false, blinking = false;
    ghostty_render_state_get(f->state, GHOSTTY_RENDER_STATE_DATA_CURSOR_VISIBLE, &visible);
    ghostty_render_state_get(f->state, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_HAS_VALUE, &in_viewport);
    ghostty_render_state_get(f->state, GHOSTTY_RENDER_STATE_DATA_CURSOR_BLINKING, &blinking);
    if (!visible || !in_viewport) return c;
    if (f->focused && blinking && f->cfg->cursor_blink_interval > 0 && !f->cursor_blink_on) return c;
    c.visible = true;
    ghostty_render_state_get(f->state, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X, &c.x);
    ghostty_render_state_get(f->state, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y, &c.y);
    c.style = GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK;
    ghostty_render_state_get(f->state, GHOSTTY_RENDER_STATE_DATA_CURSOR_VISUAL_STYLE, &c.style);
    if (!f->focused) c.style = GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK_HOLLOW;
    if (colors->cursor_has_value) c.color = colors->cursor;
    else c.color_from_cell = true;
    return c;
}

static void draw_cells(Renderer *r, const RenderFrame *f, const GhosttyRenderStateColors *colors, const CursorInfo *cursor)
{
    const FontMetrics *m = fonts_metrics(f->fonts);
    GhosttyRenderStateRowIterator rows = f->rows;
    GhosttyRenderStateRowCells cells = f->cells;
    if (ghostty_render_state_get(f->state, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &rows) != GHOSTTY_SUCCESS) return;
    const Config *cfg = f->cfg;
    Rgba cursor_quad_color = {0};
    bool cursor_block = cursor->visible && cursor->style == GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK;
    uint16_t y = 0;
    while (ghostty_render_state_row_iterator_next(rows)) {
        if (ghostty_render_state_row_get(rows, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &cells) != GHOSTTY_SUCCESS) {
            y++;
            continue;
        }
        float py = (float)(f->pad_y + y * m->cell_h);
        uint16_t x = 0;
        while (ghostty_render_state_row_cells_next(cells)) {
            float px = (float)(f->pad_x + x * m->cell_w);
            GhosttyCell raw = 0;
            ghostty_render_state_row_cells_get(cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_RAW, &raw);
            GhosttyCellWide wide = GHOSTTY_CELL_WIDE_NARROW;
            ghostty_cell_get(raw, GHOSTTY_CELL_DATA_WIDE, &wide);
            int ncells = wide == GHOSTTY_CELL_WIDE_WIDE ? 2 : 1;
            GhosttyStyle style = GHOSTTY_INIT_SIZED(GhosttyStyle);
            ghostty_render_state_row_cells_get(cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE, &style);
            GhosttyColorRgb fg = colors->foreground;
            GhosttyColorRgb bg = colors->background;
            ghostty_render_state_row_cells_get(cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR, &fg);
            bool has_bg = ghostty_render_state_row_cells_get(cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR, &bg) == GHOSTTY_SUCCESS;
            if (style.inverse) {
                GhosttyColorRgb tmp = fg;
                fg = bg;
                bg = tmp;
                has_bg = true;
            }
            if (has_bg && wide != GHOSTTY_CELL_WIDE_SPACER_TAIL)
                push_quad(&r->bg, px, py, px + (float)(m->cell_w * ncells), py + (float)m->cell_h, 0, 0, 0, 0, premul(bg, 1.0), KIND_SOLID);
            else if (has_bg)
                push_quad(&r->bg, px, py, px + (float)m->cell_w, py + (float)m->cell_h, 0, 0, 0, 0, premul(bg, 1.0), KIND_SOLID);
            bool at_cursor = cursor->visible && cursor->x == x && cursor->y == y;
            GhosttyColorRgb text_fg = fg;
            if (at_cursor) {
                GhosttyColorRgb cc = cursor->color_from_cell ? fg : cursor->color;
                cursor_quad_color = premul(cc, 1.0);
                if (cursor_block) {
                    push_quad(&r->fg, px, py, px + (float)(m->cell_w * ncells), py + (float)m->cell_h, 0, 0, 0, 0, cursor_quad_color, KIND_SOLID);
                    if (cursor->color_from_cell || cfg->cursor_text_color.special == COLOR_SPECIAL_BACKGROUND) text_fg = bg;
                    else text_fg = cfg->cursor_text_color.rgb;
                }
            }
            double alpha = style.faint ? 0.5 : 1.0;
            uint32_t len = 0;
            ghostty_render_state_row_cells_get(cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_LEN, &len);
            if (len && !style.invisible && wide != GHOSTTY_CELL_WIDE_SPACER_TAIL && wide != GHOSTTY_CELL_WIDE_SPACER_HEAD) {
                uint32_t cps[16];
                uint32_t n = MIN(len, (uint32_t)ARRAY_LEN(cps));
                if (len <= ARRAY_LEN(cps)) {
                    ghostty_render_state_row_cells_get(cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF, cps);
                } else {
                    uint32_t *all = xmalloc(len * sizeof(uint32_t));
                    ghostty_render_state_row_cells_get(cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF, all);
                    memcpy(cps, all, n * sizeof(uint32_t));
                    free(all);
                }
                if (!(n == 1 && cps[0] == ' ')) {
                    int fstyle = (style.bold ? STYLE_BOLD : 0) | (style.italic ? STYLE_ITALIC : 0);
                    push_glyph(r, glyph_get(r, f->fonts, cps, n, fstyle, ncells), px, py, premul(text_fg, alpha));
                }
            }
            if (wide != GHOSTTY_CELL_WIDE_SPACER_TAIL) {
                GhosttyColorRgb uc = resolve_style_color(&style.underline_color, colors, text_fg);
                static const uint32_t underline_sprites[] = {
                    [GHOSTTY_SGR_UNDERLINE_SINGLE] = SPRITE_UNDERLINE_SINGLE,
                    [GHOSTTY_SGR_UNDERLINE_DOUBLE] = SPRITE_UNDERLINE_DOUBLE,
                    [GHOSTTY_SGR_UNDERLINE_CURLY] = SPRITE_UNDERLINE_CURLY,
                    [GHOSTTY_SGR_UNDERLINE_DOTTED] = SPRITE_UNDERLINE_DOTTED,
                    [GHOSTTY_SGR_UNDERLINE_DASHED] = SPRITE_UNDERLINE_DASHED,
                };
                for (int c = 0; c < ncells; c++) {
                    float cx = px + (float)(c * m->cell_w);
                    if (style.underline > 0 && style.underline < (int)ARRAY_LEN(underline_sprites)) {
                        uint32_t sprite = underline_sprites[style.underline];
                        push_glyph(r, glyph_get(r, f->fonts, &sprite, 1, 0, 1), cx, py, premul(uc, alpha));
                    }
                    if (style.strikethrough) {
                        uint32_t sprite = SPRITE_STRIKETHROUGH;
                        push_glyph(r, glyph_get(r, f->fonts, &sprite, 1, 0, 1), cx, py, premul(text_fg, alpha));
                    }
                    if (style.overline) {
                        uint32_t sprite = SPRITE_OVERLINE;
                        push_glyph(r, glyph_get(r, f->fonts, &sprite, 1, 0, 1), cx, py, premul(text_fg, alpha));
                    }
                }
            }
            if (at_cursor && !cursor_block) {
                uint32_t sprite = cursor->style == GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BAR       ? SPRITE_CURSOR_BEAM
                                  : cursor->style == GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_UNDERLINE ? SPRITE_CURSOR_UNDERLINE
                                                                                                        : SPRITE_CURSOR_HOLLOW;
                int sprite_cells = sprite == SPRITE_CURSOR_BEAM ? 1 : ncells;
                push_glyph(r, glyph_get(r, f->fonts, &sprite, 1, 0, sprite_cells), px, py, cursor_quad_color);
            }
            x++;
        }
        bool clean = false;
        ghostty_render_state_row_set(rows, GHOSTTY_RENDER_STATE_ROW_OPTION_DIRTY, &clean);
        y++;
    }
}

void renderer_draw(Renderer *r, const RenderFrame *f)
{
    GhosttyRenderStateColors colors = GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
    if (ghostty_render_state_colors_get(f->state, &colors) != GHOSTTY_SUCCESS) return;
    CursorInfo cursor = cursor_info(f, &colors);

    glUseProgram(program);
    glViewport(0, 0, f->width, f->height);
    glUniform2f(u_size, (float)f->width, (float)f->height);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, r->atlas[ATLAS_COLOR].tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->atlas[ATLAS_GRAY].tex);

    for (int attempt = 0; attempt < 2; attempt++) {
        r->bg.len = r->fg.len = 0;
        r->atlas_full = false;
        draw_cells(r, f, &colors, &cursor);
        if (!r->atlas_full) break;
        log_debug(debug_rendering, "glyph atlas full, resetting");
        renderer_reset_glyphs(r);
    }

    double opacity = CLAMP(f->cfg->background_opacity, 0.0, 1.0);
    Rgba clear = premul(colors.background, opacity);
    glClearColor(clear.r / 255.0f, clear.g / 255.0f, clear.b / 255.0f, clear.a / 255.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    draw_images(r, f, GHOSTTY_KITTY_PLACEMENT_LAYER_BELOW_BG);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->atlas[ATLAS_GRAY].tex);
    draw_list(&r->bg);
    draw_images(r, f, GHOSTTY_KITTY_PLACEMENT_LAYER_BELOW_TEXT);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->atlas[ATLAS_GRAY].tex);
    draw_list(&r->fg);
    draw_images(r, f, GHOSTTY_KITTY_PLACEMENT_LAYER_ABOVE_TEXT);
    evict_images(r);

    GhosttyRenderStateDirty clean = GHOSTTY_RENDER_STATE_DIRTY_FALSE;
    ghostty_render_state_set(f->state, GHOSTTY_RENDER_STATE_OPTION_DIRTY, &clean);
}
