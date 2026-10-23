#pragma once
/* The screensaver: after some minutes with nothing pressed or pointed (a
 * video or song paused counts, one playing does not) the screen goes dark
 * and the WiiFin logo drifts across it, changing colour at each edge.  Any
 * button, key, the mouse or the pointer moving brings the screen back; that
 * press does nothing else. */
namespace Screensaver {

/* minutes before it starts, 0: never (Settings > Screensaver) */
void setMinutes(int m);
int  minutes();
/* the next and previous choice: Off, 1, 3, 5, 10, 15, 30 */
int  nextMinutes(int m, int dir);

/* each Input::update: something done or not.  True when that woke the
 * screen: the press is eaten */
bool input(bool activity);
bool active();

/* each frame, last (RenderHook) */
void draw();

} // namespace Screensaver
