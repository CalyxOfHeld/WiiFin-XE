/* What every frame shows on top, whatever the screen: linked with
 * --wrap=GRRLIB_Render (Makefile), each GRRLIB_Render comes here first.
 * The server's messages (Remote) and the test build's frame counters
 * (tools/test/tour.sh sets g_wiifin_before_render) go there. */
#include <grrlib.h>
#include "../jellyfin/RemoteControl.h"
#include "../ui/Ui.h"
#include "Text.h"
#include "MusicSession.h"
#include "Screensaver.h"
#include "AppInternal.h"

extern "C" {
void (*g_wiifin_before_render)(void) = nullptr;
void __real_GRRLIB_Render(void);

void __wrap_GRRLIB_Render(void)
{
    if (g_app_shuttingDown) {
        __real_GRRLIB_Render();
        return;
    }
    Remote::pump();
    MusicSession::pump();
    Ui::drawNotice();
    if (g_wiifin_before_render) g_wiifin_before_render();
    Screensaver::draw();   /* over everything */
    __real_GRRLIB_Render();
    Ui::frameShown();
    Text::endFrame();
}
}
