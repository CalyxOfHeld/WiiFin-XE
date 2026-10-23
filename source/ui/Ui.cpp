#include "Ui.h"
#include "../core/Text.h"
#include "../core/Lang.h"
#include "../input/Input.h"

#include <gccore.h>
#include <math.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <ogc/lwp_watchdog.h>

/* GRRLIB's 2D model-view matrix (defined in GRRLIB_core.c, not in its headers) */
extern Mtx GXmodelView2D;

namespace Ui {

/* ---- Palettes ----------------------------------------------------------- */

static const Palette LIGHT = {
    /* bgTop, bgBottom, stripe */        0xF4F5F6FF, 0xDCDFE3FF, 0x0000000A,
    /* cardTop, cardBottom, cardBorder */0xFFFFFFFF, 0xEEF0F2FF, 0xB4B9BFFF,
    /* text, textDim, textOnAccent */    0x4C4F54FF, 0x8B9097FF, 0xFFFFFFFF,
    /* accent, accentDark, glow */       0x34BEEDFF, 0x0A93C6FF, 0x34BEED90,
    /* shadow */                         0x00000030,
    /* barTop, barBottom, barBorder */   0xF0F1F3FF, 0xCDD1D6FF, 0xA7ADB4FF,
    /* field, fieldBorder */             0xFFFFFFFF, 0xC3C8CEFF,
    /* danger, ok */                     0xE0524DFF, 0x3DAF5AFF,
    /* dim */                            0x000000A0,
};

static const Palette DARK = {
    /* bgTop, bgBottom, stripe */        0x1D2736FF, 0x0D131CFF, 0xFFFFFF07,
    /* cardTop, cardBottom, cardBorder */0x2E3A4CFF, 0x232D3BFF, 0x43526AFF,
    /* text, textDim, textOnAccent */    0xEDF1F6FF, 0x8E9DB0FF, 0xFFFFFFFF,
    /* accent, accentDark, glow */       0x34BEEDFF, 0x5CCFF4FF, 0x34BEED80,
    /* shadow */                         0x00000070,
    /* barTop, barBottom, barBorder */   0x2A3546FF, 0x1A222FFF, 0x3B485CFF,
    /* field, fieldBorder */             0x18202BFF, 0x3B485CFF,
    /* danger, ok */                     0xF0605AFF, 0x4CC46AFF,
    /* dim */                            0x000000B0,
};

static const Palette FLIX = {
    /* bgTop, bgBottom, stripe */        0x303030FF, 0x111111FF, 0x00000000,
    /* cardTop, cardBottom, cardBorder */0x4A4A4AFF, 0x363636FF, 0x5E5E5EFF,
    /* text, textDim, textOnAccent */    0xF2F2F2FF, 0xA8A8A8FF, 0xFFFFFFFF,
    /* accent, accentDark, glow */       0xD81F26FF, 0xFF6B6FFF, 0xFF333370,
    /* shadow */                         0x00000090,
    /* barTop, barBottom, barBorder */   0x3C3C3CFF, 0x222222FF, 0x555555FF,
    /* field, fieldBorder */             0x1C1C1CFF, 0x555555FF,
    /* danger, ok */                     0xFF5A52FF, 0x46C35FFF,
    /* dim */                            0x000000B8,
};

static const Palette LUHU = {
    /* bgTop, bgBottom, stripe */        0x161616FF, 0x000000FF, 0xFFFFFF05,
    /* cardTop, cardBottom, cardBorder */0x55595EFF, 0x34373BFF, 0xA9AEB4FF,
    /* text, textDim, textOnAccent */    0xF2F3F4FF, 0xA4A9AFFF, 0x0E1A06FF,
    /* accent, accentDark, glow */       0x7DC242FF, 0xA4DD6FFF, 0x7DC24270,
    /* shadow */                         0x00000090,
    /* barTop, barBottom, barBorder */   0x4A4E53FF, 0x26282BFF, 0x8E9399FF,
    /* field, fieldBorder */             0x101010FF, 0x6E7379FF,
    /* danger, ok */                     0xF0605AFF, 0x7DC242FF,
    /* dim */                            0x000000C0,
};

static const Palette SUSHI = {
    /* bgTop, bgBottom, stripe */        0x2E2E2EFF, 0x1A1A1AFF, 0xFFFFFF05,
    /* cardTop, cardBottom, cardBorder */0x424242FF, 0x333333FF, 0x5C5C5CFF,
    /* text, textDim, textOnAccent */    0xF4F4F4FF, 0xABABABFF, 0xFFFFFFFF,
    /* accent, accentDark, glow */       0xF47521FF, 0xFF9C57FF, 0xF4752170,
    /* shadow */                         0x00000080,
    /* barTop, barBottom, barBorder */   0x3A3A3AFF, 0x242424FF, 0x585858FF,
    /* field, fieldBorder */             0x1E1E1EFF, 0x585858FF,
    /* danger, ok */                     0xF0605AFF, 0x4CC46AFF,
    /* dim */                            0x000000B8,
};

static Theme           s_theme = Theme::Dark;
static HomeLayout      s_layout = HomeLayout::Rows;
void       setHomeLayout(HomeLayout l) { s_layout = l; }
static LibraryStyle s_libStyle = LibraryStyle::ListCover;
void         setLibraryStyle(LibraryStyle s) { s_libStyle = s; }
LibraryStyle libraryStyle()                  { return s_libStyle; }
HomeLayout homeLayout()                { return s_layout; }
static GRRLIB_ttfFont* s_font  = nullptr;

void        setTheme(Theme t)   { s_theme = t; }
Theme       theme()             { return s_theme; }
Theme       nextTheme(Theme t)  { return (Theme)(((int)t + 1) % THEME_COUNT); }
const char* themeName(Theme t)
{
    switch (t) {
    case Theme::Dark: return N_("Dark");
    case Theme::Flix: return N_("Flix");
    case Theme::Luhu: return N_("Luhu");
    case Theme::Sushi: return N_("Sushi");
    default:          return N_("Light");
    }
}
static const Palette& themePal()
{
    switch (s_theme) {
    case Theme::Dark: return DARK;
    case Theme::Flix: return FLIX;
    case Theme::Luhu: return LUHU;
    case Theme::Sushi: return SUSHI;
    default:          return LIGHT;
    }
}
/* Solid colours: each gradient becomes its middle colour, without the
 * pinstripes (the bands a gradient shows on a 480i TV, issue 22) */
static bool s_solid = false;
void setSolidColors(bool on) { s_solid = on; }
static bool s_clock12 = false;
void setClock12h(bool on) { s_clock12 = on; }
bool clock12h()           { return s_clock12; }
static bool s_dimRows = true;
void setDimRows(bool on) { s_dimRows = on; }
bool dimRows()           { return s_dimRows; }
static bool s_classicHeader = false;
void setClassicHeader(bool on) { s_classicHeader = on; }
bool classicHeader()           { return s_classicHeader; }
bool solidColors()           { return s_solid; }
const Palette& pal()
{
    if (!s_solid) return themePal();
    static Palette flat;
    static int     flatFor = -1;
    if (flatFor != (int)s_theme) {
        flat = themePal();
        flat.bgTop  = flat.bgBottom   = mix(flat.bgTop,   flat.bgBottom,   0.5f);
        flat.cardTop = flat.cardBottom = mix(flat.cardTop, flat.cardBottom, 0.5f);
        flat.barTop = flat.barBottom  = mix(flat.barTop,  flat.barBottom,  0.5f);
        flat.stripe = 0;
        flatFor = (int)s_theme;
    }
    return flat;
}
void setFont(GRRLIB_ttfFont* f) { s_font = f; }
GRRLIB_ttfFont* font()          { return s_font; }

/* ---- Colour helpers ------------------------------------------------------ */

u32 mix(u32 a, u32 b, float t)
{
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    u32 out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        int ca = (a >> sh) & 0xFF, cb = (b >> sh) & 0xFF;
        out |= (u32)(ca + (cb - ca) * t + 0.5f) << sh;
    }
    return out;
}

u32 alpha(u32 c, float k)
{
    if (k >= 1.0f) return c;
    if (k <= 0.0f) return c & 0xFFFFFF00;
    return (c & 0xFFFFFF00) | (u32)((c & 0xFF) * k + 0.5f);
}

float approach(float cur, float target, float speed)
{
    float d = target - cur;
    if (d > -0.002f && d < 0.002f) return target;
    return cur + d * speed;
}

/* ---- Geometry ------------------------------------------------------------ */

static const int CORNER_SEGS = 7;
static const int MAX_PTS     = 4 * (CORNER_SEGS + 1);

struct Outline {
    int   n;
    float x[MAX_PTS], y[MAX_PTS];     /* perimeter, clockwise from the left of the top-left arc */
    float nx[MAX_PTS], ny[MAX_PTS];   /* outward normals */
};

static void outline(Outline& o, float x, float y, float w, float h, float r)
{
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    if (r < 0.0f) r = 0.0f;
    const float cx[4] = { x + r, x + w - r, x + w - r, x + r };
    const float cy[4] = { y + r, y + r,     y + h - r, y + h - r };
    o.n = 0;
    for (int c = 0; c < 4; ++c) {
        float a0 = (float)M_PI * (1.0f + 0.5f * c);
        for (int i = 0; i <= CORNER_SEGS; ++i) {
            float a = a0 + (float)M_PI * 0.5f * i / CORNER_SEGS;
            float ca = cosf(a), sa = sinf(a);
            o.x[o.n]  = cx[c] + r * ca;
            o.y[o.n]  = cy[c] + r * sa;
            o.nx[o.n] = ca;
            o.ny[o.n] = sa;
            ++o.n;
        }
    }
}

static inline void vtx(float x, float y, u32 c)
{
    GX_Position3f32(x, y, 0.0f);
    GX_Color1u32(c);
}

static void beginShapes()
{
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetVtxDesc(GX_VA_TEX0, GX_NONE);
    GX_LoadPosMtxImm(GXmodelView2D, GX_PNMTX0);
}

static inline u32 gradAt(float y, float y0, float h, u32 top, u32 bottom)
{
    return top == bottom ? top : mix(top, bottom, h > 0.0f ? (y - y0) / h : 0.0f);
}

/* Band from the outline to the outline pushed out by `width`, colour
 * inner -> outer (used for anti-aliasing feathers, shadows and glows). */
static void band(const Outline& o, float inset, float width, u32 inner, u32 outer,
                 float y0, float h, u32 top, u32 bottom, bool gradInner)
{
    GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, (o.n + 1) * 2);
    for (int k = 0; k <= o.n; ++k) {
        int i = k % o.n;
        float ix = o.x[i] - o.nx[i] * inset, iy = o.y[i] - o.ny[i] * inset;
        u32 ci = gradInner ? gradAt(iy, y0, h, top, bottom) : inner;
        u32 co = gradInner ? (ci & 0xFFFFFF00) : outer;
        vtx(ix, iy, ci);
        vtx(o.x[i] + o.nx[i] * width, o.y[i] + o.ny[i] * width, co);
    }
    GX_End();
}

void roundRect(float x, float y, float w, float h, float r, u32 top, u32 bottom)
{
    if (w <= 0 || h <= 0) return;
    Outline o;
    outline(o, x, y, w, h, r);
    beginShapes();
    GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, o.n + 2);
    vtx(x + w * 0.5f, y + h * 0.5f, gradAt(y + h * 0.5f, y, h, top, bottom));
    for (int k = 0; k <= o.n; ++k) {
        int i = k % o.n;
        vtx(o.x[i], o.y[i], gradAt(o.y[i], y, h, top, bottom));
    }
    GX_End();
    band(o, 0.0f, 1.0f, 0, 0, y, h, top, bottom, true);   /* anti-aliased edge */
}

void roundBorder(float x, float y, float w, float h, float r, float t, u32 color)
{
    if (w <= 0 || h <= 0) return;
    Outline o;
    outline(o, x, y, w, h, r);
    beginShapes();
    band(o, t, 0.0f, color, color, 0, 0, 0, 0, false);              /* solid ring  */
    band(o, 0.0f, 1.0f, color, color & 0xFFFFFF00, 0, 0, 0, 0, false);  /* outer AA */
    band(o, t + 1.0f, -t, color & 0xFFFFFF00, color, 0, 0, 0, 0, false); /* inner AA */
}

void shadow(float x, float y, float w, float h, float r, float blur, u32 color)
{
    Outline o;
    outline(o, x, y, w, h, r);
    beginShapes();
    GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, o.n + 2);
    vtx(x + w * 0.5f, y + h * 0.5f, color);
    for (int k = 0; k <= o.n; ++k) { int i = k % o.n; vtx(o.x[i], o.y[i], color); }
    GX_End();
    band(o, 0.0f, blur, color, color & 0xFFFFFF00, 0, 0, 0, 0, false);
}

void circle(float cx, float cy, float rad, u32 color)
{
    roundRect(cx - rad, cy - rad, rad * 2, rad * 2, rad, color);
}

void texRound(GRRLIB_texImg* tex, float x, float y, float w, float h, float r, u32 tint)
{
    texRound(tex, x, y, w, h, r, 0.0f, 0.0f, 1.0f, 1.0f, tint);
}

void texCover(GRRLIB_texImg* tex, float x, float y, float w, float h, float r,
              float aspect, u32 tint)
{
    if (!tex || tex->w == 0 || tex->h == 0) return;
    float ta = (float)tex->w / tex->h;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    if (ta > aspect) { float k = aspect / ta; u0 = (1 - k) * 0.5f; u1 = u0 + k; }
    else             { float k = ta / aspect; v0 = (1 - k) * 0.5f; v1 = v0 + k; }
    texRound(tex, x, y, w, h, r, u0, v0, u1, v1, tint);
}

void texRound(GRRLIB_texImg* tex, float x, float y, float w, float h, float r,
              float u0, float v0, float u1, float v1, u32 tint)
{
    if (!tex || !tex->data || w <= 0 || h <= 0) return;
    Outline o;
    outline(o, x, y, w, h, r);
    GXTexObj obj;
    GX_InitTexObj(&obj, tex->data, tex->w, tex->h, tex->format, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjLOD(&obj, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GX_LoadTexObj(&obj, GX_TEXMAP0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_LoadPosMtxImm(GXmodelView2D, GX_PNMTX0);
    GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, o.n + 2);
    const float du = u1 - u0, dv = v1 - v0;
    GX_Position3f32(x + w * 0.5f, y + h * 0.5f, 0.0f); GX_Color1u32(tint);
    GX_TexCoord2f32(u0 + du * 0.5f, v0 + dv * 0.5f);
    for (int k = 0; k <= o.n; ++k) {
        int i = k % o.n;
        GX_Position3f32(o.x[i], o.y[i], 0.0f);
        GX_Color1u32(tint);
        GX_TexCoord2f32(u0 + du * (o.x[i] - x) / w, v0 + dv * (o.y[i] - y) / h);
    }
    GX_End();
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetVtxDesc(GX_VA_TEX0, GX_NONE);
}

void triangle(float x0, float y0, float x1, float y1, float x2, float y2, u32 c)
{
    beginShapes();
    GX_Begin(GX_TRIANGLES, GX_VTXFMT0, 3);
    vtx(x0, y0, c); vtx(x1, y1, c); vtx(x2, y2, c);
    GX_End();
}

void icon(Icon i, float cx, float cy, u32 c)
{
    switch (i) {
    case Icon::Search: {       /* magnifier */
        const float mx = cx - 2, my = cy - 1;
        roundBorder(mx - 5, my - 5, 10, 10, 5, 2.0f, c);
        triangle(mx + 3, my + 4, mx + 5, my + 2, mx + 9, my + 8, c);
        triangle(mx + 3, my + 4, mx + 9, my + 8, mx + 7, my + 10, c);
        break;
    }
    case Icon::Home:           /* house: roof, walls, a door */
        triangle(cx - 7, cy - 1, cx, cy - 7, cx + 7, cy - 1, c);
        roundBorder(cx - 4.5f, cy - 2, 9, 8, 1, 2.0f, c);
        roundRect(cx - 1, cy + 2, 2, 4, 0, c);
        break;
    case Icon::Browse:         /* four tiles */
        for (int k = 0; k < 4; ++k)
            roundBorder(cx - 6 + (k % 2) * 7, cy - 6 + (k / 2) * 7, 5, 5, 1, 1.5f, c);
        break;
    case Icon::Menu:           /* three bars */
        for (int k = 0; k < 3; ++k)
            roundRect(cx - 6, cy - 5 + k * 4.5f, 12, 2, 1, c);
        break;
    case Icon::Play:
        triangle(cx - 4, cy - 6, cx + 6, cy, cx - 4, cy + 6, c);
        break;
    case Icon::Back:           /* arrow to the left */
        triangle(cx - 7, cy, cx - 1, cy - 6, cx - 1, cy + 6, c);
        roundRect(cx - 2, cy - 1.5f, 9, 3, 1, c);
        break;
    case Icon::Profile:        /* head and shoulders */
        roundBorder(cx - 3.5f, cy - 7, 7, 7, 3.5f, 2.0f, c);
        roundRect(cx - 6, cy + 1.5f, 12, 5.5f, 2.5f, c);
        break;
    case Icon::Settings:       /* three sliders */
        for (int k = 0; k < 3; ++k) {
            const float y = cy - 5 + k * 5, kx = cx + (k == 1 ? 3 : -3);
            roundRect(cx - 7, y - 1, 14, 2, 1, c);
            circle(kx, y, 2.5f, c);
        }
        break;
    case Icon::Folder:         /* a folder: its tab, its body */
        roundRect(cx - 7, cy - 6, 6, 3, 1, c);
        roundBorder(cx - 7, cy - 4, 14, 10, 1.5f, 2.0f, c);
        break;
    }
}

/* ---- Text --------------------------------------------------------------- */

int  textWidth(const char* s, int size) { return (int)Text::width(s_font, s, size); }
void text(float x, float y, const char* s, int size, u32 c) { Text::print((int)x, (int)y, s_font, s, size, c); }
/* one character, its ink centred on cx (a button's letter) */
static void glyphCentered(float cx, float y, const char* s, int size, u32 c)
{
    Text::print((int)lroundf(cx - Text::inkCentre(s_font, s, size)), (int)y, s_font, s, size, c);
}

void textCentered(float cx, float y, const char* s, int size, u32 c)
{
    Text::print((int)(cx - textWidth(s, size) * 0.5f), (int)y, s_font, s, size, c);
}
void textRight(float xr, float y, const char* s, int size, u32 c)
{
    Text::print((int)(xr - textWidth(s, size)), (int)y, s_font, s, size, c);
}

/* ---- Safe area ------------------------------------------------------------- */

static int   s_safeL = 0, s_safeT = 0, s_safeR = 0, s_safeB = 0;
static float s_sx = 1.0f, s_sy = 1.0f;
static float s_off = 0.0f;           /* extra drawing width on each side (16:9) */

void initScreen(bool widescreen)
{
    s_off = widescreen ? (640.0f * 4.0f / 3.0f - 640.0f) * 0.5f : 0.0f;   /* 106.67 */
    /* GRRLIB's projection maps 0..640 to the frame; widen it so one unit is
     * square on the TV.  Everything (GRRLIB, Text, Ui, video) uses it. */
    Mtx44 m;
    guOrtho(m, 0.0f, 480.0f, -s_off, 640.0f + s_off, 0.0f, 1000.0f);
    GX_LoadProjectionMtx(m, GX_ORTHOGRAPHIC);
    /* glyphs rasterised at frame resolution (see Text::setFrameMapping) */
    Text::setFrameMapping(-s_off, 640.0f / (640.0f + 2.0f * s_off));
}

float screenLeft()  { return -s_off; }
float screenRight() { return 640.0f + s_off; }

void pointerToScreen(ir_t& ir)
{
    if (!ir.valid || s_off == 0.0f) return;
    ir.x = -s_off + ir.x * (640.0f + 2.0f * s_off) / 640.0f;
}

/* the frame's lines over the 480 drawn: 1, but 528 / 480 on a 50 Hz PAL
 * Wii (GRRLIB's mode there: the picture 528 lines tall, its 48 last ones
 * left blank otherwise, a band at the bottom of the TV) */
static float lineScale() { return rmode ? rmode->efbHeight / 480.0f : 1.0f; }

/* nothing drawn outside the screen area: the viewport only places the
 * 640x480 picture, what is drawn past its edges (the next row's posters
 * below 480) would reach into the black margins */
static void safeScissor()
{
    const float ky = lineScale();
    GX_SetScissor((u32)s_safeL, (u32)(s_safeT * ky + 0.5f),
                  (u32)(640.0f * s_sx + 0.5f), (u32)(480.0f * s_sy * ky + 0.5f));
}

void setSafeArea(int l, int t, int r, int b)
{
    auto clampi = [](int v, int hi) { return v < 0 ? 0 : (v > hi ? hi : v); };
    s_safeL = clampi(l, SAFE_MAX_X); s_safeR = clampi(r, SAFE_MAX_X);
    s_safeT = clampi(t, SAFE_MAX_Y); s_safeB = clampi(b, SAFE_MAX_Y);
    s_sx = (640.0f - s_safeL - s_safeR) / 640.0f;
    s_sy = (480.0f - s_safeT - s_safeB) / 480.0f;
    /* GRRLIB's orthographic projection covers 640x480; the viewport decides
     * where that lands in the frame.  Everything outside stays black (the
     * frame is cleared on every copy to the TV). */
    const float ky = lineScale();
    GX_SetViewport((f32)s_safeL, s_safeT * ky, 640.0f * s_sx, 480.0f * s_sy * ky, 0.0f, 1.0f);
    safeScissor();
}

void safeArea(int& l, int& t, int& r, int& b) { l = s_safeL; t = s_safeT; r = s_safeR; b = s_safeB; }

void clip(float x, float y, float w, float h)
{
    /* drawing units -> frame pixels: aspect widening, then the safe area */
    const float k = 640.0f / (640.0f + 2.0f * s_off), ky = lineScale();
    GRRLIB_ClipDrawing((int)(s_safeL + (x + s_off) * k * s_sx), (int)((s_safeT + y * s_sy) * ky),
                       (int)(w * k * s_sx + 0.5f), (int)(h * s_sy * ky + 0.5f));
}

void clipReset() { safeScissor(); }

/* ---- Components --------------------------------------------------------- */

void background(bool withBand)
{
    const Palette& p = pal();
    safeScissor();   /* each frame: the player's video may have changed it */
    beginShapes();
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    const float L = screenLeft(), R = screenRight();
    vtx(L, 0, p.bgTop); vtx(R, 0, p.bgTop); vtx(R, 480, p.bgBottom); vtx(L, 480, p.bgBottom);
    GX_End();
    /* Fine horizontal pinstripes, like the Wii Menu */
    if (p.stripe & 0xFF) {
        GX_Begin(GX_QUADS, GX_VTXFMT0, 4 * 120);
        for (int y = 0; y < 480; y += 4) {
            vtx(L, y, p.stripe); vtx(R, y, p.stripe); vtx(R, y + 2, p.stripe); vtx(L, y + 2, p.stripe);
        }
        GX_End();
    }
    if (withBand) drawHeaderBand();
}

float headerBand() { return s_theme == Theme::Flix ? 44.0f : 0.0f; }

void drawHeaderBand()
{
    const float h = headerBand();
    if (h <= 0) return;
    /* glossy red title band of the old video channels */
    const float L = screenLeft(), R = screenRight();
    beginShapes();
    GX_Begin(GX_QUADS, GX_VTXFMT0, 12);
    vtx(L, 0, 0xD3242AFF); vtx(R, 0, 0xD3242AFF); vtx(R, h * 0.5f, 0xB8141AFF); vtx(L, h * 0.5f, 0xB8141AFF);
    vtx(L, h * 0.5f, 0xA80F15FF); vtx(R, h * 0.5f, 0xA80F15FF); vtx(R, h, 0x86090DFF); vtx(L, h, 0x86090DFF);
    vtx(L, h, 0x4A0306FF); vtx(R, h, 0x4A0306FF); vtx(R, h + 2, 0x4A030600); vtx(L, h + 2, 0x4A030600);
    GX_End();
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    vtx(L, 0, 0xFFFFFF40); vtx(R, 0, 0xFFFFFF40); vtx(R, 1, 0xFFFFFF40); vtx(L, 1, 0xFFFFFF40);
    GX_End();
}

void card(float x, float y, float w, float h, float r, float focus)
{
    const Palette& p = pal();
    shadow(x + 1, y + 3, w - 2, h - 2, r, 6.0f, p.shadow);
    if (focus > 0.0f) shadow(x - 2, y - 2, w + 4, h + 4, r + 2, 9.0f, alpha(p.glow, focus));
    roundRect(x, y, w, h, r, p.cardTop, p.cardBottom);
    roundBorder(x, y, w, h, r, focus > 0.0f ? 2.0f + focus : 2.0f, mix(p.cardBorder, p.accent, focus));
}

void button(float x, float y, float w, float h, const char* label, int size, float focus)
{
    const Palette& p = pal();
    label = tr(label);   /* buttons, hints, tabs and keys say the interface's words */
    card(x, y, w, h, h * 0.5f, focus);
    /* glossy top highlight */
    roundRect(x + h * 0.3f, y + 3, w - h * 0.6f, h * 0.36f, h * 0.18f,
              alpha(0xFFFFFFFF, theme() == Theme::Light ? 0.55f : 0.06f),
              alpha(0xFFFFFFFF, 0.0f));
    textCentered(x + w * 0.5f, y + (h - size) * 0.5f - 1, label, size,
                 mix(p.text, p.accentDark, focus));
}

void header(const char* title, const char* subtitle)
{
    const Palette& p = pal();
    text(28, 10 + (headerBand() > 0 ? 0 : 8), title, 24, p.text);
    if (subtitle) textRight(612, headerBand() > 0 ? 16 : 24, subtitle, 15,
                            headerBand() > 0 ? 0xFFFFFFC0 : p.textDim);
    if (headerBand() <= 0) roundRect(28, 52, 584, 2, 1, alpha(p.cardBorder, 0.8f));
}

/* D-pad hint: the same round badge as the buttons, with two arrowheads. */
static void dpadGlyph(float cx, float cy, bool vertical, u32 c)
{
    const Palette& p = pal();
    circle(cx, cy, 9, p.cardBorder);
    circle(cx, cy, 7.5f, p.cardTop);
    const float a = 5.5f, b = 1.2f, hw = 3.4f;     /* tip, base, half width */
    if (vertical) {
        triangle(cx, cy - a, cx + hw, cy - b, cx - hw, cy - b, c);
        triangle(cx, cy + a, cx - hw, cy + b, cx + hw, cy + b, c);
    } else {
        triangle(cx - a, cy, cx - b, cy - hw, cx - b, cy + hw, c);
        triangle(cx + a, cy, cx + b, cy + hw, cx + b, cy - hw, c);
    }
}

/* Width of the glyph drawn for a hint button (0 = label only). */
static ButtonStyle s_buttonStyle = ButtonStyle::WiiRemote;
void        setButtonStyle(ButtonStyle s) { s_buttonStyle = s; }
ButtonStyle buttonStyle()                 { return s_buttonStyle; }

const char* buttonName(const char* b)
{
    /* "+!": + as an action of its own (Input::isActionPressed), Z on a
     * GameCube controller (its R, the page turner, is a trigger) */
    if (strcmp(b, "+!") == 0)
        return s_buttonStyle == ButtonStyle::GameCube ? "Z" : s_buttonStyle == ButtonStyle::Keyboard ? "PgDn" : "+";
    if (s_buttonStyle == ButtonStyle::WiiRemote) return b;
    /* the Classic Controller's face buttons are written in lower case; a
     * USB keyboard's keys (Input.h) are shown as key caps */
    static const struct { const char* wii; const char* classic; const char* gc; const char* key; } MAP[] = {
        { "A",    "a",    "A",     "Enter" },
        { "B",    "b",    "B",     "Esc" },
        { "1",    "y",    "Y",     "F1" },
        { "2",    "x",    "X",     "F2" },
        { "-",    "-",    "L",     "PgUp" },
        { "+",    "+",    "R",     "PgDn" },
        { "-/+",  "-/+",  "L/R",   "PgUp/Dn" },
        { "HOME", "HOME", "START", "Home" },
        { "ZR",   "ZR",   "Z",     "F3" },
    };
    for (const auto& m : MAP)
        if (strcmp(b, m.wii) == 0)
            return s_buttonStyle == ButtonStyle::Classic ? m.classic
                 : s_buttonStyle == ButtonStyle::GameCube ? m.gc : m.key;
    return b;
}

/* A button as it looks on the controller used last: the GameCube's big
 * green A, small red B, X and Y beans, the Z bar and the L / R triggers;
 * the Wii Remote's B trigger; HOME with its house. */
enum class Shape { Disc, Square, Pill, Bar, Trigger, BeanX, BeanY };
struct Glyph {
    Shape shape;
    float w, h;
    u32   fill, edge, ink;
    bool  house;      /* HOME: the house instead of a letter */
};

static Glyph glyphOf(const char* b)
{
    const Palette& p = pal();
    Glyph g{ Shape::Disc, 18, 18, p.cardTop, p.cardBorder, p.text, false };
    const bool one = b[0] && !b[1];
    if (strcmp(b, "HOME") == 0) { g.house = true; return g; }
    if (s_buttonStyle == ButtonStyle::GameCube) {
        const u32 grey = 0xC9CDD2FF, greyEdge = 0x8A9097FF, dark = 0x2E3237FF;
        if (one && b[0] == 'A') { g.w = g.h = 20; g.fill = 0x2FB56AFF; g.edge = 0x1E7F49FF; g.ink = 0xFFFFFFFF; }
        else if (one && b[0] == 'B') { g.w = g.h = 16; g.fill = 0xD8463CFF; g.edge = 0x9A2E27FF; g.ink = 0xFFFFFFFF; }
        else if (one && b[0] == 'X') { g = { Shape::BeanX, 17, 25, grey, greyEdge, dark, false }; }
        else if (one && b[0] == 'Y') { g = { Shape::BeanY, 25, 17, grey, greyEdge, dark, false }; }
        else if (one && b[0] == 'Z') { g = { Shape::Bar, 22, 12, 0x6A5ACDFF, 0x463A9AFF, 0xFFFFFFFF, false }; }
        else if (one && (b[0] == 'L' || b[0] == 'R')) { g = { Shape::Trigger, 20, 15, grey, greyEdge, dark, false }; }
        return g;
    }
    if (strcmp(b, "ZL") == 0 || strcmp(b, "ZR") == 0) { g.shape = Shape::Trigger; g.w = 26; g.h = 15; return g; }
    /* the Wii Remote's B is a trigger: a square, as in the Wii's own menus */
    if (s_buttonStyle == ButtonStyle::WiiRemote && one && b[0] == 'B') g.shape = Shape::Square;
    return g;
}

/* The GameCube's X and Y: kidney beans curved around A.  Y lies above A,
 * its left end lower, bowed toward A below; X stands right of A, its top
 * leaning left, bowed toward A on its left.  Drawn as discs along that arc
 * (its edge first, then its fill), centred on cx, cy. */
static void beanGlyph(float cx, float cy, bool isY, u32 edge, u32 fill)
{
    const int   N = 16;
    const float R = 14.0f, r = 4.5f;                       /* the arc, the bean's half thickness */
    const float a0 = (isY ? -140.0f : -50.0f) * (float)M_PI / 180.0f;
    const float a1 = (isY ? -80.0f  :  10.0f) * (float)M_PI / 180.0f;
    float px[N], py[N], x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (int i = 0; i < N; ++i) {
        const float t = a0 + (a1 - a0) * i / (N - 1);
        px[i] = R * cosf(t);   /* around 0, 0: toward A */
        py[i] = R * sinf(t);
        x0 = fminf(x0, px[i]); x1 = fmaxf(x1, px[i]);
        y0 = fminf(y0, py[i]); y1 = fmaxf(y1, py[i]);
    }
    const float dx = cx - (x0 + x1) * 0.5f, dy = cy - (y0 + y1) * 0.5f;
    for (int i = 0; i < N; ++i) circle(px[i] + dx, py[i] + dy, r + 1.5f, edge);
    for (int i = 0; i < N; ++i) circle(px[i] + dx, py[i] + dy, r, fill);
}

/* the shape, its edge around it, centred on cx, cy */
static void drawShape(const Glyph& g, float cx, float cy)
{
    const float x = cx - g.w * 0.5f, y = cy - g.h * 0.5f;
    switch (g.shape) {
    case Shape::Disc:
        circle(cx, cy, g.w * 0.5f, g.edge);
        circle(cx, cy, g.w * 0.5f - 1.5f, g.fill);
        break;
    case Shape::Square:
        roundRect(x, y, g.w, g.h, 4.5f, g.edge);
        roundRect(x + 1.5f, y + 1.5f, g.w - 3, g.h - 3, 3.5f, g.fill);
        break;
    case Shape::Pill: {
        const float r = (g.w < g.h ? g.w : g.h) * 0.5f;
        roundRect(x, y, g.w, g.h, r, g.edge);
        roundRect(x + 1.5f, y + 1.5f, g.w - 3, g.h - 3, r - 1.5f, g.fill);
        break;
    }
    case Shape::Bar:
        roundRect(x, y, g.w, g.h, 3.5f, g.edge);
        roundRect(x + 1.5f, y + 1.5f, g.w - 3, g.h - 3, 2.5f, g.fill);
        break;
    case Shape::BeanX:
    case Shape::BeanY:
        beanGlyph(cx, cy, g.shape == Shape::BeanY, g.edge, g.fill);
        break;
    case Shape::Trigger:
        /* round on top, flat underneath */
        roundRect(x, y, g.w, g.h, 6.5f, g.edge);
        roundRect(x, y + g.h * 0.5f, g.w, g.h * 0.5f, 1.5f, g.edge);
        roundRect(x + 1.5f, y + 1.5f, g.w - 3, g.h - 3, 5.0f, g.fill);
        roundRect(x + 1.5f, y + g.h * 0.5f, g.w - 3, g.h * 0.5f - 1.5f, 1.0f, g.fill);
        break;
    }
}

/* HOME's house: a roof over a square */
static void houseGlyph(float cx, float cy, u32 c)
{
    triangle(cx, cy - 5.5f, cx + 5.5f, cy - 0.5f, cx - 5.5f, cy - 0.5f, c);
    roundRect(cx - 3.5f, cy - 1.0f, 7, 5.5f, 0.5f, c);
}

static float glyphWidth(const char* b)
{
    if (!b[0]) return 0;
    if (strcmp(b, "UD") == 0 || strcmp(b, "LR") == 0) return 18;
    if (strcmp(b, "-/+") == 0) return 40;
    if (strcmp(b, "L/R") == 0) return 44;
    const Glyph g = glyphOf(b);
    if (g.shape == Shape::Disc && strlen(b) > 1 && !g.house) return textWidth(b, 10) + 16;   /* START ... */
    return g.w;
}

/* Minus / plus drawn as bars: a 12 px "-" character is a single thin row of
 * pixels that vanishes on the TV (and when the picture is scaled). */
static void minusGlyph(float cx, float cy, u32 c) { roundRect(cx - 4.5f, cy - 1.25f, 9, 2.5f, 1.25f, c); }
static void plusGlyph(float cx, float cy, u32 c)
{
    roundRect(cx - 4.5f, cy - 1.25f, 9, 2.5f, 1.25f, c);
    roundRect(cx - 1.25f, cy - 4.5f, 2.5f, 9, 1.25f, c);
}

/* one button: its shape and what is written on it */
static void buttonGlyph(float cx, float cy, const char* b)
{
    const Glyph g = glyphOf(b);
    drawShape(g, cx, cy);
    if (g.house)           houseGlyph(cx, cy, g.ink);
    else if (b[0] == '-')  minusGlyph(cx, cy, g.ink);
    else if (b[0] == '+')  plusGlyph(cx, cy, g.ink);
    else if (g.shape == Shape::Bar || g.shape == Shape::Trigger)
        glyphCentered(cx, cy - 6, b, b[1] ? 10 : 11, g.ink);
    else if (g.shape == Shape::BeanX || g.shape == Shape::BeanY)
        glyphCentered(cx, cy - 6, b, 11, g.ink);
    else
        glyphCentered(cx, cy - 7, b, 12, g.ink);
}

float hint(float x, float y, const Hint& h)
{
    const Palette& p = pal();
    const char* b = buttonName(h.button);
    float cy = y + 9, w = glyphWidth(b);
    if (w == 0) {
        /* plain caption */
    } else if (strcmp(b, "UD") == 0 || strcmp(b, "LR") == 0) {
        dpadGlyph(x + 9, cy, b[0] == 'U', p.text);
    } else if (strcmp(b, "-/+") == 0) {
        /* two small buttons side by side */
        buttonGlyph(x + 9, cy, "-");
        buttonGlyph(x + 31, cy, "+");
    } else if (strcmp(b, "L/R") == 0) {
        buttonGlyph(x + 10, cy, "L");
        buttonGlyph(x + 34, cy, "R");
    } else if (strlen(b) > 1 && glyphOf(b).shape == Shape::Disc && !glyphOf(b).house) {
        roundRect(x, cy - 8, w, 16, 8, p.cardTop, p.cardBottom);
        roundBorder(x, cy - 8, w, 16, 8, 1.5f, p.cardBorder);
        textCentered(x + w * 0.5f, cy - 6, b, 10, p.textDim);
    } else {
        buttonGlyph(x + w * 0.5f, cy, b);
    }
    if (w > 0) w += 5;
    text(x + w, y + 2, tr(h.label), 14, p.textDim);
    return w + textWidth(tr(h.label), 14) + 14;
}

float hintWidth(const Hint& h)
{
    float g = glyphWidth(buttonName(h.button));
    return (g > 0 ? g + 5 : 0) + textWidth(tr(h.label), 14) + 14;
}

void bottomBar(const Hint* left, int nLeft, const Hint* right, int nRight)
{
    const Palette& p = pal();
    const float top = 424;
    /* Wii-style bar: wide rounded slab with a light top edge */
    const float L = screenLeft() - 20, W = screenWidth() + 40;
    shadow(L, top - 2, W, 90, 34, 8.0f, p.shadow);
    roundRect(L, top, W, 90, 34, p.barTop, p.barBottom);
    roundBorder(L, top, W, 90, 34, 2.0f, p.barBorder);

    /* clock */
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    char clock[8], date[24];
    /* TRANSLATORS: days of the week, abbreviated (the date under the clock) */
    static const char* const days[7] = { N_("Sun"), N_("Mon"), N_("Tue"), N_("Wed"),
                                         N_("Thu"), N_("Fri"), N_("Sat") };
    const char* day = tr(days[lt.tm_wday % 7]);
    if (s_clock12) {
        /* 1:31 and a small PM after it, the pair centred */
        snprintf(clock, sizeof(clock), "%d:%02d", lt.tm_hour % 12 ? lt.tm_hour % 12 : 12, lt.tm_min);
        /* TRANSLATORS: after the time on the 12-hour clock, keep it short */
        const char* ampm = lt.tm_hour < 12 ? tr("AM") : tr("PM");
        const float cw = textWidth(clock, 26), aw = textWidth(ampm, 13);
        const float cx = 320 - (cw + 4 + aw) * 0.5f;
        text(cx, top + 7, clock, 26, p.text);
        text(cx + cw + 4, top + 18, ampm, 13, p.textDim);
        snprintf(date, sizeof(date), "%s %02d/%02d", day, lt.tm_mon + 1, lt.tm_mday);
    } else {
        strftime(clock, sizeof(clock), "%H:%M", &lt);
        textCentered(320, top + 7, clock, 26, p.text);
        snprintf(date, sizeof(date), "%s %02d/%02d", day, lt.tm_mday, lt.tm_mon + 1);
    }
    textCentered(320, top + 36, date, 13, p.textDim);

    /* hints out to the screen edges (wide screens have room there), never
     * under the clock: left hints that would reach it go to the right side */
    const float x0 = screenLeft() + 40 < 30 ? screenLeft() + 40 : 30;
    const float x1 = screenRight() - 40 > 612 ? screenRight() - 40 : 612;
    const float clockL = 320 - 62, clockR = 320 + 62;
    int fit = 0;
    for (float w = x0; fit < nLeft && w + hintWidth(left[fit]) - 14 <= clockL; ++fit)
        w += hintWidth(left[fit]);
    float x = x0;
    for (int i = 0; i < fit; ++i) x += hint(x, top + 15, left[i]);
    float w = 0;
    for (int i = fit; i < nLeft; ++i) w += hintWidth(left[i]);
    for (int i = 0; right && i < nRight; ++i) w += hintWidth(right[i]);
    x = x1 - w;
    if (x < clockR) x = clockR;
    for (int i = fit; i < nLeft; ++i) x += hint(x, top + 15, left[i]);
    for (int i = 0; right && i < nRight; ++i) x += hint(x, top + 15, right[i]);
}

/* written by the music's thread, read by the footers: two lines, the one
 * shown flipped once the other is written */
static char         s_nowPlaying[2][160];
static volatile int s_nowPlayingAt = 0;
void setNowPlaying(const std::string& line)
{
    const int w = 1 - s_nowPlayingAt;
    snprintf(s_nowPlaying[w], sizeof(s_nowPlaying[w]), "%s", line.c_str());
    s_nowPlayingAt = w;
}

/* a quaver: what plays, in the footers */
static void noteGlyph(float x, float y, u32 c)
{
    circle(x + 3.5f, y + 12, 3.5f, c);
    roundRect(x + 5.5f, y + 1, 1.8f, 11, 0.9f, c);
    triangle(x + 7.3f, y + 1, x + 11, y + 5, x + 7.3f, y + 5, c);
}

void footer(const Hint* left, int nLeft, const Hint* right, int nRight, const char* center)
{
    const Palette& p = pal();
    const float top = 450;
    const float L = screenLeft() - 20, W = screenWidth() + 40;
    shadow(L, top - 1, W, 60, 16, 6.0f, p.shadow);
    roundRect(L, top, W, 60, 16, p.barTop, p.barBottom);
    roundBorder(L, top, W, 60, 16, 1.5f, p.barBorder);

    float x = 20, xl = 20;
    for (int i = 0; i < nLeft; ++i) x += hint(x, top + 5, left[i]);
    xl = x;
    float xr = 620;
    if (right && nRight > 0) {
        float w = 0;
        for (int i = 0; i < nRight; ++i) w += hintWidth(right[i]);
        x = xr = 626 - w;
        for (int i = 0; i < nRight; ++i) x += hint(x, top + 5, right[i]);
    }
    const char* playing = s_nowPlaying[s_nowPlayingAt];
    const bool music = (!center || !*center) && playing[0];
    if (music) center = playing;
    if (center && *center) {
        /* centred caption, shortened to the room left between the hints */
        float room = 2.0f * (320.0f - xl > xr - 320.0f ? xr - 320.0f : 320.0f - xl) - 16.0f;
        char buf[160], out[164];
        snprintf(buf, sizeof(buf), "%s", center);
        snprintf(out, sizeof(out), "%s", buf);
        size_t len = strlen(buf);
        while (len > 0 && textWidth(out, 14) > room) {
            do { --len; } while (len > 0 && ((unsigned char)buf[len] & 0xC0) == 0x80);
            buf[len] = 0;
            snprintf(out, sizeof(out), "%s...", buf);
        }
        if (len > 0 && music) {
            const float w = textWidth(out, 14) + 16;
            noteGlyph(320 - w / 2, top + 8, p.accent);
            text(320 - w / 2 + 16, top + 7, out, 14, p.accent);
        } else if (len > 0) {
            textCentered(320, top + 7, out, 14, p.text);
        }
    }
}

static struct { float x[8], w[8], y, h; int n; u64 at; } s_tabBar;

int tabAt(float x, float y)
{
    if (ticks_to_millisecs(gettime()) - s_tabBar.at > 100) return -1;
    if (y < s_tabBar.y || y >= s_tabBar.y + s_tabBar.h) return -1;
    for (int i = 0; i < s_tabBar.n; ++i)
        if (x >= s_tabBar.x[i] && x < s_tabBar.x[i] + s_tabBar.w[i]) return i;
    return -1;
}

float tabs(float cx, float y, const char* const* names, int n, int sel, int size)
{
    const Palette& p = pal();
    const float pad = 13, h = size + 14;
    float w[8], total = 0;
    if (n > 8) n = 8;
    for (int i = 0; i < n; ++i) { w[i] = textWidth(tr(names[i]), size) + pad * 2; total += w[i]; }
    float x0 = cx - total * 0.5f;
    shadow(x0, y + 1, total, h, h * 0.5f, 4.0f, p.shadow);
    roundRect(x0, y, total, h, h * 0.5f, p.field, mix(p.field, p.cardBottom, 0.5f));
    roundBorder(x0, y, total, h, h * 0.5f, 1.5f, p.fieldBorder);
    float x = x0;
    for (int i = 0; i < n; ++i) {
        if (i == sel) {
            roundRect(x + 2, y + 2, w[i] - 4, h - 4, (h - 4) * 0.5f,
                      mix(p.accent, 0xFFFFFFFF, 0.25f), p.accentDark);
            textCentered(x + w[i] * 0.5f, y + (h - size) * 0.5f - 1, tr(names[i]), size, p.textOnAccent);
        } else {
            textCentered(x + w[i] * 0.5f, y + (h - size) * 0.5f - 1, tr(names[i]), size, p.textDim);
        }
        s_tabBar.x[i] = x; s_tabBar.w[i] = w[i];
        x += w[i];
    }
    s_tabBar.y = y; s_tabBar.h = h; s_tabBar.n = n; s_tabBar.at = ticks_to_millisecs(gettime());
    return x0;
}

/* Round button with a triangle pointing dir (0 up, 1 down, 2 left, 3 right). */
static void roundArrow(float cx, float cy, float r0, int dir, bool enabled, bool hover)
{
    const Palette& p = pal();
    float k   = enabled ? 1.0f : 0.35f;
    bool  hot = hover && enabled;
    float rad = hot ? r0 + 2 : r0;
    shadow(cx - rad, cy - rad + 2, rad * 2, rad * 2, rad, 5.0f, alpha(p.shadow, k));
    if (hot) shadow(cx - rad - 2, cy - rad - 2, rad * 2 + 4, rad * 2 + 4, rad + 2, 8.0f, p.glow);
    roundRect(cx - rad, cy - rad, rad * 2, rad * 2, rad, alpha(p.cardTop, k), alpha(p.cardBottom, k));
    roundBorder(cx - rad, cy - rad, rad * 2, rad * 2, rad, hot ? 2.5f : 1.5f,
                alpha(hot ? p.accent : p.cardBorder, k));
    u32 c = enabled ? p.accent : alpha(p.textDim, 0.5f);
    float a = r0 * 0.47f, w = r0 * 0.37f;     /* triangle half-length / half-width */
    switch (dir) {
    case 0: triangle(cx, cy - a, cx + a, cy + w, cx - a, cy + w, c); break;
    case 1: triangle(cx - a, cy - w, cx + a, cy - w, cx, cy + a, c); break;
    case 2: triangle(cx - a, cy, cx + w, cy - a, cx + w, cy + a, c); break;
    default:triangle(cx + a, cy, cx - w, cy + a, cx - w, cy - a, c); break;
    }
}

void arrowButton(float cx, float cy, bool up, bool enabled, bool hover)
{
    roundArrow(cx, cy, 15, up ? 0 : 1, enabled, hover);
}

void pageArrow(float cx, float cy, bool left, bool enabled, bool hover)
{
    roundArrow(cx, cy, 21, left ? 2 : 3, enabled, hover);
}

void progress(float x, float y, float w, float h, float frac)
{
    const Palette& p = pal();
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    roundRect(x, y, w, h, h * 0.5f, 0x00000080);
    if (frac > 0) roundRect(x, y, w * frac < h ? h : w * frac, h, h * 0.5f,
                            mix(p.accent, 0xFFFFFFFF, 0.2f), p.accent);
}

void scrollbar(float x, float y, float h, int first, int visible, int total)
{
    const Palette& p = pal();
    if (total <= visible) return;
    float barH = h * visible / total;
    if (barH < 20) barH = 20;
    int maxTop = total - visible;
    float barY = y + (maxTop > 0 ? (h - barH) * first / maxTop : 0);
    roundRect(x, y, 6, h, 3, alpha(p.cardBorder, 0.5f));
    roundRect(x, barY, 6, barH, 3, p.accent, p.accentDark);
}

void key(float x, float y, float w, float h, const char* label,
         bool sel, bool hover, bool special, bool latched)
{
    const Palette& p = pal();
    label = tr(label);
    if (sel) {
        shadow(x - 2, y - 2, w + 4, h + 4, 9, 7.0f, p.glow);
        roundRect(x, y, w, h, 7, mix(p.accent, 0xFFFFFFFF, 0.25f), p.accentDark);
    } else if (latched) {
        roundRect(x, y, w, h, 7, 0xF7B54AFF, 0xE08E1BFF);
    } else {
        u32 top = special ? mix(p.cardBottom, p.cardBorder, 0.35f) : p.cardTop;
        roundRect(x, y, w, h, 7, top, p.cardBottom);
        roundBorder(x, y, w, h, 7, hover ? 2.0f : 1.0f, hover ? p.accent : alpha(p.cardBorder, 0.8f));
    }
    /* short ASCII words (SYM, ABC) get a smaller size than single glyphs */
    int sz = 15;
    if (strlen(label) > 1 && (unsigned char)label[0] < 0x80) sz = w > 60 ? 13 : 11;
    u32 tc = (sel || latched) ? p.textOnAccent : (special ? p.textDim : p.text);
    textCentered(x + w * 0.5f, y + (h - sz) * 0.5f - 1, label, sz, tc);
}

void field(float x, float y, float w, float h, const char* value, int size, bool focused)
{
    const Palette& p = pal();
    if (focused) shadow(x - 2, y - 2, w + 4, h + 4, h * 0.5f + 2, 8.0f, p.glow);
    else         shadow(x, y + 1, w, h, h * 0.5f, 4.0f, p.shadow);
    roundRect(x, y, w, h, h * 0.5f, p.field);
    roundBorder(x, y, w, h, h * 0.5f, focused ? 2.5f : 1.5f, focused ? p.accent : p.fieldBorder);
    if (value) text(x + h * 0.5f, y + (h - size) * 0.5f - 1, value, size, p.text);
}

static Mtx s_savedView;
static int s_offsetDepth = 0;

void pushOffset(float dx, float dy)
{
    if (s_offsetDepth++ == 0) guMtxCopy(GXmodelView2D, s_savedView);
    guMtxTransApply(GXmodelView2D, GXmodelView2D, dx, dy, 0.0f);
    GX_LoadPosMtxImm(GXmodelView2D, GX_PNMTX0);
}

void popOffset()
{
    if (s_offsetDepth > 0 && --s_offsetDepth == 0) {
        guMtxCopy(s_savedView, GXmodelView2D);
        GX_LoadPosMtxImm(GXmodelView2D, GX_PNMTX0);
    }
}

float pulse()
{
    float t = (float)(ticks_to_millisecs(gettime()) % 1600) / 1600.0f;
    return 0.8f + 0.2f * sinf(t * 2.0f * (float)M_PI);
}

void avatar(float cx, float cy, float r, const char* name, float focus)
{
    const Palette& p = pal();
    /* first character (whole UTF-8 sequence), ASCII upper-cased */
    char initial[5] = { '?', 0, 0, 0, 0 };
    if (name && name[0]) {
        unsigned char c = (unsigned char)name[0];
        int len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        for (int i = 0; i < len && name[i]; ++i) initial[i] = name[i];
        initial[len] = 0;
        if (initial[0] >= 'a' && initial[0] <= 'z') initial[0] -= 32;
    }
    if (focus > 0.0f) shadow(cx - r - 2, cy - r - 2, r * 2 + 4, r * 2 + 4, r + 2, 8.0f, alpha(p.glow, focus));
    else              shadow(cx - r, cy - r + 2, r * 2, r * 2, r, 4.0f, p.shadow);
    roundRect(cx - r, cy - r, r * 2, r * 2, r,
              mix(p.accent, 0xFFFFFFFF, 0.25f + 0.15f * focus), mix(p.accent, 0x000000FF, 0.15f));
    roundBorder(cx - r, cy - r, r * 2, r * 2, r, 2.0f, 0xFFFFFFFF);
    int size = (int)(r * 1.1f);
    textCentered(cx, cy - size * 0.55f, initial, size, 0xFFFFFFFF);
}

/* The USB mouse's pointer: a desktop arrow (tools/mouse_cursor.py), its
 * tip on x, y */
extern "C" unsigned char data_mouse_arrow_png[];   /* outside Ui:: */
bool mouseCursor(float x, float y)
{
    if (!Input::pointerIsMouse()) return false;
    static GRRLIB_texImg* tex = nullptr;
    if (!tex) tex = GRRLIB_LoadTexture(data_mouse_arrow_png);
    if (tex) GRRLIB_DrawImg((int)x - 2, (int)y - 2, tex, 0, 1, 1, 0xFFFFFFFF);
    return true;
}

/* the frame buffer shown last: Y U Y V, two pixels a word, read every
 * other line and pair at half size (each needs a frame-sized texture
 * otherwise); in RGBA, through the 601 video levels */
static GRRLIB_texImg* s_frozen = nullptr;

bool freezeFrame()
{
    if (!rmode || !xfb[fb ^ 1]) return false;
    const int W = rmode->fbWidth, H = rmode->xfbHeight, tw = W / 2, th = (H / 2) & ~3;
    if (!s_frozen) s_frozen = GRRLIB_CreateEmptyTexture(tw, th);
    if (!s_frozen) return false;
    const u32* src = (const u32*)xfb[fb ^ 1];
    auto clamp8 = [](int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); };
    for (int y = 0; y < th; ++y) {
        const u32* line = src + (y * 2) * (W / 2);
        for (int x = 0; x < tw; ++x) {
            const u32 w = line[x];
            const int c = (int)(((w >> 24) & 0xFF) + ((w >> 8) & 0xFF)) / 2 - 16;
            const int d = (int)((w >> 16) & 0xFF) - 128, e = (int)(w & 0xFF) - 128;
            const int r = clamp8((298 * c + 409 * e + 128) >> 8);
            const int g = clamp8((298 * c - 100 * d - 208 * e + 128) >> 8);
            const int b = clamp8((298 * c + 516 * d + 128) >> 8);
            GRRLIB_SetPixelTotexImg(x, y, s_frozen, (u32)(r << 24 | g << 16 | b << 8 | 0xFF));
        }
    }
    GRRLIB_FlushTex(s_frozen);
    return true;
}

bool drawFrozen()
{
    if (!s_frozen) return false;
    /* the frame buffer holds the whole frame, margins of the screen area
     * included: drawn over the whole frame, then the area as it was */
    const float ky = lineScale();
    GX_SetViewport(0.0f, 0.0f, 640.0f, 480.0f * ky, 0.0f, 1.0f);
    GX_SetScissor(0, 0, 640, (u32)(480.0f * ky + 0.5f));
    texRound(s_frozen, screenLeft(), 0, screenWidth(), 480, 0.5f, 0x7A7A7AFF);
    setSafeArea(s_safeL, s_safeT, s_safeR, s_safeB);
    return true;
}

void releaseFrozen()
{
    if (s_frozen) { GRRLIB_FreeTexture(s_frozen); s_frozen = nullptr; }
}

void spinner(GRRLIB_texImg* ring, float cx, float cy)
{
    if (!ring) return;
    float angle = (float)(ticks_to_millisecs(gettime()) % 1500) * (360.0f / 1500.0f);
    GRRLIB_SetMidHandle(ring, true);
    const u32 tint = theme() == Theme::Light ? 0x34BEEDFF : theme() == Theme::Flix ? 0xE52A30FF
                   : theme() == Theme::Dark ? 0xFFFFFFFF : pal().accent;
    GRRLIB_DrawImg(cx, cy, ring, angle, 1.0f, 1.0f, tint);
    GRRLIB_SetMidHandle(ring, false);
}

/* ---- Notice: a message from the server ---------------------------------- */

static std::string s_noticeHeader, s_noticeText;
static u64         s_noticeFrom = 0, s_noticeUntil = 0;

void showNotice(const std::string& header, const std::string& text, int ms)
{
    s_noticeHeader = header;
    s_noticeText   = text;
    s_noticeFrom   = ticks_to_millisecs(gettime());
    s_noticeUntil  = s_noticeFrom + (u64)(ms > 0 ? ms : 5000);
}

void drawNotice()
{
    if (!s_noticeUntil) return;
    u64 now = ticks_to_millisecs(gettime());
    if (now >= s_noticeUntil) { s_noticeUntil = 0; return; }
    /* slides in from the top, fades out over its last 300 ms */
    float in  = (now - s_noticeFrom) / 250.0f;
    float out = (s_noticeUntil - now) / 300.0f;
    float k   = in < 1.0f ? in : 1.0f;
    if (out < k) k = out < 0.0f ? 0.0f : out;

    const Palette& p = pal();
    const int HS = 17, TS = 15, LH = 20, MAXW = 460, PAD = 18;
    /* the text in lines that fit (words; '\n' breaks), four at most */
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos <= s_noticeText.size() && lines.size() < 4) {
        size_t e = s_noticeText.find('\n', pos);
        std::string para = s_noticeText.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        pos = e == std::string::npos ? s_noticeText.size() + 1 : e + 1;
        std::string cur;
        size_t w = 0;
        while (w <= para.size() && lines.size() < 4) {
            size_t sp = para.find(' ', w);
            std::string word = para.substr(w, sp == std::string::npos ? std::string::npos : sp - w);
            w = sp == std::string::npos ? para.size() + 1 : sp + 1;
            std::string trial = cur.empty() ? word : cur + " " + word;
            if (!cur.empty() && textWidth(trial.c_str(), TS) > MAXW - 2 * PAD) {
                lines.push_back(cur);
                cur = word;
            } else {
                cur = trial;
            }
        }
        if (!cur.empty() && lines.size() < 4) lines.push_back(cur);
    }
    /* TRANSLATORS: heading of a message sent from the server's dashboard without one */
    const std::string& head = s_noticeHeader.empty() ? std::string(tr("Message")) : s_noticeHeader;
    int wNeed = textWidth(head.c_str(), HS);
    for (const auto& l : lines) { int lw = textWidth(l.c_str(), TS); if (lw > wNeed) wNeed = lw; }
    float w = wNeed + 2 * PAD + 6;
    if (w > MAXW) w = MAXW;
    if (w < 220) w = 220;
    float h = PAD + HS + 6 + lines.size() * LH + PAD - 4;
    float x = 320 - w / 2, y = 18 - (1.0f - k) * (h + 30);

    shadow(x, y + 3, w, h, 14, 8.0f, alpha(p.shadow, k));
    roundRect(x, y, w, h, 14, alpha(p.cardTop, k), alpha(p.cardBottom, k));
    roundBorder(x, y, w, h, 14, 1.5f, alpha(p.cardBorder, k));
    roundRect(x + 10, y + 12, 4, h - 24, 2, alpha(p.accent, k));
    text(x + PAD + 6, y + PAD - 4, head.c_str(), HS, alpha(p.accent, k));
    for (size_t i = 0; i < lines.size(); ++i)
        text(x + PAD + 6, y + PAD + HS + 4 + i * LH, lines[i].c_str(), TS, alpha(p.text, k));
}

} // namespace Ui
