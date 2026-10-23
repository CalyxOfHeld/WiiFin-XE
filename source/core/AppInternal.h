#pragma once
/* Shared between App.cpp, PlaySession.cpp and HomeMenu.cpp. */
#include <grrlib.h>
#include <wiiuse/wpad.h>
#include <string>
#include "../jellyfin/JellyfinClient.h"

class LibraryView;

extern volatile bool g_app_powerOff;   // power button
extern volatile bool g_app_reset;      // reset button
extern volatile bool g_restartApp;     // "Reset" chosen in the HOME menu

/* Where leaving WiiFin goes (the main menu's Exit, the HOME menu): back to
 * the loader that launched it (the Homebrew Channel) when true, else the
 * Wii Menu.  The setting Exit To, wiifin.cfg's exit_to_loader */
extern bool g_app_exitToLoader;
/* launched by a loader (its stub at 0x80001800), not as the WAD's channel,
 * which always goes back to the Wii Menu */
bool appFromLoader();
/* the profile WiiFin opens when it starts (its userId), empty: the menu.
 * The Sign-In setting Open at Start-Up, wiifin.cfg's start_profile */
extern std::string g_app_startProfile;
/* g_app_exitToLoader, when it can be: where leaving goes */
inline bool appExitsToLoader() { return g_app_exitToLoader && appFromLoader(); }

/* Full-screen error card: title, two lines of explanation.  With retry, A
 * retries (returns true) and B goes back; without, A or B closes it. */
bool showErrorScreen(const char* title, const std::string& line1,
                     const std::string& line2, bool retry, ir_t& ir);

/* HOME menu (HomeMenu.cpp).  Returns true when the user leaves WiiFin
 * ("Wii Menu", or "Reset" which also sets g_restartApp). */
bool doShowHomeOverlay(GRRLIB_ttfFont* font, GRRLIB_texImg* btnTex,
                       GRRLIB_texImg* cursorPointerTex, bool musicEnabled);

/* Plays lv.pendingPlay* and what follows it (PlaySession.cpp).  lastItemId
 * receives the item playing at the end.  Returns true when the user left
 * WiiFin from the HOME menu. */
bool runPlaySession(JellyfinClient& client,
                    const JellyfinAuth& auth,
                    const std::string& serverUrl,
                    LibraryView& lv,
                    GRRLIB_ttfFont* font,
                    GRRLIB_texImg* btnTex,
                    GRRLIB_texImg* cursorTex,
                    GRRLIB_texImg* ringTex,
                    std::string& lastItemId);
