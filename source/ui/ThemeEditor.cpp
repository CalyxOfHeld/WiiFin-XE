#include "ThemeEditor.h"
#include "../input/Input.h"
#include "../core/SoundFX.h"
#include "../core/Lang.h"
#include <stdio.h>
#include <math.h>
#include <ogc/lwp_watchdog.h>

/* Layout: the colours on the left, the picker on the right (to the
 * screen's edge in 16:9) */
#define LX (Ui::screenLeft() + 28.0f)
static const float LW = 210, LY = 62, RSTEP = 27, RH = 24;
#define PX (LX + LW + 16.0f)
#define PW (Ui::screenRight() - 28.0f - PX)
static const float PY = 66, PH = 348;
#define SQX (PX + 14.0f)
static const float SQY = PY + 50, SQ = 132;
static const float SLY = SQY + SQ + 14, SLSTEP = 26;   /* the slides, then the field */
#define BX (PX + 72.0f)
#define BW (PW - 72.0f - 56.0f)
static const float FY = SLY + 4 * SLSTEP, FH = 30;

static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

static u32 rgb(int r, int g, int b) { return (u32)r << 24 | (u32)g << 16 | (u32)b << 8 | 0xFF; }

static u32 hsvToRgb(float h, float s, float v)
{
    h = fmodf(h, 360.0f); if (h < 0) h += 360.0f;
    const float c = v * s, x = c * (1 - fabsf(fmodf(h / 60.0f, 2.0f) - 1)), m = v - c;
    float r = 0, g = 0, b = 0;
    if      (h < 60)  { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else              { r = c; b = x; }
    return rgb((int)lroundf((r + m) * 255), (int)lroundf((g + m) * 255), (int)lroundf((b + m) * 255));
}

/* the part of the picker at x, y; -1 none */
static int partAt(float x, float y)
{
    if (x >= SQX && x < SQX + SQ && y >= SQY && y < SQY + SQ) return 0;
    for (int k = 0; k < 4; ++k)
        if (x >= PX + 10 && x < PX + PW - 10 && y >= SLY + k * SLSTEP - 2 && y < SLY + k * SLSTEP + 22) return 1 + k;
    if (x >= BX && x < PX + PW - 14 && y >= FY && y < FY + FH) return 5;
    return -1;
}

/* row i of the list: Reset and Remove share the last line */
static void rowRect(int i, bool withRemove, float& x, float& y, float& w)
{
    const int line = i == Ui::THEME_COLOR_COUNT + 2 ? i - 1 : i;
    x = LX; w = LW;
    y = LY + line * RSTEP + (line >= Ui::THEME_COLOR_COUNT ? 8 : 0);
    if (withRemove && line == Ui::THEME_COLOR_COUNT + 1) {
        w = (LW - 6) * 0.5f;
        if (i != line) x = LX + w + 6;
    }
}

static int rowAt(float x, float y, bool withRemove)
{
    for (int i = 0; i < Ui::THEME_COLOR_COUNT + (withRemove ? 3 : 2); ++i) {
        float rx, ry, rw;
        rowRect(i, withRemove, rx, ry, rw);
        if (x >= rx && x < rx + rw && y >= ry && y < ry + RH) return i;
    }
    return -1;
}

void ThemeEditor::open()
{
    const int c = Ui::customTheme();
    if (c >= 0) work = Ui::customThemes()[c];
    else {
        work = Ui::CustomTheme();
        work.base = Ui::theme();
        char n[48];
        /* TRANSLATORS: Customize Theme: the name a built-in theme's copy starts with */
        snprintf(n, sizeof(n), tr("My %s"), tr(Ui::themeName(work.base)));
        work.name = n;
    }
    opened = work;
    pal = Ui::buildPalette(work);
    Ui::setPreviewPalette(&pal);
    sel = 0; part = 0; inPicker = grabbed = dirty = asking = removing = false;
    dragging = lastHover = -1;
    typing = Typing::None;
    for (float& f : rowAnim) f = 0;
    kb.setEnterLabel(N_("Done"));
    fromColor();
    before = color();
    SYS_Report("[Theme] customize %s (%s)\n", work.name.c_str(), work.file.empty() ? "new" : work.file.c_str());
}

void ThemeEditor::close()
{
    Ui::setPreviewPalette(nullptr);   /* the theme in use: as it was, or the one saved */
    asking = removing = false;
    typing = Typing::None;
}

u32 ThemeEditor::color() const { return sel < Ui::THEME_COLOR_COUNT ? Ui::themeColor(pal, sel) : 0; }

void ThemeEditor::setColor(u32 c, bool keepHsv)
{
    if (sel >= Ui::THEME_COLOR_COUNT) return;
    c |= 0xFF;   /* the main colours are opaque */
    if (c == color()) return;
    Ui::setThemeColor(work, Ui::THEME_COLORS[sel].key, c);
    pal = Ui::buildPalette(work);
    Ui::setPreviewPalette(&pal);
    dirty = true;
    if (!keepHsv) fromColor();
}

void ThemeEditor::fromColor()
{
    const u32 c = color();
    const float r = (c >> 24) / 255.0f, g = ((c >> 16) & 0xFF) / 255.0f, b = ((c >> 8) & 0xFF) / 255.0f;
    const float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), d = mx - mn;
    v = mx;
    s = mx > 0 ? d / mx : 0;
    if (d > 0) {   /* grey: the hue stays where it was */
        if      (mx == r) h = 60 * fmodf((g - b) / d, 6);
        else if (mx == g) h = 60 * ((b - r) / d + 2);
        else              h = 60 * ((r - g) / d + 4);
        if (h < 0) h += 360;
    }
}

void ThemeEditor::applyHsv() { setColor(hsvToRgb(h, s, v), true); }

/* Left/Right (or Down/Up) on a slide: one step a press; held, steps that
 * grow */
int ThemeEditor::heldStep(u32 minus, u32 plus)
{
    const u32 down = Input::rawDown(), held = Input::held();
    const u64 now = ticks_to_millisecs(gettime());
    if (down & (minus | plus)) { heldSince = now; return (down & plus) ? 1 : -1; }
    const int dir = (held & plus) ? 1 : (held & minus) ? -1 : 0;
    if (!dir || !heldSince) return 0;
    const u64 t = now - heldSince;
    if (t < 300) return 0;
    return dir * (t > 1500 ? 4 : t > 800 ? 2 : 1);
}

void ThemeEditor::startSave()
{
    typing = Typing::Name;
    draft = work.name;
    kb.reset();
    SoundFX::play(SoundFX::FX::Select);
}

bool ThemeEditor::updatePicker(ir_t& ir)
{
    if (Input::isBackPressed()) {
        if (grabbed) grabbed = false;
        else         inPicker = false;
        SoundFX::play(SoundFX::FX::Back);
        return false;
    }
    if (Input::isLPressed() && color() != before) {   /* -: the colour as it was */
        setColor(before, false);
        SoundFX::play(SoundFX::FX::Back);
    }
    const bool a = Input::isAJustPressed() && !ir.valid;
    if (grabbed) {
        const int ds = heldStep(WPAD_BUTTON_LEFT, WPAD_BUTTON_RIGHT);
        const int dv = heldStep(WPAD_BUTTON_DOWN, WPAD_BUTTON_UP);
        if (ds || dv) { s = clamp01(s + ds * 0.01f); v = clamp01(v + dv * 0.01f); applyHsv(); }
        if (a) { grabbed = false; SoundFX::play(SoundFX::FX::Select); }
        return false;
    }
    const int was = part;
    if (Input::isUpPressed())   part = (part + P_COUNT - 1) % P_COUNT;
    if (Input::isDownPressed()) part = (part + 1) % P_COUNT;
    if (part != was) { SoundFX::play(SoundFX::FX::Move); return false; }
    if (part == P_SQUARE || part == P_TEXT) {
        if (Input::isLeftPressed()) { inPicker = false; SoundFX::play(SoundFX::FX::Back); return false; }
        if (a && part == P_SQUARE) { grabbed = true; SoundFX::play(SoundFX::FX::Select); }
        if (a && part == P_TEXT) {
            typing = Typing::Color;
            draft.clear();
            kb.reset();
            SoundFX::play(SoundFX::FX::Select);
        }
        return false;
    }
    const int d = heldStep(WPAD_BUTTON_LEFT, WPAD_BUTTON_RIGHT);
    if (!d) return false;
    if (part == P_HUE) {
        h = fmodf(h + d + 360.0f, 360.0f);
        if (s < 0.05f) s = 1.0f;   /* a grey has no hue to show: colour it */
        if (v < 0.05f) v = 1.0f;
        applyHsv();
    } else {
        const u32 c = color();
        int ch[3] = { (int)(c >> 24), (int)((c >> 16) & 0xFF), (int)((c >> 8) & 0xFF) };
        int& x = ch[part - P_RED];
        x = x + d < 0 ? 0 : x + d > 255 ? 255 : x + d;
        setColor(rgb(ch[0], ch[1], ch[2]), false);
    }
    return false;
}

bool ThemeEditor::update(ir_t& ir)
{
    if (typing != Typing::None) {
        kb.setOrigin((640 - kb.width()) * 0.5f, 214 - kb.lift());
        const Keyboard::Result r = kb.update(ir, draft, typing == Typing::Name ? 32 : 24);
        if (r == Keyboard::Result::Cancel) {
            typing = Typing::None;
            SoundFX::play(SoundFX::FX::Back);
        } else if (r == Keyboard::Result::Enter && typing == Typing::Color) {
            typing = Typing::None;
            u32 c;
            if (Ui::parseColor(draft, c)) { setColor(c, false); SoundFX::play(SoundFX::FX::Open); }
            else Ui::showNotice(tr("Not a colour"), tr("Type it as #AA5CC3, or as 170, 92, 195"), 4000);
        } else if (r == Keyboard::Result::Enter) {
            size_t a = draft.find_first_not_of(' '), b = draft.find_last_not_of(' ');
            if (a == std::string::npos) return false;   /* a name first */
            typing = Typing::None;
            work.name = draft.substr(a, b - a + 1);
            const int i = Ui::saveTheme(work);
            if (i < 0) {
                Ui::showNotice(tr("The theme wasn't saved"), tr("The SD card may be locked or full."), 5000);
                return false;
            }
            Ui::setCustomTheme(i);
            work = opened = Ui::customThemes()[i];
            pal = Ui::buildPalette(work);
            Ui::setPreviewPalette(&pal);
            dirty = false;
            SoundFX::play(SoundFX::FX::Start);
            Ui::showNotice(tr("Theme saved"), "themes/" + work.file, 3000);
            if (asking) { close(); return true; }   /* saved on the way out */
        }
        return false;
    }

    if (asking) {
        const int was = askSel;
        if (Input::isLeftPressed())  askSel = 0;
        if (Input::isRightPressed()) askSel = 1;
        if (ir.valid && ir.y >= 250 && ir.y < 288) {
            if (ir.x >= 170 && ir.x < 310) askSel = 0;
            if (ir.x >= 330 && ir.x < 470) askSel = 1;
        }
        if (askSel != was) SoundFX::play(SoundFX::FX::Move);
        if (Input::isBackPressed()) { asking = false; SoundFX::play(SoundFX::FX::Back); return false; }
        if (Input::isAJustPressed()) {
            if (askSel == 0) { startSave(); return false; }   /* asking stays: closes once saved */
            SoundFX::play(SoundFX::FX::Back);
            close();
            return true;
        }
        return false;
    }

    if (removing) {
        const int was = removeSel;
        if (Input::isLeftPressed())  removeSel = 0;
        if (Input::isRightPressed()) removeSel = 1;
        if (ir.valid && ir.y >= 266 && ir.y < 304) {
            if (ir.x >= 170 && ir.x < 310) removeSel = 0;
            if (ir.x >= 330 && ir.x < 470) removeSel = 1;
        }
        if (removeSel != was) SoundFX::play(SoundFX::FX::Move);
        if (Input::isBackPressed() || (Input::isAJustPressed() && removeSel == 1)) {
            removing = false;
            SoundFX::play(SoundFX::FX::Back);
            return false;
        }
        if (Input::isAJustPressed()) {
            removing = false;
            if (!Ui::removeTheme(opened.file)) {
                Ui::showNotice(tr("The theme wasn't removed"), tr("The SD card may be locked."), 5000);
                return false;
            }
            SoundFX::play(SoundFX::FX::Back);
            Ui::showNotice(tr("Theme removed"), tr("It's in themes/removed/ on the SD card, if you want it back."), 4000);
            close();   /* the theme in use: its base, a built-in one */
            return true;
        }
        return false;
    }

    const bool a = Input::isAJustPressed();
    if (!(Input::held() & WPAD_BUTTON_A)) dragging = -1;

    /* the pointer: a row of the list, a part of the picker */
    const int hover = ir.valid ? rowAt(ir.x, ir.y, canRemove()) : -1;
    const int hpart = ir.valid && sel < Ui::THEME_COLOR_COUNT ? partAt(ir.x, ir.y) : -1;
    if (hover >= 0 && hover != lastHover && dragging < 0 && hover != sel) {
        sel = hover;
        inPicker = grabbed = false;
        if (sel < Ui::THEME_COLOR_COUNT) { fromColor(); before = color(); }
        SoundFX::play(SoundFX::FX::Move);
    }
    lastHover = hover;
    if (a && hpart >= 0) {
        inPicker = true;
        part = hpart;
        grabbed = false;
        if (part == P_TEXT) { typing = Typing::Color; draft.clear(); kb.reset(); SoundFX::play(SoundFX::FX::Select); return false; }
        dragging = part;
    }
    if (dragging >= 0 && ir.valid) {
        if (dragging == P_SQUARE) {
            s = clamp01((ir.x - SQX) / SQ);
            v = 1.0f - clamp01((ir.y - SQY) / SQ);
            applyHsv();
        } else {
            const float t = clamp01((ir.x - BX) / BW);
            if (dragging == P_HUE) { h = t * 359.0f; if (s < 0.05f) s = 1; if (v < 0.05f) v = 1; applyHsv(); }
            else {
                const u32 c = color();
                int ch[3] = { (int)(c >> 24), (int)((c >> 16) & 0xFF), (int)((c >> 8) & 0xFF) };
                ch[dragging - P_RED] = (int)lroundf(t * 255);
                setColor(rgb(ch[0], ch[1], ch[2]), false);
            }
        }
        return false;
    }
    if (inPicker) return updatePicker(ir);

    /* the list */
    /* up and down by line (Reset and Remove share one), left and right
     * between those two */
    const int was = sel;
    const int line = sel == ROW_REMOVE ? ROW_RESET : sel, lines = ROW_RESET + 1;
    if (Input::isUpPressed())   sel = (line + lines - 1) % lines;
    if (Input::isDownPressed()) sel = (line + 1) % lines;
    if (canRemove() && sel == ROW_RESET && Input::isRightPressed()) sel = ROW_REMOVE;
    if (sel == ROW_REMOVE && Input::isLeftPressed()) sel = ROW_RESET;
    if (sel != was) {
        if (sel < Ui::THEME_COLOR_COUNT) { fromColor(); before = color(); }
        SoundFX::play(SoundFX::FX::Move);
        return false;
    }
    if (Input::isBackPressed()) {
        if (dirty) { asking = true; askSel = 0; SoundFX::play(SoundFX::FX::Select); return false; }
        SoundFX::play(SoundFX::FX::Back);
        close();
        return true;
    }
    const bool act = a && (!ir.valid || hover >= 0);
    if (sel < Ui::THEME_COLOR_COUNT && (act || Input::isRightPressed())) {
        inPicker = true;
        part = P_SQUARE;
        before = color();
        SoundFX::play(SoundFX::FX::Select);
    } else if (act && sel == ROW_SAVE) {
        startSave();
    } else if (act && sel == ROW_RESET) {
        work = opened;
        pal = Ui::buildPalette(work);
        Ui::setPreviewPalette(&pal);
        dirty = false;
        SoundFX::play(SoundFX::FX::Back);
    } else if (act && sel == ROW_REMOVE) {
        removing = true;
        removeSel = 1;   /* Keep: A twice never removes by accident */
        SoundFX::play(SoundFX::FX::Select);
    }
    return false;
}

/* a slide: its colours from left to right, the ring where it is */
static void slide(float y, const char* label, const char* value, u32 from, u32 to, float t, bool focus)
{
    const Ui::Palette& p = Ui::pal();
    Ui::text(PX + 14, y + 1, label, 14, focus ? p.accentDark : p.text);
    Ui::roundRect(BX - 1, y + 3, BW + 2, 14, 3, p.cardBorder);
    Ui::quad(BX, y + 4, BW, 12, from, to, to, from);
    const float kx = BX + t * BW;
    Ui::roundRect(kx - 3, y, 6, 20, 3, 0xFFFFFFFF);
    Ui::roundBorder(kx - 3, y, 6, 20, 3, 1.5f, 0x000000C0);
    Ui::textRight(PX + PW - 14, y + 1, value, 14, p.textDim);
    if (focus) Ui::roundBorder(PX + 8, y - 3, PW - 16, 26, 8, 2.0f, Ui::alpha(p.focus, Ui::pulse()));
}

void ThemeEditor::render(ir_t& ir)
{
    const Ui::Palette& p = Ui::pal();
    Ui::background();
    Ui::header(tr("Customize Theme"), work.name.c_str());

    /* the colours, then Save As, Reset and Remove */
    for (int i = 0; i < ROW_COUNT; ++i) {
        if (i == ROW_REMOVE && !canRemove()) continue;
        rowAnim[i] = Ui::approach(rowAnim[i], i == sel && (!inPicker || i >= Ui::THEME_COLOR_COUNT) ? 1.0f
                                             : i == sel ? 0.45f : 0.0f);
        float x, y, w;
        rowRect(i, canRemove(), x, y, w);
        if (i >= Ui::THEME_COLOR_COUNT) {
            Ui::button(x, y, w, RH, i == ROW_SAVE ? N_("Save As...") : i == ROW_RESET ? N_("Reset") : N_("Remove"), 14,
                       rowAnim[i] > 0.01f ? Ui::pulse() * rowAnim[i] : 0.0f);
            continue;
        }
        Ui::card(LX, y, LW, RH, 9, rowAnim[i]);
        const u32 c = Ui::themeColor(pal, i);
        Ui::roundRect(LX + 7, y + 4, 16, 16, 4, c);
        Ui::roundBorder(LX + 7, y + 4, 16, 16, 4, 1.0f, Ui::mix(p.text, p.cardTop, 0.5f));
        Ui::text(LX + 30, y + 4, tr(Ui::THEME_COLORS[i].label), 14, Ui::mix(p.text, p.accentDark, rowAnim[i]));
        Ui::textRight(LX + LW - 8, y + 6, Ui::colorHex(c).c_str(), 11, p.textDim);
    }

    /* the picker, for the highlighted colour */
    Ui::card(PX, PY, PW, PH, 14, 0.0f);
    if (sel < Ui::THEME_COLOR_COUNT) {
        const u32 c = color();
        Ui::text(PX + 14, PY + 8, tr(Ui::THEME_COLORS[sel].label), 18, p.text);
        Ui::text(PX + 14, PY + 30, tr(Ui::THEME_COLORS[sel].desc), 12, p.textDim);

        /* the square: across, grey to the hue; down, to black */
        const u32 hue = hsvToRgb(h, 1, 1);
        Ui::roundRect(SQX - 1, SQY - 1, SQ + 2, SQ + 2, 3, p.cardBorder);
        Ui::quad(SQX, SQY, SQ, SQ, 0xFFFFFFFF, hue, hue, 0xFFFFFFFF);
        Ui::quad(SQX, SQY, SQ, SQ, 0x00000000, 0x00000000, 0x000000FF, 0x000000FF);
        const float rx = SQX + s * SQ, ry = SQY + (1 - v) * SQ;
        Ui::roundBorder(rx - 7, ry - 7, 14, 14, 7, 2.0f, 0x000000C0);
        Ui::roundBorder(rx - 5, ry - 5, 10, 10, 5, 2.0f, 0xFFFFFFFF);
        if (inPicker && part == P_SQUARE)
            Ui::roundBorder(SQX - 5, SQY - 5, SQ + 10, SQ + 10, 6, grabbed ? 3.0f : 2.0f,
                            grabbed ? p.focus : Ui::alpha(p.focus, Ui::pulse()));

        /* before and now */
        const float sx = SQX + SQ + 16, sw = PX + PW - 14 - sx;
        Ui::text(sx, SQY - 2, tr("Before"), 12, p.textDim);
        Ui::roundRect(sx, SQY + 14, sw, 36, 6, before);
        Ui::roundBorder(sx, SQY + 14, sw, 36, 6, 1.0f, p.cardBorder);
        Ui::text(sx, SQY + 62, tr("Now"), 12, p.textDim);
        Ui::roundRect(sx, SQY + 78, sw, 36, 6, c);
        Ui::roundBorder(sx, SQY + 78, sw, 36, 6, 1.0f, p.cardBorder);

        /* hue, red, green, blue */
        const int r = c >> 24, g = (c >> 16) & 0xFF, b = (c >> 8) & 0xFF;
        char val[16];
        const bool f = inPicker;
        {   /* the hue slide: six pieces of the rainbow */
            const float y = SLY;
            Ui::text(PX + 14, y + 1, tr("Hue"), 14, f && part == P_HUE ? p.accentDark : p.text);
            Ui::roundRect(BX - 1, y + 3, BW + 2, 14, 3, p.cardBorder);
            for (int k = 0; k < 6; ++k) {
                const u32 c0 = hsvToRgb(k * 60.0f, 1, 1), c1 = hsvToRgb((k + 1) * 60.0f - 0.01f, 1, 1);
                Ui::quad(BX + k * BW / 6, y + 4, BW / 6 + 0.5f, 12, c0, c1, c1, c0);
            }
            const float kx = BX + h / 360.0f * BW;
            Ui::roundRect(kx - 3, y, 6, 20, 3, 0xFFFFFFFF);
            Ui::roundBorder(kx - 3, y, 6, 20, 3, 1.5f, 0x000000C0);
            snprintf(val, sizeof(val), "%d\xc2\xb0", (int)h);
            Ui::textRight(PX + PW - 14, y + 1, val, 14, p.textDim);
            if (f && part == P_HUE) Ui::roundBorder(PX + 8, y - 3, PW - 16, 26, 8, 2.0f, Ui::alpha(p.focus, Ui::pulse()));
        }
        snprintf(val, sizeof(val), "%d", r);
        slide(SLY + SLSTEP, tr("Red"), val, rgb(0, g, b), rgb(255, g, b), r / 255.0f, f && part == P_RED);
        snprintf(val, sizeof(val), "%d", g);
        slide(SLY + 2 * SLSTEP, tr("Green"), val, rgb(r, 0, b), rgb(r, 255, b), g / 255.0f, f && part == P_GREEN);
        snprintf(val, sizeof(val), "%d", b);
        slide(SLY + 3 * SLSTEP, tr("Blue"), val, rgb(r, g, 0), rgb(r, g, 255), b / 255.0f, f && part == P_BLUE);

        /* typed: #RRGGBB or r,g,b */
        Ui::text(PX + 14, FY + 7, tr("Code"), 14, f && part == P_TEXT ? p.accentDark : p.text);
        char code[48];
        snprintf(code, sizeof(code), "%s   %d, %d, %d", Ui::colorHex(c).c_str(), r, g, b);
        Ui::field(BX, FY, PX + PW - 14 - BX, FH, code, 15, f && part == P_TEXT);
    } else if (sel == ROW_REMOVE) {
        Ui::text(PX + 14, PY + 8, tr("Remove"), 18, p.text);
        Ui::text(PX + 14, PY + 34, tr("Take this theme off the Theme list."), 13, p.textDim);
        Ui::text(PX + 14, PY + 56, tr("Its file goes to themes/removed/."), 13, p.textDim);
        Ui::text(PX + 14, PY + 78, tr("A PC can put it back from there."), 13, p.textDim);
        Ui::text(PX + 14, PY + 100, tr("Anything using it goes back to its base theme."), 13, p.textDim);
    } else {
        Ui::text(PX + 14, PY + 8, tr(sel == ROW_SAVE ? "Save As..." : "Reset"), 18, p.text);
        Ui::text(PX + 14, PY + 34, sel == ROW_SAVE ? tr("Save these colours as a theme of your own, under a name.")
                                                   : tr("Back to the colours the theme had when you opened it."), 13, p.textDim);
        if (sel == ROW_SAVE) {
            Ui::text(PX + 14, PY + 56, tr("It goes in themes/ next to WiiFin, where a PC can copy or edit it."), 13, p.textDim);
            Ui::text(PX + 14, PY + 78, tr("Saving under a theme's name replaces it."), 13, p.textDim);
        }
    }

    /* the name to save under, or a colour typed */
    if (typing != Typing::None) {
        GRRLIB_Rectangle(Ui::screenLeft(), 0, Ui::screenWidth(), 480, p.dim, 1);
        Ui::card((640 - kb.width()) * 0.5f - 10, 104 - kb.lift(), kb.width() + 20, kb.height() + 120, 16, 0.0f);
        Ui::text((640 - kb.width()) * 0.5f + 4, 116 - kb.lift(), typing == Typing::Name ? tr("Theme name") : tr("Colour"), 18, p.text);
        if (typing == Typing::Color)
            Ui::textRight((640 + kb.width()) * 0.5f - 4, 120 - kb.lift(), tr("#AA5CC3, or 170, 92, 195"), 13, p.textDim);
        const std::string shown = draft + "_";
        Ui::field((640 - kb.width()) * 0.5f, 150 - kb.lift(), kb.width(), 40, shown.c_str(), 18, true);
        kb.render(ir);
        const Ui::Hint l[] = { { "A", N_("Type") }, { "kbdel", N_("Delete") }, { "kbspace", N_("Space") }, { "kbsym", N_("Symbols") }, { "+!", N_("Done") } };
        Ui::bottomBar(l, 5);
        return;
    }

    if (asking) {
        GRRLIB_Rectangle(Ui::screenLeft(), 0, Ui::screenWidth(), 480, p.dim, 1);
        Ui::card(140, 170, 360, 130, 20, 0.0f);
        Ui::textCentered(320, 192, tr("Save your colours?"), 21, p.text);
        Ui::textCentered(320, 222, tr("Leaving without saving puts the theme back."), 14, p.textDim);
        Ui::button(170, 250, 140, 38, N_("Save As..."), 17, askSel == 0 ? Ui::pulse() : 0.0f);
        Ui::button(330, 250, 140, 38, N_("Don't Save"), 17, askSel == 1 ? Ui::pulse() : 0.0f);
        const Ui::Hint l[] = { { "A", N_("Select") }, { "B", N_("Cancel") } };
        Ui::bottomBar(l, 2);
        return;
    }

    if (removing) {
        GRRLIB_Rectangle(Ui::screenLeft(), 0, Ui::screenWidth(), 480, p.dim, 1);
        Ui::card(140, 160, 360, 160, 20, 0.0f);
        char q[96];
        /* TRANSLATORS: Customize Theme > Remove: %s is the theme's name */
        snprintf(q, sizeof(q), tr("Remove \"%s\"?"), opened.name.c_str());
        Ui::textCentered(320, 182, q, 21, p.text);
        Ui::textCentered(320, 214, tr("It moves to themes/removed/ on the SD card."), 14, p.textDim);
        Ui::textCentered(320, 234, tr("A PC can put it back."), 14, p.textDim);
        Ui::button(170, 266, 140, 38, N_("Remove"), 17, removeSel == 0 ? Ui::pulse() : 0.0f);
        Ui::button(330, 266, 140, 38, N_("Keep"), 17, removeSel == 1 ? Ui::pulse() : 0.0f);
        const Ui::Hint l[] = { { "A", N_("Select") }, { "B", N_("Cancel") } };
        Ui::bottomBar(l, 2);
        return;
    }

    if (!inPicker) {
        const Ui::Hint l[] = { { "A", N_("Select") } };
        const Ui::Hint r[] = { { "B", N_("Done") } };
        Ui::bottomBar(l, 1, r, 1);
    } else if (grabbed) {
        const Ui::Hint l[] = { { "UD", N_("Brightness") }, { "LR", N_("Shade") }, { "A", N_("Done") } };
        Ui::bottomBar(l, 3);
    } else if (part == P_SQUARE || part == P_TEXT) {
        const Ui::Hint l[] = { { "A", part == P_SQUARE ? N_("Move") : N_("Type") }, { "-", N_("Undo") } };
        const Ui::Hint r[] = { { "B", N_("Back") } };
        Ui::bottomBar(l, 2, r, 1);
    } else {
        const Ui::Hint l[] = { { "LR", N_("Adjust") }, { "-", N_("Undo") } };
        const Ui::Hint r[] = { { "B", N_("Back") } };
        Ui::bottomBar(l, 2, r, 1);
    }
}
