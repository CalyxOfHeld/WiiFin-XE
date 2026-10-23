#include "Screensaver.h"
#include "../ui/Ui.h"
#include "../ui/JpegTexture.h"
#include "../ui/Avatars.h"
#include "../player/WiiPlayer.h"
#include <grrlib.h>
#include <ogc/lwp_watchdog.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <string>

extern unsigned char data_logo_wiifin_png[];

namespace {

int   s_minutes = 3;
int   s_logoKind = Screensaver::LogoWiiFin;
int   s_speed = Screensaver::SpeedNormal;
int   s_effect = Screensaver::EffectEdge;
std::string s_dir;               /* WiiFin's folder: screensaver.png there */
u64   s_lastActive = 0;          /* ms */
bool  s_on = false;

GRRLIB_texImg* s_wiifin = nullptr;

/* the picture: its name (from WiiFin's folder), it loaded (shrunk), the one
 * that would not load (not tried again until chosen again), and whether its
 * file is there (looked at once, when chosen: Settings asks every frame) */
std::string s_pic, s_picLoaded, s_picFailed;
GRRLIB_texImg* s_own = nullptr;
bool  s_picThere = false;

/* the picture's limits: the file, a PNG decoded whole (a JPEG is decoded
 * at a half, a quarter or an eighth), and the size it is kept at */
const long FILE_MAX = 3 * 1024 * 1024;
const u32  PNG_MAX  = 1600;
const u32  KEEP_MAX = 256;

/* The bounce, in whole steps so it can land exactly in a corner: the
 * logo's travel across (tx) and down (ty) are rounded to multiples of 4
 * and it moves 1, 2 or 4 units a frame each way, so it is in a corner every
 * lcm(tx, ty) / step frames (some minutes); it starts on a path out of one,
 * already a little way along. */
const int STEPS[Screensaver::SPEED_COUNT] = { 1, 2, 4 };
int   s_step = 2;
int   s_px = 0, s_py = 0, s_vx = 2, s_vy = 2;         /* in its box */
int   s_tx = 0, s_ty = 0;
float s_w = 0, s_h = 0, s_x0 = 0, s_y0 = 0;           /* the logo, the box's corner */
int   s_kindNow = Screensaver::LogoWiiFin;              /* what is drifting (kind(), at the start) */
int   s_tint = 0;
u32   s_fade = 0;                                     /* frames, Fade */
int   s_flash = 0;                                    /* frames left of a corner's glow */
u32   s_corners = 0;

/* the logos' colours, one more at each edge: the WiiFin logo and a picture
 * of one's own are multiplied (their own colours kept, shifted); the DVD
 * one is drawn in each */
const u32 TINTS[] = { 0xFFFFFFFF, 0xFF9C9CFF, 0x9CFFA8FF, 0x9CC8FFFF, 0xFFE08CFF, 0xE0A0FFFF, 0x8CFFF0FF };
const u32 SOLID[] = { 0xBE00FFFF, 0x00FEFFFF, 0xFF8300FF, 0x0026FFFF, 0xFFFA01FF, 0xFF2600FF, 0xFF008BFF, 0x25FF01FF };
const int N_TINTS = sizeof(TINTS) / sizeof(TINTS[0]), N_SOLID = sizeof(SOLID) / sizeof(SOLID[0]);
const u32 FADE_FRAMES = 240;                          /* 4 s from a colour to the next */

u64 nowMs() { return ticks_to_millisecs(gettime()); }

u32 be32(const u8* p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

void freePicture()
{
    if (s_own) GRRLIB_FreeTexture(s_own);
    s_own = nullptr;
    s_picLoaded.clear();
}

bool fileThere(const std::string& path)
{
    struct stat st;
    return !path.empty() && stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

/* the picture chosen, loaded once.  Never while a video is open (paused):
 * its memory is the video's then, so the WiiFin logo stands in */
GRRLIB_texImg* ownPicture()
{
    if (s_pic.empty() || s_dir.empty()) return nullptr;
    if (s_own && s_picLoaded == s_pic) return s_own;
    if (s_picFailed == s_pic || wii_player_is_running()) return nullptr;
    freePicture();
    const std::string path = s_dir + s_pic;
    FILE* f = fopen(path.c_str(), "rb");
    const char* why = "no file";
    if (f) {
        fseek(f, 0, SEEK_END);
        const long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        u8* data = n > 24 && n <= FILE_MAX ? (u8*)memalign(32, n) : nullptr;
        why = n > FILE_MAX ? "over 3 MB" : "no memory";
        if (data && fread(data, 1, n, f) == (size_t)n) {
            GRRLIB_texImg* t = nullptr;
            if (data[0] == 0xFF && data[1] == 0xD8) {
                t = loadJPEGTexture(data, (u32)n, 2 * KEEP_MAX);
                why = "not a JPEG it could read";
            } else if (!memcmp(data + 1, "PNG", 3)) {
                const u32 w = be32(data + 16), h = be32(data + 20);
                why = "a PNG over 1600 pixels";
                if (w && h && w <= PNG_MAX && h <= PNG_MAX) {
                    t = GRRLIB_LoadTexture(data);
                    why = "not a PNG it could read";
                }
            } else why = "not a PNG or JPEG";
            if (t && t->w && t->h) s_own = shrinkTexture(t, KEEP_MAX);
            else if (t) GRRLIB_FreeTexture(t);
        }
        free(data);
        fclose(f);
    }
    if (s_own) s_picLoaded = s_pic;
    else       s_picFailed = s_pic;
    SYS_Report("[Screensaver] %s: %s%s\n", s_pic.c_str(), s_own ? "loaded" : "not used: ", s_own ? "" : why);
    if (s_own) SYS_Report("[Screensaver] kept at %ux%u\n", s_own->w, s_own->h);
    return s_own;
}

u32 blend(u32 a, u32 b, float f)
{
    u32 out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        const float ca = (a >> sh) & 0xFF, cb = (b >> sh) & 0xFF;
        out |= (u32)(ca + (cb - ca) * f + 0.5f) << sh;
    }
    return out;
}

/* the colour now, from one of the sets */
u32 colourOf(const u32* set, int n)
{
    if (s_effect == Screensaver::EffectNone) return 0xFFFFFFFF;
    if (s_effect == Screensaver::EffectFade) {
        const u32 i = s_fade / FADE_FRAMES;
        return blend(set[i % n], set[(i + 1) % n], (float)(s_fade % FADE_FRAMES) / FADE_FRAMES);
    }
    return set[s_tint % n];
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
    s_kindNow = kind();
    logoSize(s_w, s_h);
    s_step = STEPS[s_speed];
    const float L = Ui::screenLeft(), W = Ui::screenWidth();
    s_tx = (int)(W - s_w) / 4 * 4;
    s_ty = (int)(480 - s_h) / 4 * 4;
    if (s_tx < 8) s_tx = 8;
    if (s_ty < 8) s_ty = 8;
    /* a path that comes back to a corner within half a minute (the box
     * nearly square to the logo's travel) only bounces corner to corner:
     * a step narrower */
    long period = (long)s_tx / gcd(s_tx, s_ty) * s_ty / s_step;
    for (int tries = 0; tries < 8 && period < 30 * 60 && s_tx > 8; ++tries) {
        s_tx -= 4;
        period = (long)s_tx / gcd(s_tx, s_ty) * s_ty / s_step;
    }
    s_x0 = L + (W - s_w - s_tx) * 0.5f;
    s_y0 = (480 - s_h - s_ty) * 0.5f;
    /* out of the top-left corner, a little way along */
    s_px = s_py = 40 * s_step < s_tx && 40 * s_step < s_ty ? 40 * s_step : 0;
    s_vx = s_vy = s_step;
    s_flash = 0;
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

void Screensaver::setDir(const std::string& appDir)
{
    s_dir = appDir;
    s_picFailed.clear();
    /* "Your Own" from before a picture could be chosen: the first there is */
    if (s_logoKind == LogoOwn && s_pic.empty()) {
        const std::vector<std::string> all = pictures();
        if (!all.empty()) s_pic = all[0];
    }
    s_picThere = fileThere(s_dir.empty() ? "" : s_dir + s_pic);
}

void Screensaver::setMinutes(int m) { s_minutes = m < 0 ? 0 : m; s_lastActive = nowMs(); }
int  Screensaver::minutes() { return s_minutes; }

void Screensaver::setLogo(int l) { s_logoKind = l >= 0 && l < LOGO_COUNT ? l : LogoWiiFin; }
int  Screensaver::logo() { return s_logoKind; }

void Screensaver::setPicture(const std::string& rel)
{
    if (rel != s_pic) freePicture();
    s_pic = rel;
    s_picFailed.clear();
    s_picThere = fileThere(s_dir.empty() ? "" : s_dir + s_pic);
}
const std::string& Screensaver::picture() { return s_pic; }

std::vector<std::string> Screensaver::pictures()
{
    std::vector<std::string> out;
    if (s_dir.empty()) return out;
    for (const char* name : { "screensaver.png", "screensaver.jpg", "screensaver.jpeg" })
        if (fileThere(s_dir + name)) out.push_back(name);
    for (const std::string& f : Avatars::files())      /* "file:NAME" */
        out.push_back("pictures/" + f.substr(5));
    return out;
}

std::string Screensaver::pictureLabel()
{
    std::string n = s_pic.substr(s_pic.rfind('/') == std::string::npos ? 0 : s_pic.rfind('/') + 1);
    const size_t dot = n.rfind('.');
    return dot == std::string::npos ? n : n.substr(0, dot);
}

void Screensaver::nextLogo()
{
    if (s_logoKind == LogoWiiFin) { s_logoKind = LogoDvd; return; }
    const std::vector<std::string> all = pictures();
    if (s_logoKind == LogoDvd) {
        /* none there: "Picture" all the same, saying where to put one */
        s_logoKind = LogoOwn;
        setPicture(all.empty() ? std::string() : all[0]);
    } else {
        size_t at = all.size();
        for (size_t i = 0; i < all.size(); ++i) if (all[i] == s_pic) at = i;
        if (at + 1 >= all.size()) { s_logoKind = LogoWiiFin; return; }
        setPicture(all[at + 1]);
    }
    ownPicture();   /* loaded now (while choosing), not when the screen goes dark */
}

bool Screensaver::ownLogoFound() { return !s_pic.empty() && (s_own ? true : s_picThere && s_picFailed != s_pic); }

void Screensaver::setSpeed(int sp) { s_speed = sp >= 0 && sp < SPEED_COUNT ? sp : SpeedNormal; }
int  Screensaver::speed() { return s_speed; }
void Screensaver::setEffect(int e) { s_effect = e >= 0 && e < EFFECT_COUNT ? e : EffectEdge; }
int  Screensaver::effect() { return s_effect; }

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
    ++s_fade;
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
    const int k = s_kindNow;
    const u32 c = k == LogoDvd ? colourOf(SOLID, N_SOLID) : colourOf(TINTS, N_TINTS);
    /* the corner: a glow in its colour, fading over a second and a half
     * (one soft swell: nothing blinks) */
    if (s_flash > 0) {
        const float a = s_flash / 90.0f;
        Ui::shadow(x, y, s_w, s_h, 16, 26.0f, Ui::alpha(k == LogoDvd || s_effect == EffectNone ? c : colourOf(SOLID, N_SOLID), 0.7f * a));
        --s_flash;
    }
    if (k == LogoDvd) {
        drawDvd(x, y, c);
        return;
    }
    GRRLIB_texImg* tex = k == LogoOwn ? s_own : s_wiifin;
    if (!tex) return;
    const float kx = s_w / tex->w, ky = s_h / tex->h;
    GRRLIB_DrawImg(x, y, tex, 0, kx, ky, (c & 0xFFFFFF00) | (s_effect == EffectNone ? 0xFF : 0xC8));
}
