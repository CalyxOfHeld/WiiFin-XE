#pragma once
#include <grrlib.h>
#include <wiiuse/wpad.h>
#include <string>
#include <vector>
#include "../jellyfin/JellyfinClient.h"
#include "Keyboard.h"
#include "PinPad.h"
#include "ThemeEditor.h"

struct SavedProfile;

class SettingsView {
public:
    /* profile: the one signed in (its own look, how it signs in); null
     * from WiiFin's menu (WiiFin's own look, the system) */
    SettingsView(GRRLIB_texImg* btn, GRRLIB_ttfFont* font, JellyfinClient& client, bool& musicEnabled,
                 SavedProfile* profile = nullptr);

    // Returns true when the user exits (B or back)
    bool update(ir_t& ir);
    void render(ir_t& ir);

    // Opens the screen-area calibration directly (first-launch offer).
    void startCalibration() { calibrating = true; calCorner = 0; }
    bool isCalibrating() const { return calibrating; }

    /* Display Libraries chosen: the settings closed for the profile's
     * library list, and opened again when it closes */
    bool wantsLibraries = false;

private:
    GRRLIB_texImg*  btnTex;
    GRRLIB_ttfFont* font;
    JellyfinClient& client;
    bool&           musicEnabled;
    SavedProfile*   profile;

    /* One screen: the sections on the left (Search, View, Playback,
     * System, Sign-In), the open one's settings listed on the right */
    std::vector<int> sections;    /* those offered (Sign-In: a profile's) */
    int   section   = 1;          /* the one open (Section in the .cpp) */
    int   sideSel   = 1;          /* place in sections */
    bool  sideFocus = true;       /* the highlight on the sections, else on the settings */
    float sideAnim[5] = {};
    std::vector<int> rows;        /* the open section's settings (Row in the .cpp) */
    int   selectedIndex = 0;      /* place in rows */
    int   top           = 0;      /* first row shown */
    float focusAnim[32] = {};     /* one per place in rows */
    bool  irMode        = false;
    /* what the pointer was on: it takes the highlight when that changes */
    int   lastHoverSide = -1, lastHoverRow = -1;
    void openSection(int s);
    std::vector<int> sectionRows(int s) const;   /* every setting, for the search */
    std::vector<int> shownRows(int s) const;     /* group headers, and the settings of the open ones */
    float rowHeight(int i) const;
    float rowStep(int i) const;
    bool  fits(int from, int to) const;
    int  visibleRows() const;                    /* from top */
    float rowsTop() const;
    void follow();

    /* Search: every setting whose name or description has what is typed */
    Keyboard    kb;
    bool        typing = false;
    std::string query;
    void runSearch();

    /* what a setting shows: its name, what it does, its value */
    struct RowInfo { std::string label, desc, value; u32 col = 0; };
    RowInfo describe(int id);

    /* the Sign-In row's choice, applied on leaving the settings (a new
     * passcode asked then) */
    int    signInWant = -1;
    bool   leaveSignIn();
    /* a new passcode, typed twice */
    PinPad pin;
    int    pinStep = 0;           /* 0 none, 1 first time, 2 again */
    std::string pinFirst;

    /* Customize Theme: the colours, over the settings */
    ThemeEditor editor;
    bool        editing = false;

    /* Screen-area (overscan) calibration */
    bool  calibrating   = false;
    int   calCorner     = 0;      /* 0 top-left, 1 bottom-right */
    u64   calHeldSince  = 0;      /* d-pad auto-repeat */
    u64   calLastStep   = 0;
    /* 1: the area saved under a name (a screen of one's own) */
    bool        calNaming = false;
    std::string calName;
    Keyboard    calKb;

    void activate(int id);
    void updateCalibration(ir_t& ir);
    void renderCalibration(ir_t& ir);
};
