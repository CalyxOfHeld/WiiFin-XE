#include "Screensaver.h"
#include "../ui/Ui.h"
#include "../player/WiiPlayer.h"
#include <grrlib.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>

extern unsigned char data_logo_wiifin_png[];

namespace {

int   s_minutes = 3;
u64   s_lastActive = 0;          /* ms */
bool  s_on = false;

GRRLIB_texImg* s_logo = nullptr;
float s_x = 0, s_y = 0, s_dx = 1.3f, s_dy = 1.0f;
int   s_tint = 0;

/* the logo's colour, a new one at each edge (multiplied: its own colours
 * shifted toward each) */
const u32 TINTS[] = { 0xFFFFFFFF, 0xFF9C9CFF, 0x9CFFA8FF, 0x9CC8FFFF, 0xFFE08CFF, 0xE0A0FFFF, 0x8CFFF0FF };
const int N_TINTS = sizeof(TINTS) / sizeof(TINTS[0]);
const float LOGO_W = 220.0f;

u64 nowMs() { return ticks_to_millisecs(gettime()); }

}

void Screensaver::setMinutes(int m) { s_minutes = m < 0 ? 0 : m; s_lastActive = nowMs(); }
int  Screensaver::minutes() { return s_minutes; }

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
            SYS_Report("[Screensaver] off\n");
            return activity;
        }
        return false;
    }
    if (!s_on && s_minutes > 0 && now - s_lastActive >= (u64)s_minutes * 60000) {
        s_on = true;
        /* somewhere along the top, going down and across */
        s_x = Ui::screenLeft() + (Ui::screenWidth() - LOGO_W) * 0.3f;
        s_y = 60;
        SYS_Report("[Screensaver] on after %d min\n", s_minutes);
    }
    return false;
}

void Screensaver::draw()
{
    if (!s_on) return;
    if (!s_logo) s_logo = GRRLIB_LoadTexture(data_logo_wiifin_png);
    const float L = Ui::screenLeft(), R = Ui::screenRight();
    GRRLIB_Rectangle(L - 2, -2, R - L + 4, 484, 0x000000FF, 1);
    if (!s_logo) return;
    const float k = LOGO_W / s_logo->w, h = s_logo->h * k;
    s_x += s_dx;
    s_y += s_dy;
    bool hit = false;
    if (s_x < L)              { s_x = L;              s_dx = -s_dx; hit = true; }
    if (s_x + LOGO_W > R)     { s_x = R - LOGO_W;     s_dx = -s_dx; hit = true; }
    if (s_y < 0)              { s_y = 0;              s_dy = -s_dy; hit = true; }
    if (s_y + h > 480)        { s_y = 480 - h;        s_dy = -s_dy; hit = true; }
    if (hit) s_tint = (s_tint + 1) % N_TINTS;
    GRRLIB_DrawImg(s_x, s_y, s_logo, 0, k, k, (TINTS[s_tint] & 0xFFFFFF00) | 0xC8);
}
