#include "SettingsView.h"
#include "Ui.h"
#include "LibraryDraw.h"
#include "../player/vo_wiifin.h"
#include "../player/WiiPlayer.h"
#include "../player/Subtitles.h"
#include "../player/PlayerView.h"
#include "../core/SoundFX.h"
#include "../core/Screensaver.h"
#include "../player/RemoteSpeaker.h"
#include "../core/MusicBGM.h"
#include "../core/Text.h"
#include "../core/Lang.h"
#include "../input/Input.h"
#include "../core/SavedProfile.h"
#include "../core/AppInternal.h"
#include "../version.h"
#include <stdio.h>
#include <ctype.h>
#include <ogc/lwp_watchdog.h>

/* The rows, by what they set (describe and activate take these) */
enum Row { R_SSL, R_MUSIC, R_SOUNDS, R_QUALITY, R_DIRECT, R_SMOOTH, R_THEME, R_HOME, R_VIEW,
           R_LIGHT, R_AREA, R_BACKGROUNDS, R_CLOCK, R_LANGUAGE, R_EXIT, R_START, R_DIM, R_HEADER, R_SIGNIN, R_STARTUP, R_LIBS, R_CUSTOMIZE,
           R_SAVER, R_SPEAKER, R_SAVER_LOGO, R_POWER_BTN, R_RESET_BTN,
           R_SAVER_SPEED, R_SAVER_EFFECT, R_KB_SIZE,
           R_SUBTITLES, R_SUB_LANG,
           R_PAD_SCRUB, R_PAD_VOLUME, R_EPISODE_BTNS, R_ASK_STOP };

/* The sections, on the left: a search through all of them, then the look
 * (a profile's own; WiiFin's menu: the start-up one), playback, what
 * every profile shares, how a profile signs in */
enum Section { S_SEARCH, S_VIEW, S_PLAYBACK, S_SYSTEM, S_SIGNIN, S_COUNT };
static const char* const SECTIONS[S_COUNT] = {
    N_("Search"), N_("View"), N_("Playback"), N_("System"), N_("Sign-In")
};

/* Layout: the sections in a column on the left, the settings on the
 * right (to the screen's edge in 16:9), what the highlighted one does
 * under them */
#define SIDE_X (Ui::screenLeft() + 28.0f)
static const float SIDE_W = 150, LIST_Y = 66, ROW_H = 40, ROW_STEP = 46;
#define PANE_X (SIDE_X + SIDE_W + 16.0f)
#define PANE_W (Ui::screenRight() - 28.0f - PANE_X)
static const float FIELD_H = 34;              /* the search's text, over its matches */
static const float DESC_Y = 352, DESC_H = 62; /* the highlighted setting, said in full */

SettingsView::SettingsView(GRRLIB_texImg* btn, GRRLIB_ttfFont* f, JellyfinClient& c, bool& music,
                           SavedProfile* prof)
    : btnTex(btn), font(f), client(c), musicEnabled(music), profile(prof)
{
    sections = { S_SEARCH, S_VIEW, S_PLAYBACK, S_SYSTEM };
    if (profile) sections.push_back(S_SIGNIN);
    signInWant = profile ? profile->signIn : -1;
    kb.setEnterLabel(N_("Done"));
    calKb.setEnterLabel(N_("Save"));
    openSection(S_VIEW);
}

/* A section's settings in groups, each under a header that A folds and
 * unfolds (all open at first; folded stays so while WiiFin runs).  In
 * rows, a header is H_BASE + its group. */
enum Group { G_THEME, G_HOME, G_SCREEN, G_VIDEO, G_SUBTITLES, G_CONTROLLER, G_WII, G_SOUND, G_SAVER, G_CLOCK, G_BUTTONS, G_CONNECTION, G_COUNT };
static const char* const GROUPS[G_COUNT] = {
    /* TRANSLATORS: Settings: the headers over groups of settings */
    N_("Theme"), N_("Home and Libraries"), N_("Screen and Keyboard"), N_("Video"), N_("Subtitles"), N_("Controller"), N_("Speaker and Disc Light"),
    N_("Sound"), N_("Screensaver"), N_("Clock and Language"), N_("Buttons and Exit"), N_("Connection")
};
static const int H_BASE = 1000;
static bool s_folded[G_COUNT] = {};
struct RowGroup { int group; std::vector<int> rows; };   /* group -1: no header */

static std::vector<RowGroup> sectionGroups(int s, bool profile)
{
    switch (s) {
    case S_VIEW:
        /* the home and the libraries' look: a profile's (WiiFin's menu has neither) */
        return { { G_THEME,  { R_THEME, R_CUSTOMIZE, R_BACKGROUNDS } },
                 { G_HOME,   profile ? std::vector<int>{ R_HOME, R_VIEW, R_LIBS, R_START, R_DIM, R_HEADER }
                                     : std::vector<int>{ R_START, R_DIM, R_HEADER } },
                 { G_SCREEN, { R_AREA, R_KB_SIZE } } };
    case S_PLAYBACK:
        return { { G_VIDEO,      { R_QUALITY, R_DIRECT, R_SMOOTH } },
                 { G_SUBTITLES,  { R_SUBTITLES, R_SUB_LANG } },
                 { G_CONTROLLER, { R_PAD_SCRUB, R_PAD_VOLUME, R_EPISODE_BTNS, R_ASK_STOP } },
                 { G_WII,        { R_SPEAKER, R_LIGHT } } };
    case S_SYSTEM:
        return { { G_SOUND,      { R_MUSIC, R_SOUNDS } },
                 { G_SAVER,      { R_SAVER, R_SAVER_LOGO, R_SAVER_SPEED, R_SAVER_EFFECT } },
                 { G_CLOCK,      { R_CLOCK, R_LANGUAGE } },
                 { G_BUTTONS,    { R_EXIT, R_POWER_BTN, R_RESET_BTN } },
                 { G_CONNECTION, { R_SSL } } };
    case S_SIGNIN:
        return profile ? std::vector<RowGroup>{ { -1, { R_SIGNIN, R_STARTUP } } } : std::vector<RowGroup>{};
    default:
        return {};
    }
}

static bool isHeader(int id) { return id >= H_BASE; }

std::vector<int> SettingsView::sectionRows(int s) const
{
    std::vector<int> out;
    for (const RowGroup& g : sectionGroups(s, profile != nullptr)) out.insert(out.end(), g.rows.begin(), g.rows.end());
    return out;
}

/* what the list shows: each header, its settings unless folded */
std::vector<int> SettingsView::shownRows(int s) const
{
    std::vector<int> out;
    for (const RowGroup& g : sectionGroups(s, profile != nullptr)) {
        if (g.group >= 0) out.push_back(H_BASE + g.group);
        if (g.group < 0 || !s_folded[g.group]) out.insert(out.end(), g.rows.begin(), g.rows.end());
    }
    return out;
}

void SettingsView::openSection(int s)
{
    section = s;
    for (int i = 0; i < (int)sections.size(); ++i) if (sections[i] == s) sideSel = i;
    selectedIndex = top = 0;
    for (float& f : focusAnim) f = 0.0f;
    if (s == S_SEARCH) runSearch();
    else               rows = shownRows(s);
}

/* every setting whose name or description (as shown, translated) has the
 * query, letters in either case */
void SettingsView::runSearch()
{
    rows.clear();
    if (query.empty()) return;
    auto lower = [](std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; };
    const std::string q = lower(query);
    for (int s : sections)
        for (int id : sectionRows(s)) {
            const RowInfo r = describe(id);
            if (lower(r.label).find(q) != std::string::npos || lower(r.desc).find(q) != std::string::npos)
                rows.push_back(id);
        }
    if (selectedIndex >= (int)rows.size()) selectedIndex = rows.empty() ? 0 : (int)rows.size() - 1;
    follow();
}

float SettingsView::rowsTop() const { return section == S_SEARCH ? LIST_Y + FIELD_H + 8 : LIST_Y; }

/* a header is shorter than a setting */
static const float HDR_H = 26, HDR_STEP = 32;
float SettingsView::rowHeight(int i) const { return isHeader(rows[i]) ? HDR_H : ROW_H; }
float SettingsView::rowStep(int i) const   { return isHeader(rows[i]) ? HDR_STEP : ROW_STEP; }
/* rows from..to (both in) fit between the top and the description */
bool SettingsView::fits(int from, int to) const
{
    float y = rowsTop();
    for (int i = from; i <= to; ++i) y += rowStep(i);
    return y - (rowStep(to) - rowHeight(to)) <= DESC_Y - 6;
}
int SettingsView::visibleRows() const
{
    int k = 0;
    while (top + k < (int)rows.size() && fits(top, top + k)) ++k;
    return k > 0 ? k : 1;
}

void SettingsView::follow()
{
    const int n = (int)rows.size();
    if (selectedIndex < top) top = selectedIndex;
    while (top < selectedIndex && !fits(top, selectedIndex)) ++top;
    while (top > 0 && n > 0 && fits(top - 1, n - 1)) --top;   /* no room left empty at the end */
    if (top < 0) top = 0;
}

/* Leaving the settings: the Sign-In row's choice applied; a passcode new
 * to the profile is typed first (twice).  False: the pad is up */
bool SettingsView::leaveSignIn() {
    if (!profile || signInWant < 0 || signInWant == profile->signIn) return true;
    if (signInWant == SavedProfile::Passcode) {
        pinStep = 1;
        pin.start(N_("Choose a passcode"), N_("Four digits, asked when this profile opens"));
        return false;
    }
    profile->signIn = signInWant;
    SYS_Report("[Settings] sign-in %d\n", profile->signIn);
    return true;
}

SettingsView::RowInfo SettingsView::describe(int id)
{
    const Ui::Palette& p = Ui::pal();
    const u32 OFF = 0x8A9099FF;
    RowInfo r;
    auto set = [&](const char* label, const char* desc, const char* value, u32 col) {
        r.label = tr(label); r.desc = tr(desc); r.value = tr(value); r.col = col;
    };
    switch (id) {
    case R_SSL:
        set(N_("SSL Verification"), N_("Validate the server HTTPS certificate (disable for self-signed certs)"),
            client.sslVerify ? N_("ON") : N_("OFF"), client.sslVerify ? p.ok : OFF);
        break;
    case R_MUSIC:
        set(N_("Background Music"), N_("Play background music in menus"),
            musicEnabled ? N_("ON") : N_("OFF"), musicEnabled ? p.ok : OFF);
        break;
    case R_SOUNDS:
        set(N_("Interface Sounds"),
            SoundFX::enabled() ? N_("Sounds in the menus; your own in sounds/ on the SD card")
                               : N_("The menus stay silent"),
            SoundFX::enabled() ? N_("ON") : N_("OFF"), SoundFX::enabled() ? p.ok : OFF);
        break;
    case R_QUALITY: {
        static const u32 qualityCol[JellyfinClient::VIDEO_QUALITY_COUNT] =
            { 0xD98A2EFF, 0x34A8DDFF, 0x3DAF5AFF, 0x9A5BD6FF };
        /* the chosen bitrate, and what the connection measured at the first
         * video holds it to (it is not the setting changing on its own) */
        char desc[160];
        const int q = client.videoQuality;
        if (client.measuredKbps() > 0 && client.linkQuality() < q)
            snprintf(desc, sizeof(desc),
                     tr("%.1f Mb/s; your connection (%.1f Mb/s measured) holds videos to %.1f"),
                     JellyfinClient::qualityBitrate(q) / 1000000.0, client.measuredKbps() / 1000.0,
                     client.videoBitrate() / 1000000.0);
        else if (q == JellyfinClient::VIDEO_QUALITY_COUNT - 1)
            snprintf(desc, sizeof(desc),
                     tr("%.1f Mb/s: sharpest, for a wired adapter. Lower it if videos stutter"),
                     JellyfinClient::qualityBitrate(q) / 1000000.0);
        else
            snprintf(desc, sizeof(desc),
                     tr("Video bitrate: %.1f Mb/s. Lower it if playback keeps buffering"),
                     JellyfinClient::qualityBitrate(q) / 1000000.0);
        r.label = tr("Video Quality");
        r.desc  = desc;
        r.value = tr(JellyfinClient::videoQualityName(q));
        r.col   = qualityCol[q];
        break;
    }
    case R_DIRECT:
        set(N_("Direct Play"),
            client.directPlay ? N_("Files the Wii can decode play as they are (SD DivX, DVD...): no server work")
                              : N_("Every video is converted by the server"),
            client.directPlay ? N_("Auto") : N_("OFF"), client.directPlay ? p.ok : OFF);
        break;
    case R_SMOOTH:
        set(N_("Smooth Motion"),
            g_wiifin_smooth_motion ? N_("Blends frames on picture changes: films move evenly on 60 Hz TVs")
                                   : N_("Each frame shown as is (24 fps films judder slightly on 60 Hz)"),
            g_wiifin_smooth_motion ? N_("ON") : N_("OFF"), g_wiifin_smooth_motion ? p.ok : OFF);
        break;
    case R_SUBTITLES:
        set(N_("Subtitles"),
            Subtitles::defaultEnabled() ? N_("Subtitles will always be on by default for every video")
                                        : N_("Subtitles will start off by default for every video"),
            Subtitles::defaultEnabled() ? N_("ON") : N_("OFF"),
            Subtitles::defaultEnabled() ? p.ok : OFF);
        break;
    case R_SUB_LANG: {
        const int idx = Subtitles::preferredLanguageIndex();
        set(N_("Subtitle Language"),
            idx == 0 ? N_("The track chosen by Jellyfin, if subtitles are on")
                     : N_("Tries to choose subtitles in this language when available"),
            Subtitles::languageName(idx),
            idx == 0 ? OFF : p.accent);
        break;
    }
    case R_PAD_SCRUB: {
        static const char* const scrubNames[] = { N_("C-Stick"), N_("Main Stick"), N_("D-Pad"), N_("OFF") };
        static const char* const scrubDescs[] = {
            N_("Left and right on C-Stick / right stick jumps through video"),
            N_("Left and right on the main analog stick jumps through video"),
            N_("Left and right on the D-Pad jumps through video"),
            N_("Stick and D-Pad scrubbing is turned off"),
        };
        const int s = (int)Input::padScrub();
        set(N_("Scrub Control"), scrubDescs[s], scrubNames[s], s == 3 ? OFF : p.accent);
        break;
    }
    case R_PAD_VOLUME: {
        static const char* const volNames[] = { N_("D-Pad"), N_("Main Stick"), N_("C-Stick"), N_("OFF") };
        static const char* const volDescs[] = {
            N_("Up and down on the D-Pad turns volume up and down"),
            N_("Up and down on the main analog stick turns volume up and down"),
            N_("Up and down on C-Stick / right stick turns volume up and down"),
            N_("Stick and D-Pad volume control is turned off"),
        };
        const int v = (int)Input::padVolume();
        set(N_("Volume Control"), volDescs[v], volNames[v], v == 3 ? OFF : p.accent);
        break;
    }
    case R_EPISODE_BTNS:
        set(N_("Episode Buttons"),
            PlayerView::episodeButtons ? N_("+ and - (and |< >| after the last chapter) go to the next or previous episode")
                                       : N_("+ and - do nothing; |< >| only go through the chapters"),
            PlayerView::episodeButtons ? N_("ON") : N_("OFF"), PlayerView::episodeButtons ? p.ok : OFF);
        break;
    case R_ASK_STOP:
        set(N_("Ask Before Stopping"),
            PlayerView::askBeforeStop ? N_("B asks before the video stops")
                                      : N_("B stops the video at once"),
            PlayerView::askBeforeStop ? N_("ON") : N_("OFF"), PlayerView::askBeforeStop ? p.ok : OFF);
        break;
    case R_THEME: {
        static const char* themeDesc[Ui::THEME_COUNT] = {
            N_("Light, like the Wii Menu"),
            N_("Dark, for movie nights"),
            N_("Red and charcoal, browse in rows like the old video channels"),
            N_("Black, silver and green"),
            N_("Dark grey and orange"),
        };
        static const u32 themeCol[Ui::THEME_COUNT] = { 0x34A8DDFF, 0x45546BFF, 0xD81F26FF, 0x7DC242FF, 0xF47521FF };
        if (Ui::customTheme() >= 0) {   /* one of the files in themes/ */
            char d[96];
            snprintf(d, sizeof(d), tr("Your own, from themes/%s"), Ui::customThemes()[Ui::customTheme()].file.c_str());
            r.label = tr("Theme"); r.desc = d;
            r.value = Ui::customThemes()[Ui::customTheme()].name;
            r.col   = Ui::pal().accent | 0xFF;
            break;
        }
        const int t = (int)Ui::theme();
        set(N_("Theme"), themeDesc[t], Ui::themeName(Ui::theme()), themeCol[t]);
        break;
    }
    case R_CUSTOMIZE:
        set(N_("Customize Theme"), N_("Change any colour, text included, and save it as a theme of your own"),
            N_("Edit"), p.accent);
        break;
    case R_HOME: {
        const bool rowsHome = Ui::homeLayout() == Ui::HomeLayout::Rows;
        set(N_("Home Screen"), rowsHome ? N_("Rows of posters to browse, like the old video channels")
                                        : N_("A grid of your libraries"),
            rowsHome ? N_("Rows") : N_("Grid"), rowsHome ? p.accent : OFF);
        break;
    }
    case R_VIEW: {
        static const char* name[Ui::LIBRARY_STYLE_COUNT] = { N_("Posters"), N_("List"), N_("List + Posters") };
        static const char* desc[Ui::LIBRARY_STYLE_COUNT] = {
            N_("Browse libraries with their artwork"),
            N_("Libraries as text lists: quickest to browse big ones"),
            N_("Text lists, with the cover of the selected title"),
        };
        const int v = (int)Ui::libraryStyle();
        set(N_("Library View"), desc[v], name[v], v == 0 ? OFF : p.accent);
        break;
    }
    case R_LIGHT:
        set(N_("Disc Slot Light"),
            g_wiifin_disc_light ? N_("Pulses with the sound, as bright as the Wii Settings say")
                                : N_("Stays dark during playback"),
            g_wiifin_disc_light ? N_("ON") : N_("OFF"), g_wiifin_disc_light ? p.ok : OFF);
        break;
    case R_AREA: {
        int l, t, rr, b;
        Ui::safeArea(l, t, rr, b);
        const bool full = !(l | t | rr | b);
        int np;
        const Ui::ScreenPreset* ps = Ui::screenPresets(np);
        const int snap = Ui::screenPreset(), mine = Ui::savedScreen();
        set(N_("Screen Area"), N_("Shrink the interface if your TV crops the edges (overscan); save one per TV"),
            full ? N_("Full") : snap >= 0 ? ps[snap].name : N_("Adjusted"), full ? OFF : p.accent);
        if (mine >= 0) r.value = Ui::savedScreens()[mine].name;
        break;
    }
    case R_BACKGROUNDS:
        set(N_("Backgrounds"),
            Ui::solidColors() ? N_("Solid colours: no bands on TVs that show them in gradients")
                              : N_("Soft gradients, like the Wii Menu"),
            Ui::solidColors() ? N_("Solid") : N_("Gradient"), Ui::solidColors() ? p.accent : OFF);
        break;
    case R_SAVER: {
        const int m = Screensaver::minutes();
        static char v[24];
        /* TRANSLATORS: the screensaver starts after this many minutes */
        if (m > 0) snprintf(v, sizeof v, tr("%d min"), m);
        set(N_("Screensaver"),
            m > 0 ? N_("A logo drifts on a dark screen after a while untouched (paused too)")
                  : N_("The screen stays as it is, however long it's left"),
            m > 0 ? "" : N_("Off"), m > 0 ? p.accent : OFF);
        if (m > 0) r.value = v;
        break;
    }
    case R_POWER_BTN:
    case R_RESET_BTN: {
        const bool power = id == R_POWER_BTN;
        const int a = power ? g_app_powerAction : g_app_resetAction;
        set(power ? N_("Power Button") : N_("Reset Button"),
            a == BTN_POWER_OFF ? N_("Turns the Wii off (or to standby, as the Wii Settings say)")
            : a == BTN_HBC     ? N_("Goes back to the Homebrew Channel")
            : a == BTN_WIIMC   ? N_("Opens WiiMC-SS XE (the Homebrew Channel if it isn't there)")
                               : N_("Goes back to the Wii Menu"),
            buttonActionName(a), a == (power ? BTN_POWER_OFF : BTN_HBC) ? OFF : p.accent);
        break;
    }
    case R_SAVER_LOGO: {
        const int l = Screensaver::logo();
        set(N_("Screensaver Logo"),
            l == Screensaver::LogoDvd ? N_("A DVD-player-style logo")
            : l == Screensaver::LogoVhs ? N_("A VHS-tape-style logo")
            : l == Screensaver::LogoCd  ? N_("A Compact-Disc-style logo")
            : l == Screensaver::LogoOwn
                ? (Screensaver::picture().empty()
                       ? N_("Put a PNG or JPEG in apps/WiiFin/pictures/; until then, the WiiFin logo")
                   : Screensaver::ownLogoFound()
                       ? N_("Your picture, from apps/WiiFin/ (A: the next one)")
                       : N_("That picture can't be used: a JPEG, or a PNG up to 1600 pixels, under 3 MB"))
                : N_("The WiiFin logo"),
            l == Screensaver::LogoDvd ? N_("DVD") : l == Screensaver::LogoVhs ? N_("VHS")
            : l == Screensaver::LogoCd ? N_("CD") : l == Screensaver::LogoOwn ? N_("Picture") : N_("WiiFin"),
            l ? p.accent : OFF);
        if (l == Screensaver::LogoOwn && !Screensaver::picture().empty()) r.value = Screensaver::pictureLabel();
        break;
    }
    case R_KB_SIZE: {
        const int k = Keyboard::size();
        set(N_("Keyboard Size"),
            k == Keyboard::SizeLarger ? N_("The on-screen keyboard at its biggest, keys and letters")
            : k == Keyboard::SizeLarge ? N_("The on-screen keyboard a little bigger, keys and letters")
                                       : N_("The on-screen keyboard as it comes"),
            k == Keyboard::SizeLarger ? N_("Larger") : k == Keyboard::SizeLarge ? N_("Large") : N_("Normal"),
            k ? p.accent : OFF);
        break;
    }
    case R_SAVER_SPEED: {
        const int sp = Screensaver::speed();
        set(N_("Screensaver Speed"),
            sp == Screensaver::SpeedSlow ? N_("Drifts slowly; a corner comes about every ten minutes")
            : sp == Screensaver::SpeedFast ? N_("Drifts quickly; a corner comes every few minutes")
                                           : N_("Drifts steadily; a corner comes every five minutes or so"),
            sp == Screensaver::SpeedSlow ? N_("Slow") : sp == Screensaver::SpeedFast ? N_("Fast") : N_("Normal"),
            sp == Screensaver::SpeedNormal ? OFF : p.accent);
        break;
    }
    case R_SAVER_EFFECT: {
        const int e = Screensaver::effect();
        set(N_("Screensaver Colours"),
            e == Screensaver::EffectFade ? N_("The colour fades slowly from one to the next")
            : e == Screensaver::EffectNone ? N_("Its own colours, always (the DVD logo white)")
                                           : N_("A new colour each time it touches an edge"),
            e == Screensaver::EffectFade ? N_("Fade") : e == Screensaver::EffectNone ? N_("Off") : N_("Each Edge"),
            e == Screensaver::EffectEdge ? OFF : p.accent);
        break;
    }
    case R_SPEAKER: {
        const int m = RemoteSpeaker::mode();
        set(N_("Wii Remote Speaker"),
            m == RemoteSpeaker::RemoteOnly ? N_("Videos and music play on the Wii Remote's speaker only, not the TV")
            : m == RemoteSpeaker::Both     ? N_("Videos and music play on the TV and the Wii Remote's speaker")
                                           : N_("Sound from the TV only"),
            m == RemoteSpeaker::RemoteOnly ? N_("Wii Remote") : m == RemoteSpeaker::Both ? N_("TV + Remote") : N_("Off"),
            m ? p.accent : OFF);
        break;
    }
    case R_CLOCK:
        set(N_("Clock"),
            Ui::clock12h() ? N_("1:30 PM, the date as month/day") : N_("13:30, the date as day/month"),
            Ui::clock12h() ? N_("12-hour") : N_("24-hour"), p.accent);
        break;
    case R_LANGUAGE:
        /* each language by its own name */
        set(N_("Language"),
            Lang::choice() < 0 ? N_("The Wii's language, as set in the Wii Settings") : N_("Chosen here, whatever the Wii's language"),
            "", Lang::choice() < 0 ? OFF : p.accent);
        r.value = Lang::name(Lang::current());
        break;
    case R_EXIT: {
        const bool toLoader = appExitsToLoader();
        set(N_("Exit To"),
            !appFromLoader() ? N_("Started as a channel: leaving goes to the Wii Menu")
            : toLoader       ? N_("Exit and the HOME menu go back to the Homebrew Channel")
                             : N_("Exit and the HOME menu go to the Wii Menu"),
            toLoader ? N_("HBC") : N_("Wii Menu"), toLoader ? p.accent : OFF);
        break;
    }
    case R_START:
        set(N_("Start Screen"),
            g_app_startBrowse ? N_("The rows home opens on browse: your libraries")
                              : N_("The rows home opens on home: Continue Watching"),
            g_app_startBrowse ? N_("Browse") : N_("Home"), g_app_startBrowse ? OFF : p.accent);
        break;
    case R_DIM:
        set(N_("Other Rows"),
            Ui::dimRows() ? N_("The rows home shades the rows under the one you're on")
                          : N_("The rows home shows every row plainly"),
            Ui::dimRows() ? N_("Shaded") : N_("Shown"), Ui::dimRows() ? OFF : p.accent);
        break;
    case R_HEADER:
        set(N_("Header Buttons"),
            Ui::classicHeader() ? N_("The rows home's header: search, a button to the other page, and your profile's circle for the menu")
                                : N_("The rows home's header: search, home, browse and menu buttons, each with its icon"),
            Ui::classicHeader() ? N_("Classic") : N_("Icons"), Ui::classicHeader() ? p.accent : OFF);
        break;
    case R_SIGNIN: {
        static const char* const how[3]  = { N_("Automatic"), N_("Passcode"), N_("Password") };
        static const char* const desc[3] = {
            N_("Opens at once on this Wii"),
            N_("Asks the profile's four-digit passcode first"),
            N_("Asks the password (or Quick Connect) each time"),
        };
        const int m = signInWant >= 0 && signInWant < 3 ? signInWant : 0;
        set(N_("Sign-In"), desc[m], how[m], m == 0 ? OFF : p.accent);
        break;
    }
    case R_LIBS: {
        /* how many the profile leaves off its home */
        int hidden = 0;
        if (profile && !profile->hiddenLibs.empty()) {
            hidden = 1;
            for (char c : profile->hiddenLibs) if (c == ',') ++hidden;
        }
        set(N_("Display Libraries"), N_("Choose which libraries show on your home; hidden ones can still be opened here"),
            hidden ? "" : N_("All"), hidden ? p.accent : OFF);
        if (hidden) {
            char v[32];
            /* TRANSLATORS: Display Libraries: how many are left off the home */
            snprintf(v, sizeof(v), tr("%d hidden"), hidden);
            r.value = v;
        }
        break;
    }
    case R_STARTUP: {
        const bool on = profile && !profile->userId.empty() && g_app_startProfile == profile->userId;
        set(N_("Open at Start-Up"),
            on ? N_("Opens when WiiFin starts; hold B at start to skip")
            : g_app_startProfile.empty() ? N_("WiiFin starts at its own menu")
                                         : N_("Another profile opens at start-up: A makes it this one"),
            on ? N_("ON") : N_("OFF"), on ? p.ok : OFF);
        break;
    }
    default: break;
    }
    return r;
}

void SettingsView::activate(int index) {
    if (isHeader(index)) {   /* fold or unfold its group; the highlight stays on it */
        bool& folded = s_folded[index - H_BASE];
        folded = !folded;
        rows = shownRows(section);
        for (int i = 0; i < (int)rows.size(); ++i) if (rows[i] == index) selectedIndex = i;
        follow();
        return;
    }
    switch (index) {
        case 0: client.sslVerify = !client.sslVerify; break;
        case 1: musicEnabled = !musicEnabled; MusicBGM::setEnabled(musicEnabled); break;
        case 2: SoundFX::setEnabled(!SoundFX::enabled()); break;
        case 3: client.videoQuality = (client.videoQuality + 1) % JellyfinClient::VIDEO_QUALITY_COUNT; break;
        case 4: client.directPlay = !client.directPlay; break;
        case 5: g_wiifin_smooth_motion = !g_wiifin_smooth_motion; break;
        case 6:   /* the built-in ones, then the files in themes/ */
            Ui::setThemeSlot((Ui::themeSlot() + 1) % Ui::themeSlotCount());
            /* the Flix look is made for the rows of carousels */
            if (Ui::theme() == Ui::Theme::Flix && Ui::customTheme() < 0) Ui::setHomeLayout(Ui::HomeLayout::Rows);
            break;
        case R_CUSTOMIZE: editor.open(); editing = true; break;
        case 7:
            Ui::setHomeLayout(Ui::homeLayout() == Ui::HomeLayout::Rows ? Ui::HomeLayout::Grid
                                                                       : Ui::HomeLayout::Rows);
            break;
        case 8:
            Ui::setLibraryStyle((Ui::LibraryStyle)(((int)Ui::libraryStyle() + 1) % Ui::LIBRARY_STYLE_COUNT));
            break;
        case 9: g_wiifin_disc_light = !g_wiifin_disc_light; break;
        case 10: calibrating = true; calCorner = 0; break;
        case 11: Ui::setSolidColors(!Ui::solidColors()); break;
        case 12: Ui::setClock12h(!Ui::clock12h()); break;
        case 13:   /* the Wii's, then each language in turn */
            Lang::choose(Lang::next(Lang::choice()));
            break;
        case R_SAVER:   Screensaver::setMinutes(Screensaver::nextMinutes(Screensaver::minutes(), 1)); break;
        case R_POWER_BTN: g_app_powerAction = (g_app_powerAction + 1) % BTN_ACTION_COUNT; break;
        case R_RESET_BTN: g_app_resetAction = (g_app_resetAction + 1) % BTN_ACTION_COUNT; break;
        case R_SAVER_LOGO: Screensaver::nextLogo(); break;
        case R_KB_SIZE: Keyboard::setSize((Keyboard::size() + 1) % Keyboard::SIZE_COUNT); break;
        case R_SAVER_SPEED: Screensaver::setSpeed((Screensaver::speed() + 1) % Screensaver::SPEED_COUNT); break;
        case R_SAVER_EFFECT: Screensaver::setEffect((Screensaver::effect() + 1) % Screensaver::EFFECT_COUNT); break;
        case R_SPEAKER: RemoteSpeaker::setMode((RemoteSpeaker::mode() + 1) % RemoteSpeaker::MODE_COUNT); break;
        case R_EXIT:   /* the channel has no loader to go back to */
            if (appFromLoader()) g_app_exitToLoader = !g_app_exitToLoader;
            break;
        case R_SIGNIN:   /* at once, a passcode, the password: chosen, applied on leaving */
            if (!profile) break;
            signInWant = (signInWant + 1) % 3;
            break;
        case R_START: g_app_startBrowse = !g_app_startBrowse; break;
        case R_DIM:   Ui::setDimRows(!Ui::dimRows()); break;
        case R_HEADER: Ui::setClassicHeader(!Ui::classicHeader()); break;
        case R_LIBS: if (profile) wantsLibraries = true; break;
        case R_STARTUP:   /* this profile at start-up, or none; one at most */
            if (!profile || profile->userId.empty()) break;
            if (g_app_startProfile == profile->userId) g_app_startProfile.clear();
            else                                       g_app_startProfile = profile->userId;
            SYS_Report("[Settings] start-up profile %s\n", g_app_startProfile.empty() ? "none" : "this one");
            break;
        case R_SUBTITLES:
            Subtitles::setDefaultEnabled(!Subtitles::defaultEnabled());
            break;
        case R_SUB_LANG:
            Subtitles::nextPreferredLanguage();
            break;
        case R_PAD_SCRUB:
            Input::setPadScrub((Input::PadScrub)(((int)Input::padScrub() + 1) % 4));
            break;
        case R_PAD_VOLUME:
            Input::setPadVolume((Input::PadVolume)(((int)Input::padVolume() + 1) % 4));
            break;
        case R_EPISODE_BTNS: PlayerView::episodeButtons = !PlayerView::episodeButtons; break;
        case R_ASK_STOP:     PlayerView::askBeforeStop  = !PlayerView::askBeforeStop;  break;
    }
}

/* ---------------------------------------------------------------
 * Screen area calibration: two corner brackets, moved with the d-pad until
 * they sit in the corners of the visible picture.  The GUI follows live.
 * --------------------------------------------------------------- */
void SettingsView::updateCalibration(ir_t& ir)
{
    std::vector<Ui::SavedScreen>& saved = Ui::savedScreens();
    if (calNaming) {   /* its name typed: Done saves it (one of the same name replaced) */
        calKb.setOrigin((640 - calKb.width()) * 0.5f, 214 - calKb.lift());
        const Keyboard::Result res = calKb.update(ir, calName, 24);
        if (res == Keyboard::Result::Cancel) { calNaming = false; SoundFX::play(SoundFX::FX::Back); }
        if (res == Keyboard::Result::Enter) {
            calNaming = false;
            size_t a = calName.find_first_not_of(' '), z = calName.find_last_not_of(' ');
            const std::string name = a == std::string::npos ? "" : calName.substr(a, z - a + 1);
            if (name.empty()) { SoundFX::play(SoundFX::FX::Back); return; }
            int l, t, r, b;
            Ui::safeArea(l, t, r, b);
            Ui::SavedScreen s { name, l, t, r, b };
            bool replaced = false;
            for (Ui::SavedScreen& o : saved) if (o.name == name) { o = s; replaced = true; }
            if (!replaced) saved.push_back(s);
            SYS_Report("[Settings] screen \"%s\" saved: %d,%d,%d,%d\n", name.c_str(), l, t, r, b);
            Ui::showNotice(tr("Screen saved"), name, 2500);
            SoundFX::play(SoundFX::FX::Open);
        }
        return;
    }
    if (Input::isBackPressed()) { calibrating = false; return; }
    if (Input::isAJustPressed()) calCorner ^= 1;
    /* -/+: the snaps (the picture's shapes, margins), then the screens
     * saved, from the one it is on */
    if (Input::isLPressed() || Input::isRPressed()) {
        int n;
        const Ui::ScreenPreset* ps = Ui::screenPresets(n);
        const int total = n + (int)saved.size();
        int at = Ui::savedScreen() >= 0 ? n + Ui::savedScreen() : Ui::screenPreset();
        if (at < 0) at = Input::isRPressed() ? -1 : 0;
        at = (at + (Input::isRPressed() ? 1 : total - 1)) % total;
        if (at < n) Ui::setSafeArea(ps[at].l, ps[at].t, ps[at].r, ps[at].b);
        else {
            const Ui::SavedScreen& s = saved[at - n];
            Ui::setSafeArea(s.l, s.t, s.r, s.b);
        }
        SoundFX::play(SoundFX::FX::Move);
        return;
    }
    /* 1: saved under a name; 2: the saved one it is on forgotten */
    if (Input::is1Pressed()) {
        const int on = Ui::savedScreen();
        if (on >= 0) calName = saved[on].name;
        else {
            char def[24];
            snprintf(def, sizeof def, tr("TV %d"), (int)saved.size() + 1);
            calName = def;
        }
        calKb.reset();
        calNaming = true;
        SoundFX::play(SoundFX::FX::Select);
        return;
    }
    if (Input::is2Pressed() && Ui::savedScreen() >= 0) {
        const int on = Ui::savedScreen();
        Ui::showNotice(tr("Screen forgotten"), saved[on].name, 2500);
        saved.erase(saved.begin() + on);
        SoundFX::play(SoundFX::FX::Back);
        return;
    }

    /* Single press = 1 px; holding repeats after 350 ms, faster after 1 s */
    u32 held = Input::held();
    u32 dirs = held & (WPAD_BUTTON_UP | WPAD_BUTTON_DOWN | WPAD_BUTTON_LEFT | WPAD_BUTTON_RIGHT);
    u64 now  = ticks_to_millisecs(gettime());
    int dx = 0, dy = 0, step = 1;
    if (Input::isLeftPressed())  dx = -1;
    if (Input::isRightPressed()) dx = +1;
    if (Input::isUpPressed())    dy = -1;
    if (Input::isDownPressed())  dy = +1;
    if (dx || dy) { calHeldSince = now; calLastStep = now; }
    else if (dirs && calHeldSince && now - calHeldSince > 350 && now - calLastStep > 40) {
        calLastStep = now;
        if (held & WPAD_BUTTON_LEFT)  dx = -1;
        if (held & WPAD_BUTTON_RIGHT) dx = +1;
        if (held & WPAD_BUTTON_UP)    dy = -1;
        if (held & WPAD_BUTTON_DOWN)  dy = +1;
        if (now - calHeldSince > 1000) step = 3;
    }
    if (!dirs && !(dx || dy)) calHeldSince = 0;
    if (!dx && !dy) return;

    int l, t, r, b;
    Ui::safeArea(l, t, r, b);
    if (calCorner == 0) { l += dx * step; t += dy * step; }   /* moving the corner inward grows the margin */
    else                { r -= dx * step; b -= dy * step; }
    Ui::setSafeArea(l, t, r, b);
}

void SettingsView::renderCalibration(ir_t& ir)
{
    const Ui::Palette& p = Ui::pal();
    int l, t, r, b;
    Ui::safeArea(l, t, r, b);

    Ui::background(false);

    Ui::card(100, 120, 440, 214, 18, 0.0f);
    Ui::textCentered(320, 136, tr("Screen Area"), 22, p.text);
    Ui::textCentered(320, 168, tr("Bring the arrows in until the white edge"), 15, p.textDim);
    Ui::textCentered(320, 188, tr("shows all the way round, or snap with -/+."), 15, p.textDim);
    int np;
    const Ui::ScreenPreset* ps = Ui::screenPresets(np);
    const int snap = Ui::screenPreset(), mine = Ui::savedScreen();
    const std::string shown = mine >= 0 ? Ui::savedScreens()[mine].name
                            : snap >= 0 ? std::string(tr(ps[snap].name)) : std::string(tr("Adjusted by hand"));
    Ui::textCentered(320, 222, shown.c_str(), 18, p.accentDark);
    char vals[96];
    snprintf(vals, sizeof(vals), tr("Left %d   \xc2\xb7   Top %d   \xc2\xb7   Right %d   \xc2\xb7   Bottom %d"), l, t, r, b);
    Ui::textCentered(320, 254, vals, 14, p.textDim);
    char nSaved[64];
    snprintf(nSaved, sizeof nSaved, Ui::savedScreens().size() == 1 ? tr("%d screen saved") : tr("%d screens saved"),
             (int)Ui::savedScreens().size());
    Ui::textCentered(320, 278, tr("1 saves this area for the TV you use now;"), 13, p.textDim);
    Ui::textCentered(320, 296, (std::string(tr("-/+ bring it back later.")) + "  (" + nSaved + ")").c_str(), 13, p.textDim);

    /* bar first: the brackets must stay visible on top of it */
    const Ui::Hint left[]  = { { "UD", N_("Move") }, { "A", N_("Switch") }, { "-/+", N_("Snap") },
                               { "1", N_("Save") }, { "2", N_("Forget") } };
    static const Ui::Hint right[] = { { "B", N_("Done") } };
    Ui::bottomBar(left, mine >= 0 ? 5 : 4, right, 1);

    /* The picture's very edge, in stripes: white, red, white, red, 4 units
     * each.  The outer white one seen on all four sides: nothing is cut.
     * Fewer stripes seen on a side: that side is cropped by about 4 each. */
    const float SL = Ui::screenLeft(), SR = Ui::screenRight();
    for (int k = 0; k < 4; ++k) {
        const float in = k * 4.0f, T = 4.0f;
        const u32 c = k % 2 ? 0xE03A3AFF : 0xFFFFFFFF;
        Ui::roundRect(SL + in, in, SR - SL - 2 * in, T, 0, c);              /* top */
        Ui::roundRect(SL + in, 480 - in - T, SR - SL - 2 * in, T, 0, c);    /* bottom */
        Ui::roundRect(SL + in, in, T, 480 - 2 * in, 0, c);                  /* left */
        Ui::roundRect(SR - in - T, in, T, 480 - 2 * in, 0, c);              /* right */
    }

    /* Corner brackets inside the stripes: the whole picture moves with the
     * setting, so they show where the TV cuts it. */
    const float ARM = 46, TH = 5;
    for (int c = 0; c < 2; ++c) {
        bool on = (c == calCorner);
        u32 col = on ? Ui::mix(p.accent, 0xFFFFFFFF, 1.0f - Ui::pulse()) : p.textDim;
        float cx = c == 0 ? SL + 16 : SR - 16, cy = c == 0 ? 16.0f : 464.0f;
        float sx = c == 0 ? 1.0f : -1.0f, sy = c == 0 ? 1.0f : -1.0f;
        /* L-shaped bracket hugging the corner */
        Ui::roundRect(sx > 0 ? cx : cx - ARM, sy > 0 ? cy : cy - TH, ARM, TH, 1, col);
        Ui::roundRect(sx > 0 ? cx : cx - TH, sy > 0 ? cy : cy - ARM, TH, ARM, 1, col);
        /* arrow pointing into the corner */
        float ax = cx + sx * 26, ay = cy + sy * 26;
        Ui::triangle(cx + sx * 9, cy + sy * 9, ax + sx * 6, ay - sy * 8, ax - sx * 8, ay + sy * 6, col);
        Ui::roundRect(ax - 3 + sx * 2, ay - 3 + sy * 2, 6, 6, 3, col);
    }

    /* the name to save it under */
    if (calNaming) {
        GRRLIB_Rectangle(Ui::screenLeft(), 0, Ui::screenWidth(), 480, p.dim, 1);
        Ui::card((640 - calKb.width()) * 0.5f - 10, 104 - calKb.lift(), calKb.width() + 20, calKb.height() + 120, 16, 0.0f);
        Ui::text((640 - calKb.width()) * 0.5f + 4, 116 - calKb.lift(), tr("Save this screen as"), 18, p.text);
        Ui::textRight((640 + calKb.width()) * 0.5f - 4, 120 - calKb.lift(), tr("The same name replaces it"), 13, p.textDim);
        const std::string typed = calName + "_";
        Ui::field((640 - calKb.width()) * 0.5f, 150 - calKb.lift(), calKb.width(), 40, typed.c_str(), 18, true);
        calKb.render(ir);
        static const Ui::Hint kl[] = { { "A", N_("Type") }, { "kbdel", N_("Delete") }, { "kbspace", N_("Space") }, { "kbsym", N_("Symbols") }, { "+!", N_("Done") } };
        Ui::bottomBar(kl, 5);
    }
}

// ---------------------------------------------------------------
bool SettingsView::update(ir_t& ir) {
    if (editing) {
        if (editor.update(ir)) { editing = false; lastHoverRow = lastHoverSide = -1; }
        return false;
    }
    if (calibrating) { updateCalibration(ir); return false; }
    if (pinStep) {
        const int r = pin.update(ir);
        if (r < 0) { pinStep = 0; signInWant = profile->signIn; wantsLibraries = false; return false; }   /* the settings, as they were */
        if (r == 0) return false;
        if (pinStep == 1) {
            pinFirst = pin.code();
            pinStep = 2;
            pin.start(N_("Enter it again"));
        } else if (pin.code() != pinFirst) {
            pinStep = 1;
            pin.start(N_("Choose a passcode"), N_("The two didn't match: once more"));
            pin.reject();
        } else {
            profile->passSalt = PinPad::newSalt();
            profile->passHash = PinPad::hash(profile->passSalt, pin.code());
            profile->signIn   = SavedProfile::Passcode;
            pinStep = 0;
            SoundFX::play(SoundFX::FX::Start);
            SYS_Report("[Settings] sign-in %d\n", profile->signIn);
            return true;   /* the passcode was asked on the way out */
        }
        return false;
    }

    /* typing the search: the matches follow each letter */
    if (typing) {
        kb.setOrigin((640 - kb.width()) * 0.5f, 214 - kb.lift());
        const std::string was = query;
        const Keyboard::Result r = kb.update(ir, query, 32);
        if (query != was) { selectedIndex = top = 0; runSearch(); }
        if (r == Keyboard::Result::Enter || r == Keyboard::Result::Cancel) {
            typing = false;
            sideFocus = rows.empty() || r == Keyboard::Result::Cancel;
            SoundFX::play(r == Keyboard::Result::Enter ? SoundFX::FX::Open : SoundFX::FX::Back);
        }
        return false;
    }

    irMode = ir.valid;   /* no pointer on the screen: the D-pad drives, A acts on the highlighted item */
    const bool a = Input::isAJustPressed();
    const int  n = (int)rows.size(), nSide = (int)sections.size();
    const int  selBefore = selectedIndex, sideBefore = sideSel, secBefore = section;
    const bool focusBefore = sideFocus;

    /* 1: the search, from anywhere */
    if (Input::is1Pressed()) {
        openSection(S_SEARCH);
        typing = true;
        kb.reset();
        SoundFX::play(SoundFX::FX::Open);
        return false;
    }

    /* the pointer: a section, or a setting */
    int hoverSide = -1, hoverRow = -1;
    if (ir.valid) {
        for (int i = 0; i < nSide; ++i) {
            const float y = LIST_Y + i * ROW_STEP;
            if (ir.x >= SIDE_X && ir.x < SIDE_X + SIDE_W && ir.y >= y && ir.y < y + ROW_H) hoverSide = i;
        }
        float y = rowsTop();
        for (int k = 0, vis = visibleRows(); k < vis && top + k < n; ++k) {
            const int i = top + k;
            if (ir.x >= PANE_X && ir.x < PANE_X + PANE_W && ir.y >= y && ir.y < y + rowHeight(i)) hoverRow = i;
            y += rowStep(i);
        }
        if (section == S_SEARCH && ir.x >= PANE_X && ir.x < PANE_X + PANE_W &&
            ir.y >= LIST_Y && ir.y < LIST_Y + FIELD_H && a) {
            typing = true;   /* A on the search's field: type again */
            SoundFX::play(SoundFX::FX::Open);
            return false;
        }
    }
    const bool newSide = hoverSide >= 0 && hoverSide != lastHoverSide;
    const bool newRow  = hoverRow  >= 0 && hoverRow  != lastHoverRow;
    lastHoverSide = hoverSide; lastHoverRow = hoverRow;

    if (sideFocus) {
        if (Input::isBackPressed()) {
            SoundFX::play(SoundFX::FX::Back);
            return leaveSignIn();
        }
        if (Input::isUpPressed())   sideSel = (sideSel + nSide - 1) % nSide;
        if (Input::isDownPressed()) sideSel = (sideSel + 1) % nSide;
        if (newSide) sideSel = hoverSide;
        /* the section highlighted shows its settings at once */
        if (sections[sideSel] != section) openSection(sections[sideSel]);
        const bool open = (a && (!ir.valid || hoverSide >= 0)) || Input::isRightPressed();
        if (open) {
            if (section == S_SEARCH && (a || rows.empty())) { typing = true; kb.reset(); }
            else if (!rows.empty())                         sideFocus = false;
        }
        if (newRow) { sideFocus = false; selectedIndex = hoverRow; }
        else if (a && hoverRow >= 0) { sideFocus = false; selectedIndex = hoverRow; }   /* A on one: it, next A changes it */
    } else {
        if (Input::isBackPressed() || Input::isLeftPressed()) {
            sideFocus = true;
            SoundFX::play(SoundFX::FX::Back);
            return false;
        }
        if (n > 0) {
            if (Input::isUpPressed())   selectedIndex = (selectedIndex + n - 1) % n;
            if (Input::isDownPressed()) selectedIndex = (selectedIndex + 1) % n;
            if (Input::isLPressed())    selectedIndex = selectedIndex - visibleRows() < 0 ? 0 : selectedIndex - visibleRows();
            if (Input::isRPressed())    selectedIndex = selectedIndex + visibleRows() >= n ? n - 1 : selectedIndex + visibleRows();
        }
        if (newRow) selectedIndex = hoverRow;
        if (newSide) { sideFocus = true; sideSel = hoverSide; if (sections[sideSel] != section) openSection(sections[sideSel]); }
        follow();
        if (a && n > 0 && (!ir.valid || hoverRow >= 0)) {
            const bool header = isHeader(rows[selectedIndex]);
            activate(rows[selectedIndex]);
            if (header) { SoundFX::play(SoundFX::FX::Select); return false; }
            if (wantsLibraries) {   /* the settings close for the list */
                SoundFX::play(SoundFX::FX::Open);
                return leaveSignIn();
            }
            if (section == S_SEARCH) {   /* the match changed its words: still listed */
                const int id = rows[selectedIndex];
                runSearch();
                for (int i = 0; i < (int)rows.size(); ++i) if (rows[i] == id) selectedIndex = i;
                if (rows.empty()) sideFocus = true;
                follow();
            }
            SoundFX::play(SoundFX::FX::Start);   /* turning Interface Sounds on plays it */
            return false;
        }
    }
    if (typing)                                                SoundFX::play(SoundFX::FX::Open);
    else if (section != secBefore)                             SoundFX::play(SoundFX::FX::Page);
    else if (selectedIndex != selBefore || sideSel != sideBefore || sideFocus != focusBefore)
                                                               SoundFX::play(SoundFX::FX::Move);
    return false;
}

/* s in at most two lines of w pixels (size 13), a word at a time */
static void wrap2(const std::string& s, float w, std::string& l1, std::string& l2)
{
    l1 = s; l2.clear();
    if (Ui::textWidth(s.c_str(), 13) <= w) return;
    size_t cut = std::string::npos, sp = s.find(' ');
    while (sp != std::string::npos && Ui::textWidth(s.substr(0, sp).c_str(), 13) <= w) { cut = sp; sp = s.find(' ', sp + 1); }
    if (cut == std::string::npos) return;
    l1 = s.substr(0, cut);
    l2 = s.substr(cut + 1);
}

void SettingsView::render(ir_t& ir) {
    if (editing) { editor.render(ir); return; }
    if (calibrating) { renderCalibration(ir); return; }
    if (pinStep) { Ui::background(); pin.render(ir); return; }
    const Ui::Palette& p = Ui::pal();
    const int n = (int)rows.size(), vis = visibleRows();

    Ui::background();
    Ui::header(tr("Settings"), tr(SECTIONS[section]));

    /* the sections: the open one lit, brighter with the highlight on it */
    for (int i = 0; i < (int)sections.size(); ++i) {
        const bool open = sections[i] == section;
        sideAnim[i] = Ui::approach(sideAnim[i], open ? (sideFocus || typing ? 1.0f : 0.45f) : 0.0f);
        Ui::button(SIDE_X, LIST_Y + i * ROW_STEP, SIDE_W, ROW_H, tr(SECTIONS[sections[i]]), 16,
                   sideAnim[i] > 0.01f ? (sideFocus && open ? Ui::pulse() : 1.0f) * sideAnim[i] : 0.0f);
    }
    /* who made it, under the sections */
    {
        const float y = LIST_Y + sections.size() * ROW_STEP + 14;
        Ui::textCentered(SIDE_X + SIDE_W / 2, y, "WiiFin-XE v" WIIFIN_VERSION, 12, p.accentDark);
        Ui::textCentered(SIDE_X + SIDE_W / 2, y + 18, tr("Made with \xe2\x9d\xa4 for Jellyfin"), 11, p.textDim);
    }

    /* the search's text, over its matches */
    if (section == S_SEARCH) {
        Ui::field(PANE_X, LIST_Y, PANE_W, FIELD_H, query.c_str(), 16, typing);
        if (query.empty() && !typing)
            Ui::text(PANE_X + 14, LIST_Y + 8, tr("A to type: any setting, by name or what it does"), 14, p.textDim);
    }

    /* the settings */
    const float y0 = rowsTop();
    for (int i = 0; i < n && i < 32; ++i)
        focusAnim[i] = Ui::approach(focusAnim[i], !sideFocus && !typing && i == selectedIndex ? 1.0f : 0.0f);
    float y = y0;
    for (int k = 0; k < vis && top + k < n; ++k, y += rowStep(top + k - 1)) {
        const int i = top + k;
        const float f = i < 32 ? focusAnim[i] : 0.0f;
        if (isHeader(rows[i])) {   /* a group's header: its name, folded or not */
            const int g = rows[i] - H_BASE;
            Ui::roundRect(PANE_X, y, PANE_W, HDR_H, 9, Ui::alpha(p.cardBottom, 0.45f));
            if (f > 0.01f) {
                Ui::shadow(PANE_X - 2, y - 2, PANE_W + 4, HDR_H + 4, 11, 8.0f, Ui::alpha(p.glow, f));
                Ui::roundBorder(PANE_X, y, PANE_W, HDR_H, 9, 2.0f + f, Ui::alpha(p.focus, f));
            }
            const float cx = PANE_X + 16, cy = y + HDR_H * 0.5f;
            const u32 c = Ui::mix(p.textDim, p.accentDark, f);
            if (s_folded[g]) Ui::triangle(cx - 3, cy - 6, cx + 4, cy, cx - 3, cy + 6, c);   /* > folded */
            else             Ui::triangle(cx - 6, cy - 3, cx + 6, cy - 3, cx, cy + 4, c);   /* v open */
            Ui::text(PANE_X + 30, y + 5, tr(GROUPS[g]), 15, Ui::mix(p.text, p.accentDark, f));
            if (s_folded[g]) {
                int count = 0;
                for (const RowGroup& rg : sectionGroups(section, profile != nullptr))
                    if (rg.group == g) count = (int)rg.rows.size();
                char m[48];
                snprintf(m, sizeof(m), tr("%d hidden"), count);
                Ui::textRight(PANE_X + PANE_W - 14, y + 6, m, 13, p.textDim);
            }
            continue;
        }
        const RowInfo r = describe(rows[i]);
        Ui::card(PANE_X, y, PANE_W, ROW_H, 12, f);
        const int pw = Ui::textWidth(r.value.c_str(), 14) + 26;
        const float px = PANE_X + PANE_W - pw - 10, py = y + (ROW_H - 24) * 0.5f;
        std::string label = LibDraw::fitText(font, r.label, 17, (int)(px - PANE_X - 26));
        Ui::text(PANE_X + 14, y + 10, label.c_str(), 17, p.text);
        Ui::roundRect(px, py, pw, 24, 12, Ui::mix(r.col, 0xFFFFFFFF, 0.15f), r.col);
        Ui::textCentered(px + pw * 0.5f, py + 3, r.value.c_str(), 14, p.textOnAccent);
    }
    /* left of the settings, in the gap after the sections */
    if (n > vis) Ui::scrollbar(PANE_X - 11, y0, DESC_Y - 6 - y0, top, vis, n);
    if (n == 0 && section == S_SEARCH && !query.empty())
        Ui::textCentered(PANE_X + PANE_W / 2, y0 + 20, tr("No setting matches"), 15, p.textDim);

    /* what the highlighted setting does, in full */
    if (!typing && !sideFocus && selectedIndex < n) {
        std::string desc;
        if (isHeader(rows[selectedIndex]))
            desc = tr(s_folded[rows[selectedIndex] - H_BASE] ? "A shows the settings in this group." : "A hides the settings in this group.");
        else
            desc = describe(rows[selectedIndex]).desc;
        std::string l1, l2;
        wrap2(desc, PANE_W - 28, l1, l2);
        Ui::roundRect(PANE_X, DESC_Y, PANE_W, DESC_H, 12, Ui::alpha(p.cardBottom, 0.6f));
        Ui::text(PANE_X + 14, DESC_Y + 10, LibDraw::fitText(font, l1, 13, (int)PANE_W - 28).c_str(), 13, p.text);
        if (!l2.empty())
            Ui::text(PANE_X + 14, DESC_Y + 32, LibDraw::fitText(font, l2, 13, (int)PANE_W - 28).c_str(), 13, p.text);
    }

    if (typing) {
        Ui::card((640 - kb.width()) * 0.5f - 10, 204 - kb.lift(), kb.width() + 20, kb.height() + 20, 16, 0.0f);
        kb.render(ir);
        return;   /* the keyboard says its own buttons */
    }
    static const Ui::Hint side[] = { { "UD", N_("Choose") }, { "A", N_("Open") }, { "1", N_("Search") }, { "B", N_("Back") } };
    static const Ui::Hint list[] = { { "UD", N_("Choose") }, { "A", N_("Change") }, { "1", N_("Search") }, { "B", N_("Sections") } };
    const Ui::Hint* h = sideFocus ? side : list;
    Ui::bottomBar(h, 3, h + 3, 1);
}
