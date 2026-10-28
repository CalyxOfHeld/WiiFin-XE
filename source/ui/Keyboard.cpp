#include "Keyboard.h"
#include "Ui.h"
#include "../input/Input.h"
#include "../core/SoundFX.h"
#include "../core/Lang.h"

#include <string.h>
#include <ogc/pad.h>
#include <ogc/lwp_watchdog.h>

static const char* const LETTERS[4] = {
    "1234567890-",
    "qwertyuiop/",
    "asdfghjkl:@",
    "zxcvbnm,._'",
};
static const char* const SYMBOLS[4] = {
    "!?#$%&*()=+",
    "\"<>[]{}|\\;~",
    "^`_-/:@,.'*",
    "1234567890.",
};

/* Bottom row: wide keys, as (first column, span); for an address, http://
 * and https:// in Space's place, plus cursor navigation keys */
struct BottomKey { int col, span; int sp; };
static const BottomKey BOTTOM[] = {
    { 0, 2, 1 /* K_SHIFT */ }, { 2, 1, 2 /* K_SYM */ },
    { 3, 1, 8 /* K_LEFT */ },  { 4, 1, 9 /* K_RIGHT */ },
    { 5, 2, 3 /* K_SPACE */ }, { 7, 2, 4 /* K_DEL */ },
    { 9, 2, 5 /* K_ENTER */ }, { 0, 0, 0 },
};
static const BottomKey BOTTOM_PASSWORD[] = {
    { 0, 2, 1 /* K_SHIFT */ }, { 2, 1, 2 /* K_SYM */ },
    { 3, 1, 8 /* K_LEFT */ },  { 4, 1, 9 /* K_RIGHT */ },
    { 5, 2, 3 /* K_SPACE */ }, { 7, 1, 10 /* K_PW_TOGGLE */ },
    { 8, 1, 4 /* K_DEL */ },   { 9, 2, 5 /* K_ENTER */ },
    { 0, 0, 0 },
};
static const BottomKey BOTTOM_URL[] = {
    { 0, 2, 1 /* K_SHIFT */ }, { 2, 1, 2 /* K_SYM */ },
    { 3, 1, 8 /* K_LEFT */ },  { 4, 1, 9 /* K_RIGHT */ },
    { 5, 2, 6 /* K_HTTP */ },  { 7, 2, 7 /* K_HTTPS */ },
    { 9, 1, 4 /* K_DEL */ },   { 10, 1, 5 /* K_ENTER */ },
    { 0, 0, 0 },
};

static void moveCursorLeft(const std::string& text, int& pos) {
    if (pos <= 0) { pos = 0; return; }
    pos--;
    while (pos > 0 && ((unsigned char)text[pos] & 0xC0) == 0x80) pos--;
}

static void moveCursorRight(const std::string& text, int& pos) {
    if (pos >= (int)text.size()) { pos = (int)text.size(); return; }
    pos++;
    while (pos < (int)text.size() && ((unsigned char)text[pos] & 0xC0) == 0x80) pos++;
}

static void eraseBefore(std::string& text, int& pos) {
    if (pos <= 0 || text.empty()) return;
    int start = pos - 1;
    while (start > 0 && ((unsigned char)text[start] & 0xC0) == 0x80) start--;
    text.erase(start, pos - start);
    pos = start;
}

static unsigned long long nowMs() { return ticks_to_millisecs(gettime()); }

/* the keys' size: Normal, Large, Larger (the widest 616 across: inside the
 * 4:3 picture's 640) */
static int s_size = Keyboard::SizeLarger;
static const float CELL_WS[Keyboard::SIZE_COUNT] = { 46, 52, 56 };
static const float CELL_HS[Keyboard::SIZE_COUNT] = { 40, 45, 49 };
void  Keyboard::setSize(int s) { s_size = s >= 0 && s < SIZE_COUNT ? s : SizeNormal; }
int   Keyboard::size()  { return s_size; }
float Keyboard::cellW() { return CELL_WS[s_size]; }
float Keyboard::cellH() { return CELL_HS[s_size]; }

void Keyboard::reset()
{
    page = 0; shift = 0;
    selRow = 1; selCol = 0;
    cursor = -1;
    flashMs = 0;
    pwShown = false;
    customHeaderNav = false;
}

void Keyboard::setCursorPos(int pos)
{
    cursor = pos;
}

int Keyboard::calcScrollOffset(const std::string& text, int maxW, int fontSize) const
{
    int cPos = cursor;
    if (cPos < 0 || cPos > (int)text.size()) cPos = (int)text.size();
    int scroll = 0;
    while (cPos > scroll && Ui::textWidth(text.substr(scroll, cPos - scroll).c_str(), fontSize) > maxW - 20)
        scroll++;
    return scroll;
}

int Keyboard::charIndexAt(const std::string& text, int fontSize, float clickX)
{
    if (clickX <= 0) return 0;
    int len = (int)text.size();
    float prevW = 0.0f;
    for (int i = 1; i <= len; ++i) {
        float curW = Ui::textWidth(text.substr(0, i).c_str(), fontSize);
        float midW = (prevW + curW) * 0.5f;
        if (clickX < midW) return i - 1;
        prevW = curW;
    }
    return len;
}

Keyboard::Key Keyboard::keyAt(int row, int col) const
{
    Key k{ row, col, 1, 0, K_NONE };
    if (row < 0 || row >= ROWS || col < 0 || col >= COLS) return k;
    if (row < 4) {
        const char* r = (page == 0 ? LETTERS : SYMBOLS)[row];
        if (r && col < (int)strlen(r)) k.ch = r[col];
        return k;
    }
    const BottomKey* bList = urlKeys ? BOTTOM_URL : (passwordMode ? BOTTOM_PASSWORD : BOTTOM);
    for (const BottomKey* b = bList; b->span; ++b)
        if (col >= b->col && col < b->col + b->span) {
            k.col = b->col; k.span = b->span; k.sp = (Special)b->sp;
            return k;
        }
    return k;
}

Keyboard::Key Keyboard::bottomKey(Special sp) const
{
    const BottomKey* bList = urlKeys ? BOTTOM_URL : (passwordMode ? BOTTOM_PASSWORD : BOTTOM);
    for (const BottomKey* b = bList; b->span; ++b)
        if (b->sp == sp) return Key{ ROWS - 1, b->col, b->span, 0, sp };
    return Key{ ROWS - 1, 0, 1, 0, K_NONE };
}

bool Keyboard::keyUnder(const ir_t& ir, int& row, int& col) const
{
    if (!ir.valid) return false;
    float fx = (ir.x - x0) / cellW(), fy = (ir.y - y0) / cellH();
    if (fx < 0 || fy < 0 || fx >= COLS || fy >= ROWS) return false;
    row = (int)fy; col = (int)fx;
    /* the gaps between keys count as nothing */
    float cx = fx - col, cy = fy - row;
    if (row < 4 && (cx < 0.04f || cx > 0.96f)) return false;
    if (cy < 0.05f || cy > 0.95f) return false;
    return true;
}

Keyboard::Result Keyboard::press(const Key& k, std::string& text, size_t maxLen)
{
    flashRow = k.row; flashCol = k.col; flashMs = nowMs();
    if (cursor < 0 || cursor > (int)text.size()) cursor = (int)text.size();
    switch (k.sp) {
    case K_SHIFT: {
        unsigned long long t = nowMs();
        if (shift == 1 && t - lastShiftMs < 450) shift = 2;   /* double press: lock */
        else shift = shift ? 0 : 1;
        lastShiftMs = t;
        SoundFX::play(SoundFX::FX::PressKey);
        return Result::None;
    }
    case K_SYM:
        page ^= 1;
        SoundFX::play(SoundFX::FX::PressKey);
        return Result::None;
    case K_SPACE:
        if (text.size() < maxLen) {
            text.insert(cursor, 1, ' ');
            cursor++;
            SoundFX::play(SoundFX::FX::PressKey);
        }
        return Result::None;
    case K_DEL:
        eraseBefore(text, cursor);
        SoundFX::play(SoundFX::FX::Backspace);
        return Result::None;
    case K_LEFT:
        moveCursorLeft(text, cursor);
        SoundFX::play(SoundFX::FX::Move);
        return Result::None;
    case K_RIGHT:
        moveCursorRight(text, cursor);
        SoundFX::play(SoundFX::FX::Move);
        return Result::None;
    case K_ENTER:
        return Result::Enter;
    case K_HTTP:
    case K_HTTPS: {   /* the scheme in front, in place of one already there */
        size_t rest = 0;
        if (text.compare(0, 7, "http://") == 0) rest = 7;
        else if (text.compare(0, 8, "https://") == 0) rest = 8;
        const std::string prefix = (k.sp == K_HTTPS ? "https://" : "http://");
        const std::string t = prefix + text.substr(rest);
        if (t.size() <= maxLen) {
            int diff = (int)prefix.size() - (int)rest;
            text = t;
            cursor = std::max(0, std::min((int)text.size(), cursor + diff));
        }
        SoundFX::play(SoundFX::FX::PressKey);
        return Result::None;
    }
    case K_PW_TOGGLE:
        pwShown = !pwShown;
        SoundFX::play(SoundFX::FX::Start);
        return Result::None;
    default: break;
    }
    if (!k.ch || text.size() >= maxLen) return Result::None;
    char c = k.ch;
    if (shift && c >= 'a' && c <= 'z') c = (char)(c - 32);
    text.insert(cursor, 1, c);
    cursor++;
    if (shift == 1 && page == 0 && k.ch >= 'a' && k.ch <= 'z') shift = 0;   /* one-shot */
    SoundFX::play(SoundFX::FX::PressKey);
    return Result::None;
}

Keyboard::Result Keyboard::update(const ir_t& ir, std::string& text, size_t maxLen)
{
    if (cursor < 0 || cursor > (int)text.size()) cursor = (int)text.size();

    /* typed on a USB keyboard: straight into the text; Enter is done */
    while (int c = Input::typedChar()) {
        if (c == '\n') return Result::Enter;
        if (c == '\b') {
            eraseBefore(text, cursor);
            SoundFX::play(SoundFX::FX::Backspace);
        } else if (c == 0x7F) { // Delete key
            if (cursor < (int)text.size()) {
                int next = cursor + 1;
                while (next < (int)text.size() && ((unsigned char)text[next] & 0xC0) == 0x80) next++;
                text.erase(cursor, next - cursor);
                SoundFX::play(SoundFX::FX::Backspace);
            }
        } else if (c == 0x11) { // Left arrow
            moveCursorLeft(text, cursor);
            SoundFX::play(SoundFX::FX::Move);
        } else if (c == 0x12) { // Right arrow
            moveCursorRight(text, cursor);
            SoundFX::play(SoundFX::FX::Move);
        } else if (c == 0x01) { // Home
            cursor = 0;
            SoundFX::play(SoundFX::FX::Move);
        } else if (c == 0x05) { // End
            cursor = (int)text.size();
            SoundFX::play(SoundFX::FX::Move);
        } else if (c >= 32 && c < 127 && text.size() < maxLen) {
            text.insert(cursor, 1, (char)c);
            cursor++;
            SoundFX::play(SoundFX::FX::PressKey);
        }
    }

    /* Pointer: check text field click */
    if (hasTextField && ir.valid) {
        if (ir.x >= tfX && ir.x <= tfX + tfW && ir.y >= tfY && ir.y <= tfY + tfH) {
            if (Input::isAJustPressed()) {
                const float startX = tfX + tfH * 0.5f;
                int scroll = calcScrollOffset(text, (int)(tfW - tfH), 18);
                std::string vis = text.substr(scroll);
                int rel = charIndexAt(vis, 18, ir.x - startX);
                cursor = std::max(0, std::min((int)text.size(), scroll + rel));
                selRow = -1;
                SoundFX::play(SoundFX::FX::Move);
                return Result::None;
            }
        }
    }

    /* Pointer: the highlight follows it */
    int pr, pc;
    bool overKey = keyUnder(ir, pr, pc);
    if (overKey) { Key k = keyAt(pr, pc); selRow = k.row; selCol = k.col; }

    /* D-pad */
    if (selRow < 0) {
        if (!customHeaderNav) {
            if (Input::isLeftPressed()) {
                moveCursorLeft(text, cursor);
                SoundFX::play(SoundFX::FX::Move);
            }
            if (Input::isRightPressed()) {
                moveCursorRight(text, cursor);
                SoundFX::play(SoundFX::FX::Move);
            }
            if (Input::isDownPressed()) {
                selRow = 0;
                SoundFX::play(SoundFX::FX::Move);
            }
            if (Input::isUpPressed()) {
                selRow = ROWS - 1;
                SoundFX::play(SoundFX::FX::Move);
            }
        }
    } else {
        if (Input::isUpPressed()) {
            if (selRow == 0 && hasTextField) {
                selRow = -1;
                SoundFX::play(SoundFX::FX::Move);
            } else {
                selRow = (selRow + ROWS - 1) % ROWS;
            }
        }
        if (Input::isDownPressed()) {
            if (selRow == ROWS - 1 && hasTextField) {
                selRow = -1;
                SoundFX::play(SoundFX::FX::Move);
            } else {
                selRow = (selRow + 1) % ROWS;
            }
        }
        if (Input::isLeftPressed()) {
            Key k = keyAt(selRow, selCol);
            selCol = (k.col + COLS - 1) % COLS;
        }
        if (Input::isRightPressed()) {
            Key k = keyAt(selRow, selCol);
            selCol = (k.col + k.span) % COLS;
        }
    }

    if (Input::isAJustPressed()) {
        if (ir.valid) {
            /* with the pointer on screen, only the key under it counts */
            if (overKey) return press(keyAt(pr, pc), text, maxLen);
        } else {
            if (selRow >= 0) return press(keyAt(selRow, selCol), text, maxLen);
        }
    }
    /* the shortcuts: - deletes, 1 a space, 2 the symbols; a GameCube
     * controller's own: X, Y, R, and L Shift (its X and R would otherwise
     * be the Wii Remote's 2 and +) */
    const u16 gc = Input::gcRawDown();
    const bool del   = gc ? (gc & PAD_BUTTON_X) != 0  : Input::isLPressed();
    const bool space = gc ? (gc & PAD_BUTTON_Y) != 0  : Input::is1Pressed();
    const bool sym   = gc ? (gc & PAD_TRIGGER_R) != 0 : Input::is2Pressed();
    if (gc & PAD_TRIGGER_L) press(bottomKey(K_SHIFT), text, maxLen);
    if (del)   press(bottomKey(K_DEL), text, maxLen);
    if (space) {   /* for an address, a space all the same (no key for it) */
        Key k = bottomKey(K_SPACE);
        if (k.sp == K_NONE) { k.sp = K_SPACE; k.row = -1; }
        press(k, text, maxLen);
    }
    if (sym)   press(bottomKey(K_SYM), text, maxLen);
    if (Input::isActionPressed()) return Result::Enter;                       /* + (GameCube Z) */
    if (Input::isBackPressed()) {
        if (selRow < 0 || text.empty()) return Result::Cancel;
        eraseBefore(text, cursor);
        SoundFX::play(SoundFX::FX::Backspace);
    }
    return Result::None;
}

const char* Keyboard::label(const Key& k, char* buf) const
{
    switch (k.sp) {
    /* TRANSLATORS: keys of the on-screen keyboard; keep the arrow in front */
    case K_SHIFT: return shift == 2 ? N_("\xe2\x87\xaa Caps") : N_("\xe2\x87\xa7 Shift");
    case K_SYM:   return page == 0 ? "#+=" : "ABC";
    case K_SPACE: return N_("Space");
    case K_DEL:   return k.span == 1 ? N_("Del") : N_("\xe2\x86\x90 Delete");
    case K_ENTER: return enterLabel;
    case K_HTTP:  return "http://";
    case K_HTTPS: return "https://";
    case K_LEFT:  return "<";
    case K_RIGHT: return ">";
    case K_PW_TOGGLE: return pwShown ? N_("Hide") : N_("Show");
    default: break;
    }
    buf[0] = (shift && k.ch >= 'a' && k.ch <= 'z') ? (char)(k.ch - 32) : k.ch;
    buf[1] = 0;
    return buf;
}

void Keyboard::render(const ir_t& ir) const
{
    const Ui::Palette& p = Ui::pal();
    int hr = -1, hc = -1;
    bool hover = keyUnder(ir, hr, hc);
    Key hk = hover ? keyAt(hr, hc) : Key{ -1, -1, 0, 0, K_NONE };
    Key sk = (selRow >= 0 && selRow < ROWS) ? keyAt(selRow, selCol) : Key{ -1, -1, 0, 0, K_NONE };
    unsigned long long t = nowMs();
    const float scale = cellH() / 40.0f;   /* the labels grow with the keys */
    const int   esz = (int)(15 * scale + 0.5f);

    for (int row = 0; row < ROWS; ++row) {
        for (int col = 0; col < COLS; ) {
            Key k = keyAt(row, col);
            col = k.col + k.span;
            float x = x0 + k.col * cellW() + 2, y = y0 + row * cellH() + 2;
            float w = k.span * cellW() - 4, h = cellH() - 4;
            bool sel = (k.row == sk.row && k.col == sk.col);
            bool hov = hover && k.row == hk.row && k.col == hk.col;
            /* typed: the key dips for a moment */
            float flash = (k.row == flashRow && k.col == flashCol && t - flashMs < 140)
                        ? 1.0f - (t - flashMs) / 140.0f : 0.0f;
            if (flash > 0) { x += 1.5f * flash; y += 2.0f * flash; w -= 3 * flash; h -= 3 * flash; }

            char buf[4];
            const char* lab = label(k, buf);
            bool special = k.sp != K_NONE;
            if (k.sp == K_ENTER) {
                /* action key: always in the accent colour */
                if (sel) Ui::shadow(x - 2, y - 2, w + 4, h + 4, 9, 8.0f, p.glow);
                Ui::roundRect(x, y, w, h, 7, Ui::mix(p.accent, 0xFFFFFFFF, sel ? 0.35f : 0.15f), p.accentDark);
                if (sel) Ui::roundBorder(x, y, w, h, 7, 2.0f, 0xFFFFFFC0);
                Ui::textCentered(x + w * 0.5f, y + (h - esz) * 0.5f - 1, tr(lab), esz, p.textOnAccent);
                continue;
            }
            bool latched = (k.sp == K_SHIFT && shift) || (k.sp == K_SYM && page == 1);
            Ui::key(x, y, w, h, lab, sel, hov && !sel, special, latched, scale);
        }
    }
}
