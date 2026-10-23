#pragma once
/* The screensaver: after some minutes with nothing pressed or pointed (a
 * video or song paused counts, one playing does not) the screen goes dark
 * and the WiiFin logo drifts across it, changing colour at each edge.  Any
 * button, key, the mouse or the pointer moving brings the screen back; that
 * press does nothing else. */
#include <string>
#include <vector>

namespace Screensaver {

/* minutes before it starts, 0: never (Settings > Screensaver) */
void setMinutes(int m);
int  minutes();
/* the next and previous choice: Off, 1, 3, 5, 10, 15, 30 */
int  nextMinutes(int m, int dir);

/* what drifts: the WiiFin logo, a DVD-player-style one, or a picture of
 * one's own (none found: WiiFin's).  It moves in whole steps, so now and
 * then it lands right in a corner (every few minutes) and glows */
enum Logo { LogoWiiFin = 0, LogoDvd = 1, LogoOwn = 2, LOGO_COUNT };
void setLogo(int l);
int  logo();
/* the next choice: WiiFin, DVD, then each picture in turn */
void nextLogo();

/* the picture: "screensaver.png" (or .jpg) in WiiFin's folder, or
 * "pictures/NAME" (the profile pictures' folder).  It is shrunk to 256
 * pixels when it loads, so it costs little to keep */
void setPicture(const std::string& rel);
const std::string& picture();
/* the ones there are (read again each time) */
std::vector<std::string> pictures();
/* the picture's name without its folder and extension */
std::string pictureLabel();
/* the picture chosen loaded (or loadable) */
bool ownLogoFound();

/* how fast it drifts: 1, 2 or 4 pixels a frame */
enum Speed { SpeedSlow = 0, SpeedNormal = 1, SpeedFast = 2, SPEED_COUNT };
void setSpeed(int s);
int  speed();

/* its colours: a new one at each edge, slowly fading from one to the next,
 * or its own (the DVD logo white).  None ever flash */
enum Effect { EffectEdge = 0, EffectFade = 1, EffectNone = 2, EFFECT_COUNT };
void setEffect(int e);
int  effect();

/* WiiFin's folder (with its '/') */
void setDir(const std::string& appDir);

/* each Input::update: something done or not.  True when that woke the
 * screen: the press is eaten */
bool input(bool activity);
bool active();

/* each frame, last (RenderHook) */
void draw();

} // namespace Screensaver
