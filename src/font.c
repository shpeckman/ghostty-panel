// src/font.c
#include "font.h"

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYNTHESIS_H
#include FT_TRUETYPE_TABLES_H
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "boxdraw.h"
#include "util.h"

static const unsigned char embedded_regular[] = {
#embed "JetBrainsMonoNerdFont-Regular.ttf"
};
static const unsigned char embedded_bold[] = {
#embed "JetBrainsMonoNerdFont-Bold.ttf"
};
static const unsigned char embedded_italic[] = {
#embed "JetBrainsMonoNerdFont-Italic.ttf"
};
static const unsigned char embedded_bold_italic[] = {
#embed "JetBrainsMonoNerdFont-BoldItalic.ttf"
};

typedef struct {
    const unsigned char *data;
    size_t len;
} EmbeddedFont;

static const EmbeddedFont embedded_fonts[4] = {
    {embedded_regular, sizeof embedded_regular},
    {embedded_bold, sizeof embedded_bold},
    {embedded_italic, sizeof embedded_italic},
    {embedded_bold_italic, sizeof embedded_bold_italic},
};

typedef struct {
    FT_Face face;
    char *path;
    int index;
    bool synth_bold;
    bool synth_italic;
} Face;

typedef struct {
    uint32_t key;
    int16_t face;
    bool used;
} FallbackEntry;

struct Fonts {
    FT_Library lib;
    Face *faces;
    size_t nfaces;
    size_t cap;
    int primary[4];
    int symbols;
    FallbackEntry *fallback;
    size_t fallback_cap;
    size_t fallback_len;
    double pt_size;
    double dpi;
    char *family;
    FontMetrics m;
    uint8_t *scratch;
    size_t scratch_cap;
};

static FcConfig *fc_config(void)
{
    static FcConfig *config;
    static bool tried;
    if (!tried) {
        tried = true;
        config = FcInitLoadConfigAndFonts();
        if (!config) log_msg("fontconfig initialization failed, only the built in font is available");
    }
    return config;
}

static void apply_size(Fonts *f, FT_Face face, int target_height)
{
    if (FT_IS_SCALABLE(face)) {
        FT_Set_Char_Size(face, 0, (FT_F26Dot6)lround(f->pt_size * 64.0), (FT_UInt)lround(f->dpi), (FT_UInt)lround(f->dpi));
        return;
    }
    if (face->num_fixed_sizes <= 0) return;
    int best = 0;
    int best_diff = 1 << 30;
    for (int i = 0; i < face->num_fixed_sizes; i++) {
        int h = face->available_sizes[i].height;
        int diff = h >= target_height ? h - target_height : (target_height - h) * 4;
        if (diff < best_diff) {
            best = i;
            best_diff = diff;
        }
    }
    FT_Select_Size(face, best);
}

static int add_face(Fonts *f, FT_Face face, const char *path, int index, int target_height)
{
    if (f->nfaces == f->cap) {
        f->cap = f->cap ? f->cap * 2 : 8;
        f->faces = xrealloc(f->faces, f->cap * sizeof(Face));
    }
    apply_size(f, face, target_height);
    f->faces[f->nfaces] = (Face){.face = face, .path = path ? xstrdup(path) : NULL, .index = index};
    return (int)f->nfaces++;
}

static int load_file_face(Fonts *f, const char *path, int index)
{
    for (size_t i = 0; i < f->nfaces; i++)
        if (f->faces[i].path && f->faces[i].index == index && strcmp(f->faces[i].path, path) == 0) return (int)i;
    FT_Face face;
    if (FT_New_Face(f->lib, path, index, &face) != 0) return -1;
    return add_face(f, face, path, index, f->m.cell_h ? f->m.cell_h : 16);
}

static int load_embedded_face(Fonts *f, int style)
{
    FT_Face face;
    const EmbeddedFont *e = &embedded_fonts[style];
    if (FT_New_Memory_Face(f->lib, e->data, (FT_Long)e->len, 0, &face) != 0) return -1;
    return add_face(f, face, NULL, 0, 16);
}

static bool fc_match(const char *name, int style, bool name_is_explicit, char **path, int *index)
{
    FcConfig *config = fc_config();
    if (!config) return false;
    FcPattern *pat = FcNameParse((const FcChar8 *)name);
    if (!pat) return false;
    if (!name_is_explicit) {
        FcPatternAddInteger(pat, FC_WEIGHT, (style & STYLE_BOLD) ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
        FcPatternAddInteger(pat, FC_SLANT, (style & STYLE_ITALIC) ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
    }
    FcConfigSubstitute(config, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    FcResult result;
    FcPattern *match = FcFontMatch(config, pat, &result);
    FcPatternDestroy(pat);
    if (!match) return false;
    FcChar8 *file = NULL;
    int idx = 0;
    bool ok = FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch;
    if (ok) {
        FcPatternGetInteger(match, FC_INDEX, 0, &idx);
        *path = xstrdup((const char *)file);
        *index = idx;
    }
    FcPatternDestroy(match);
    return ok;
}

static bool is_auto(const char *s)
{
    return !s || !*s || strcmp(s, "auto") == 0;
}

static bool uses_embedded(const char *family)
{
    return !family || !*family || strcmp(family, "default") == 0;
}

static int load_primary(Fonts *f, const Config *cfg, int style)
{
    static const size_t offsets[4] = {
        offsetof(Config, font_family),
        offsetof(Config, bold_font),
        offsetof(Config, italic_font),
        offsetof(Config, bold_italic_font),
    };
    const char *specific = *(char *const *)((const char *)cfg + offsets[style]);
    bool explicit_name = style != 0 && !is_auto(specific);
    const char *name = explicit_name ? specific : cfg->font_family;
    if (uses_embedded(name)) return load_embedded_face(f, style);
    char *path = NULL;
    int index = 0;
    int face = -1;
    if (fc_match(name, style, explicit_name, &path, &index)) face = load_file_face(f, path, index);
    free(path);
    if (face < 0) {
        log_msg("font '%s' not found, using the built in font", name);
        return load_embedded_face(f, style);
    }
    if (style && face == f->primary[0]) {
        FT_Face base = f->faces[face].face;
        FT_Face dup;
        if (FT_New_Face(f->lib, f->faces[face].path, f->faces[face].index, &dup) == 0) {
            face = add_face(f, dup, NULL, 0, 16);
            f->faces[face].synth_bold = (style & STYLE_BOLD) && !(base->style_flags & FT_STYLE_FLAG_BOLD);
            f->faces[face].synth_italic = (style & STYLE_ITALIC) && !(base->style_flags & FT_STYLE_FLAG_ITALIC);
        }
    }
    return face;
}

static int round_up_div64(FT_Pos v)
{
    return (int)((v + 63) >> 6);
}

static void compute_metrics(Fonts *f, double scale)
{
    FT_Face face = f->faces[f->primary[0]].face;
    FontMetrics *m = &f->m;
    m->scale = scale;
    int advance = 0;
    for (uint32_t cp = 0x20; cp < 0x7F; cp++) {
        if (FT_Load_Char(face, cp, FT_LOAD_DEFAULT | FT_LOAD_TARGET_LIGHT) == 0)
            advance = MAX(advance, round_up_div64(face->glyph->advance.x));
    }
    int ascender = round_up_div64(face->size->metrics.ascender);
    int descender = (int)(face->size->metrics.descender >> 6);
    int height = MAX(round_up_div64(face->size->metrics.height), ascender - descender);
    m->cell_w = MAX(1, advance);
    m->cell_h = MAX(1, height);
    m->baseline = ascender + (height - (ascender - descender)) / 2;
    FT_Fixed y_scale = face->size->metrics.y_scale;
    int thickness = FT_IS_SCALABLE(face) ? (int)lround(FT_MulFix(face->underline_thickness, y_scale) / 64.0) : 1;
    m->underline_thickness = MAX(1, thickness);
    int upos = FT_IS_SCALABLE(face) ? (int)lround(FT_MulFix(face->underline_position, y_scale) / 64.0) : -1;
    m->underline_pos = CLAMP(m->baseline - upos - m->underline_thickness / 2, 0, m->cell_h - m->underline_thickness);
    TT_OS2 *os2 = FT_IS_SCALABLE(face) ? FT_Get_Sfnt_Table(face, FT_SFNT_OS2) : NULL;
    if (os2 && os2->yStrikeoutSize > 0) {
        m->strike_thickness = MAX(1, (int)lround(FT_MulFix(os2->yStrikeoutSize, y_scale) / 64.0));
        m->strike_pos = m->baseline - (int)lround(FT_MulFix(os2->yStrikeoutPosition, y_scale) / 64.0);
    } else {
        m->strike_thickness = m->underline_thickness;
        m->strike_pos = m->baseline - ascender / 3;
    }
    m->strike_pos = CLAMP(m->strike_pos, 0, m->cell_h - m->strike_thickness);
    m->line_light = MAX(1, (int)lround(f->dpi / 72.0));
    m->line_heavy = MAX(m->line_light + 1, (int)lround(f->dpi / 72.0 * 1.75));
}

Fonts *fonts_new(const Config *cfg, double scale, char *err, size_t errlen)
{
    Fonts *f = xcalloc(1, sizeof(Fonts));
    if (FT_Init_FreeType(&f->lib) != 0) {
        snprintf(err, errlen, "cannot initialize FreeType");
        free(f);
        return NULL;
    }
    f->pt_size = cfg->font_size > 0 ? cfg->font_size : 11.0;
    f->dpi = 96.0 * scale;
    f->family = xstrdup(uses_embedded(cfg->font_family) ? "monospace" : cfg->font_family);
    for (int style = 0; style < 4; style++) {
        f->primary[style] = load_primary(f, cfg, style);
        if (f->primary[style] < 0) {
            snprintf(err, errlen, "cannot load a font");
            fonts_free(f);
            return NULL;
        }
    }
    f->symbols = load_embedded_face(f, 0);
    compute_metrics(f, scale);
    for (size_t i = 0; i < f->nfaces; i++) apply_size(f, f->faces[i].face, f->m.cell_h);
    return f;
}

void fonts_free(Fonts *f)
{
    if (!f) return;
    for (size_t i = 0; i < f->nfaces; i++) {
        FT_Done_Face(f->faces[i].face);
        free(f->faces[i].path);
    }
    free(f->faces);
    free(f->fallback);
    free(f->family);
    free(f->scratch);
    FT_Done_FreeType(f->lib);
    free(f);
}

const FontMetrics *fonts_metrics(const Fonts *f)
{
    return &f->m;
}

static bool has_glyph(Fonts *f, int face, uint32_t cp)
{
    return face >= 0 && FT_Get_Char_Index(f->faces[face].face, cp) != 0;
}

static int fc_fallback(Fonts *f, uint32_t cp, bool want_color)
{
    FcConfig *config = fc_config();
    if (!config) return -1;
    FcPattern *pat = FcNameParse((const FcChar8 *)f->family);
    if (!pat) return -1;
    FcCharSet *cs = FcCharSetCreate();
    FcCharSetAddChar(cs, cp);
    FcPatternAddCharSet(pat, FC_CHARSET, cs);
    if (want_color) FcPatternAddBool(pat, FC_COLOR, FcTrue);
    FcConfigSubstitute(config, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    FcResult result;
    FcFontSet *set = FcFontSort(config, pat, FcTrue, NULL, &result);
    int found = -1;
    for (int pass = 0; set && pass < 2 && found < 0; pass++) {
        for (int i = 0; i < set->nfont && found < 0; i++) {
            FcCharSet *fcs = NULL;
            if (FcPatternGetCharSet(set->fonts[i], FC_CHARSET, 0, &fcs) != FcResultMatch || !FcCharSetHasChar(fcs, cp)) continue;
            FcBool color = FcFalse;
            FcPatternGetBool(set->fonts[i], FC_COLOR, 0, &color);
            if (pass == 0 && want_color != (color == FcTrue)) continue;
            FcChar8 *file = NULL;
            int idx = 0;
            if (FcPatternGetString(set->fonts[i], FC_FILE, 0, &file) != FcResultMatch) continue;
            FcPatternGetInteger(set->fonts[i], FC_INDEX, 0, &idx);
            int face = load_file_face(f, (const char *)file, idx);
            if (has_glyph(f, face, cp)) found = face;
        }
    }
    if (set) FcFontSetDestroy(set);
    FcCharSetDestroy(cs);
    FcPatternDestroy(pat);
    return found;
}

static int cached_fallback(Fonts *f, uint32_t cp, bool want_color)
{
    uint32_t key = cp | (want_color ? 0x80000000u : 0);
    if (f->fallback_len * 2 >= f->fallback_cap) {
        size_t old_cap = f->fallback_cap;
        FallbackEntry *old = f->fallback;
        f->fallback_cap = old_cap ? old_cap * 2 : 256;
        f->fallback = xcalloc(f->fallback_cap, sizeof(FallbackEntry));
        f->fallback_len = 0;
        for (size_t i = 0; i < old_cap; i++) {
            if (!old[i].used) continue;
            size_t j = hash_bytes(&old[i].key, sizeof old[i].key, 0) & (f->fallback_cap - 1);
            while (f->fallback[j].used) j = (j + 1) & (f->fallback_cap - 1);
            f->fallback[j] = old[i];
            f->fallback_len++;
        }
        free(old);
    }
    size_t j = hash_bytes(&key, sizeof key, 0) & (f->fallback_cap - 1);
    while (f->fallback[j].used) {
        if (f->fallback[j].key == key) return f->fallback[j].face;
        j = (j + 1) & (f->fallback_cap - 1);
    }
    int face = fc_fallback(f, cp, want_color);
    f->fallback[j] = (FallbackEntry){.key = key, .face = (int16_t)face, .used = true};
    f->fallback_len++;
    return face;
}

static bool prefers_color(uint32_t cp)
{
    return (cp >= 0x1F000 && cp <= 0x1FAFF) || (cp >= 0x1FC00 && cp <= 0x1FFFD);
}

static int face_for(Fonts *f, uint32_t cp, int style, bool want_color)
{
    if (want_color) {
        int face = cached_fallback(f, cp, true);
        if (face >= 0 && FT_HAS_COLOR(f->faces[face].face)) return face;
    }
    if (has_glyph(f, f->primary[style], cp)) return f->primary[style];
    if (has_glyph(f, f->primary[0], cp)) return f->primary[0];
    if (has_glyph(f, f->symbols, cp)) return f->symbols;
    return cached_fallback(f, cp, false);
}

static uint8_t *scratch(Fonts *f, size_t n)
{
    if (n > f->scratch_cap) {
        f->scratch = xrealloc(f->scratch, n);
        f->scratch_cap = n;
    }
    memset(f->scratch, 0, n);
    return f->scratch;
}

static void blit(GlyphBitmap *dst, const FT_Bitmap *src, int dx, int dy, double s)
{
    int sw = (int)src->width, sh = (int)src->rows;
    int dw = MAX(1, (int)lround(sw * s)), dh = MAX(1, (int)lround(sh * s));
    for (int y = 0; y < dh; y++) {
        int ty = dy + y;
        if (ty < 0 || ty >= dst->height) continue;
        int sy0 = (int)(y / s), sy1 = MAX(sy0 + 1, MIN(sh, (int)((y + 1) / s)));
        for (int x = 0; x < dw; x++) {
            int tx = dx + x;
            if (tx < 0 || tx >= dst->width) continue;
            int sx0 = (int)(x / s), sx1 = MAX(sx0 + 1, MIN(sw, (int)((x + 1) / s)));
            unsigned acc[4] = {0};
            unsigned count = 0;
            for (int yy = sy0; yy < sy1 && yy < sh; yy++) {
                const uint8_t *row = src->buffer + (ptrdiff_t)yy * src->pitch;
                for (int xx = sx0; xx < sx1 && xx < sw; xx++) {
                    count++;
                    switch (src->pixel_mode) {
                    case FT_PIXEL_MODE_BGRA:
                        acc[0] += row[xx * 4 + 2];
                        acc[1] += row[xx * 4 + 1];
                        acc[2] += row[xx * 4 + 0];
                        acc[3] += row[xx * 4 + 3];
                        break;
                    case FT_PIXEL_MODE_MONO:
                        acc[3] += (row[xx >> 3] & (0x80 >> (xx & 7))) ? 255 : 0;
                        break;
                    default:
                        acc[3] += row[xx];
                        break;
                    }
                }
            }
            if (!count) continue;
            if (dst->color) {
                uint8_t *p = dst->pixels + ((size_t)ty * dst->width + tx) * 4;
                for (int k = 0; k < 4; k++) p[k] = (uint8_t)(acc[k] / count);
            } else {
                uint8_t v = (uint8_t)(acc[3] / count);
                uint8_t *p = dst->pixels + (size_t)ty * dst->width + tx;
                if (*p < v) *p = v;
            }
        }
    }
}

static bool render_codepoint(Fonts *f, uint32_t cp, int style, bool want_color, GlyphBitmap *out, bool first)
{
    int fi = face_for(f, cp, style, want_color);
    if (fi < 0) return false;
    Face *face = &f->faces[fi];
    FT_Int32 flags = FT_LOAD_DEFAULT | FT_LOAD_TARGET_LIGHT;
    if (FT_HAS_COLOR(face->face)) flags |= FT_LOAD_COLOR;
    if (FT_Load_Glyph(face->face, FT_Get_Char_Index(face->face, cp), flags) != 0) return false;
    FT_GlyphSlot slot = face->face->glyph;
    if (face->synth_bold) FT_GlyphSlot_Embolden(slot);
    if (face->synth_italic) FT_GlyphSlot_Oblique(slot);
    if (slot->format != FT_GLYPH_FORMAT_BITMAP && FT_Render_Glyph(slot, FT_RENDER_MODE_LIGHT) != 0) return false;
    const FT_Bitmap *bm = &slot->bitmap;
    bool color = bm->pixel_mode == FT_PIXEL_MODE_BGRA;
    if (first) {
        out->color = color;
        out->pixels = scratch(f, (size_t)out->width * out->height * (color ? 4 : 1));
    } else if (color != out->color) {
        return true;
    }
    if (!bm->width || !bm->rows) return true;
    double s = 1.0;
    int dx, dy;
    if (color || !FT_IS_SCALABLE(face->face)) {
        s = MIN((double)out->width / bm->width, (double)out->height / bm->rows);
        s = MIN(s, color ? 1e9 : 1.0);
        dx = (out->width - (int)lround(bm->width * s)) / 2;
        dy = (out->height - (int)lround(bm->rows * s)) / 2;
    } else if ((int)bm->width > out->width + 1) {
        s = (double)out->width / bm->width;
        dx = 0;
        dy = f->m.baseline - (int)lround(slot->bitmap_top * s);
    } else {
        int advance = round_up_div64(slot->advance.x);
        int offset = advance < out->width ? (out->width - advance) / 2 : 0;
        dx = slot->bitmap_left + offset;
        if (dx + (int)bm->width > out->width) dx = out->width - (int)bm->width;
        dx = MAX(dx, 0);
        dy = f->m.baseline - slot->bitmap_top;
    }
    blit(out, bm, dx, dy, s);
    return true;
}

static bool skip_in_cluster(uint32_t cp)
{
    return cp == 0x200D || cp == 0xFE0E || cp == 0xFE0F || (cp >= 0x1F3FB && cp <= 0x1F3FF);
}

bool fonts_render(Fonts *f, const uint32_t *cps, size_t n, int style, int cells, GlyphBitmap *out)
{
    if (!n) return false;
    out->width = f->m.cell_w * MAX(1, cells);
    out->height = f->m.cell_h;
    if (boxdraw_supports(cps[0])) {
        out->color = false;
        out->pixels = scratch(f, (size_t)out->width * out->height);
        boxdraw_render(cps[0], &f->m, out->width, out->height, out->pixels);
        return true;
    }
    bool want_color = prefers_color(cps[0]);
    for (size_t i = 1; i < n; i++) {
        if (cps[i] == 0xFE0F) want_color = true;
        if (cps[i] == 0xFE0E) want_color = false;
    }
    if (!render_codepoint(f, cps[0], style & 3, want_color, out, true)) return false;
    if (out->color) return true;
    for (size_t i = 1; i < n; i++) {
        if (skip_in_cluster(cps[i])) continue;
        render_codepoint(f, cps[i], style & 3, false, out, false);
    }
    return true;
}
