#include "Screensaver.h"
#include "../ui/Ui.h"
#include "../ui/JpegTexture.h"
#include "../player/WiiPlayer.h"
#include <grrlib.h>
#include <ogc/lwp_watchdog.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

extern unsigned char data_logo_wiifin_png[];

namespace {

int   s_minutes = 3;
int   s_logoKind = Screensaver::LogoWiiFin;
std::string s_dir;               /* WiiFin's folder: screensaver.png there */
u64   s_lastActive = 0;          /* ms */
bool  s_on = false;

GRRLIB_texImg* s_wiifin = nullptr;
GRRLIB_texImg* s_own = nullptr;
bool  s_ownTried = false;

/* The bounce, in whole steps so it can land exactly in a corner: the
 * logo's travel across (tx) and down (ty) are rounded to multiples of 4
 * and it moves 2 units a frame each way, so it is in a corner every
 * lcm(tx, ty) / 2 frames (some minutes); it starts on a path out of one,
 * already a little way along. */
const int STEP = 2;
int   s_px = 0, s_py = 0, s_vx = STEP, s_vy = STEP;   /* in its box */
int   s_tx = 0, s_ty = 0;
float s_w = 0, s_h = 0, s_x0 = 0, s_y0 = 0;           /* the logo, the box's corner */
int   s_tint = 0;
int   s_flash = 0;                                    /* frames left of a corner's flash */
u32   s_corners = 0;

/* the logos' colours, one more at each edge: the WiiFin logo and a picture
 * of one's own are multiplied (their own colours kept, shifted); the DVD
 * one is drawn in each */
const u32 TINTS[] = { 0xFFFFFFFF, 0xFF9C9CFF, 0x9CFFA8FF, 0x9CC8FFFF, 0xFFE08CFF, 0xE0A0FFFF, 0x8CFFF0FF };
const u32 SOLID[] = { 0xBE00FFFF, 0x00FEFFFF, 0xFF8300FF, 0x0026FFFF, 0xFFFA01FF, 0xFF2600FF, 0xFF008BFF, 0x25FF01FF };
const int N_TINTS = sizeof(TINTS) / sizeof(TINTS[0]), N_SOLID = sizeof(SOLID) / sizeof(SOLID[0]);

u64 nowMs() { return ticks_to_millisecs(gettime()); }

GRRLIB_texImg* ownPicture()
{
    if (s_ownTried) return s_own;
    s_ownTried = true;
    for (const char* name : { "screensaver.png", "screensaver.jpg" }) {
        FILE* f = fopen((s_dir + name).c_str(), "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        const long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        u8* data = n > 0 && n < 3 * 1024 * 1024 ? (u8*)memalign(32, n) : nullptr;
        if (data && fread(data, 1, n, f) == (size_t)n)
            s_own = data[0] == 0xFF && data[1] == 0xD8 ? loadJPEGTexture(data, (u32)n) : GRRLIB_LoadTexture(data);
        free(data);
        fclose(f);
        if (s_own && (s_own->w > 1024 || s_own->h > 1024)) { GRRLIB_FreeTexture(s_own); s_own = nullptr; }
        SYS_Report("[Screensaver] %s: %s\n", name, s_own ? "loaded" : "not loaded");
        if (s_own) break;
    }
    return s_own;
}

/* the logo shown: the one chosen, else the WiiFin one */
int kind()
{
    if (s_logoKind == Screensaver::LogoOwn && !ownPicture()) return Screensaver::LogoWiiFin;
    return s_logoKind;
}

void logoSize(float& w, float& h)
{
    switch (kind()) {
    case Screensaver::LogoDvd: w = 196; h = 104; return;
    case Screensaver::LogoOwn: {
        GRRLIB_texImg* t = ownPicture();
        const float k = 220.0f / (t->w > t->h ? t->w : t->h);
        w = t->w * k; h = t->h * k;
        return;
    }
    default:
        if (!s_wiifin) s_wiifin = GRRLIB_LoadTexture(data_logo_wiifin_png);
        w = 220;
        h = s_wiifin ? 220.0f * s_wiifin->h / s_wiifin->w : 55;
        return;
    }
}

int gcd(int a, int b) { while (b) { const int t = a % b; a = b; b = t; } return a; }

void startBounce()
{
    logoSize(s_w, s_h);
    const float L = Ui::screenLeft(), W = Ui::screenWidth();
    s_tx = (int)(W - s_w) / 4 * 4;
    s_ty = (int)(480 - s_h) / 4 * 4;
    s_x0 = L + (W - s_w - s_tx) * 0.5f;
    s_y0 = (480 - s_h - s_ty) * 0.5f;
    /* out of the top-left corner, 40 frames ago */
    s_px = s_py = 40 * STEP;
    s_vx = s_vy = STEP;
    s_flash = 0;
    const long period = (long)s_tx / gcd(s_tx, s_ty) * s_ty / STEP;
    SYS_Report("[Screensaver] on after %d min: a corner every %ld frames (%ld s at 60 Hz)\n",
               s_minutes, period, period / 60);
}

/* the DVD-style logo: the letters over a disc, all in one colour */
void drawDvd(float x, float y, u32 c)
{
    const char* dvd = "DVD";
    const int size = 62;
    const float cx = x + s_w / 2;
    for (int dx = 0; dx <= 2; ++dx)     /* heavier letters: drawn three times across */
        Ui::textCentered(cx - 1 + dx, y - 6, dvd, size, c);
    /* the disc: a flat ring, "VIDEO" in its middle */
    const float dy = y + 70, dw = s_w * 0.86f, dh = 30;
    Ui::roundRect(cx - dw / 2, dy, dw, dh, dh / 2, c);
    Ui::roundRect(cx - dw * 0.30f, dy + 8, dw * 0.60f, dh - 16, (dh - 16) / 2, 0x000000FF);
    Ui::textCentered(cx, dy + 9, "VIDEO", 11, c);
}

}

void Screensaver::setDir(const std::string& appDir) { s_dir = appDir; s_ownTried = false; }

void Screensaver::setMinutes(int m) { s_minutes = m < 0 ? 0 : m; s_lastActive = nowMs(); }
int  Screensaver::minutes() { return s_minutes; }

void Screensaver::setLogo(int l) { s_logoKind = l >= 0 && l < LOGO_COUNT ? l : LogoWiiFin; s_ownTried = false; }
int  Screensaver::logo() { return s_logoKind; }
bool Screensaver::ownLogoFound() { s_ownTried = false; return ownPicture() != nullptr; }

int Screensaver::nextMinutes(int m, int dir)
{
    static const int CHOICES[] = { 0, 1, 3, 5, 10, 15, 30 };
    const int n = sizeof(CHOICES) / sizeof(CHOICES[0]);
    int at = 0;
    for (int i = 0; i < n; ++i) if (CHOICES[i] == m) at = i;
    return CHOICES[(at + dir + n) % n];
}

bool Screensaver::active() { return s_on; }

bool Screensaver::input(bool activity)
{
    const u64 now = nowMs();
    if (!s_lastActive) s_lastActive = now;
    /* playing (not paused): never idle */
    if (activity || wii_player_playing()) {
        s_lastActive = now;
        if (s_on) {
            s_on = false;
            SYS_Report("[Screensaver] off (%u corner%s)\n", (unsigned)s_corners, s_corners == 1 ? "" : "s");
            return activity;
        }
        return false;
    }
    if (!s_on && s_minutes > 0 && now - s_lastActive >= (u64)s_minutes * 60000) {
        s_on = true;
        s_corners = 0;
        startBounce();
    }
    return false;
}

void Screensaver::draw()
{
    if (!s_on) return;
    const float L = Ui::screenLeft(), R = Ui::screenRight();
    GRRLIB_Rectangle(L - 2, -2, R - L + 4, 484, 0x000000FF, 1);
    /* a step; an edge reached turns it back and changes its colour */
    s_px += s_vx;
    s_py += s_vy;
    bool hitX = false, hitY = false;
    if (s_px <= 0 || s_px >= s_tx) { s_px = s_px <= 0 ? 0 : s_tx; s_vx = -s_vx; hitX = true; }
    if (s_py <= 0 || s_py >= s_ty) { s_py = s_py <= 0 ? 0 : s_ty; s_vy = -s_vy; hitY = true; }
    if (hitX || hitY) ++s_tint;
    if (hitX && hitY) {   /* right in the corner */
        s_flash = 90;
        ++s_corners;
        SYS_Report("[Screensaver] corner!\n");
    }
    const float x = s_x0 + s_px, y = s_y0 + s_py;
    const int k = kind();
    /* the corner: a second and a half of colours, one a frame, and a glow */
    const int t = s_flash > 0 ? s_tint + s_flash / 3 : s_tint;
    if (s_flash > 0) {
        const float a = s_flash / 90.0f;
        Ui::shadow(x, y, s_w, s_h, 16, 26.0f, Ui::alpha(SOLID[t % N_SOLID], 0.7f * a));
        --s_flash;
    }
    if (k == LogoDvd) {
        drawDvd(x, y, SOLID[t % N_SOLID]);
        return;
    }
    GRRLIB_texImg* tex = k == LogoOwn ? ownPicture() : s_wiifin;
    if (!tex) return;
    const float kx = s_w / tex->w, ky = s_h / tex->h;
    GRRLIB_DrawImg(x, y, tex, 0, kx, ky, (TINTS[t % N_TINTS] & 0xFFFFFF00) | 0xC8);
}
