#pragma once
#include <string>
#include <vector>
#include <utility>
#include <grrlib.h>
#include <wiiuse/wpad.h>

/* -----------------------------------------------------------------------
 * Ui — shared look of WiiFin, inspired by the Wii Menu.
 *
 * Two palettes (light "Wii Menu" and dark "Wii night") selected in the
 * settings; every view draws with the primitives and components below
 * instead of hard-coded colours.  Coordinates are GRRLIB's 640x480.
 *
 * Shapes are drawn straight with GX (triangle fans with per-vertex
 * gradients).  Edges get a 1 px feather that fades to transparent, which
 * gives anti-aliased curves without MSAA; shadows and focus glows use the
 * same trick with a wider feather.
 * ----------------------------------------------------------------------- */
namespace Ui {

/* Light: Wii Menu.  Dark: Wii at night.  Flix: charcoal and red, with the
 * row-of-carousels home screen of the old Wii video channels.  Luhu: black,
 * silver and green; Sushi: dark grey and orange (two more of those
 * channels' colours). */
enum class Theme { Light = 0, Dark = 1, Flix = 2, Luhu = 3, Sushi = 4 };
static const int THEME_COUNT = 5;

struct Palette {
    u32 bgTop, bgBottom, stripe;          /* screen background               */
    u32 cardTop, cardBottom, cardBorder;  /* tiles, cards, buttons            */
    u32 text, textDim, textOnAccent;
    u32 accent, accentDark, glow;         /* Wii blue, focus halo             */
    u32 shadow;
    u32 barTop, barBottom, barBorder;     /* bottom bar                       */
    u32 field, fieldBorder;               /* text inputs, list rows           */
    u32 danger, ok;
    u32 dim;                              /* modal backdrop                   */
    u32 focus;                            /* the ring around what is selected (glow: its halo) */
};

void        setTheme(Theme t);
Theme       nextTheme(Theme t);
Theme       theme();
const char* themeName(Theme t);
const Palette& pal();

/* ---- Your own themes ----------------------------------------------------
 * Text files in themes/ next to wiifin.cfg (sd:/apps/WiiFin/themes/), one
 * theme each, easy to edit and share on a PC:
 *
 *   name = Midnight
 *   base = Dark            (a built-in theme: every colour left out)
 *   background = #101820   (#RRGGBB, #RRGGBBAA, or r,g,b)
 *
 * The eleven main colours (THEME_COLORS) each set the shades that go with
 * them; the palette's own names (bg_bottom, glow...) set one exactly.
 * A theme of one's own keeps its base's layout (Flix: the red band, in
 * the theme's accent). */
struct ThemeColor { const char* key; const char* label; const char* desc; };
static const int THEME_COLOR_COUNT = 11;
extern const ThemeColor THEME_COLORS[THEME_COLOR_COUNT];
struct CustomTheme {
    std::string name;
    std::string file;                                 /* in themes/; "" not saved yet */
    Theme base = Theme::Dark;
    std::vector<std::pair<std::string, u32> > colors; /* key, RRGGBBAA, as in the file */
};
void loadThemes(const std::string& dir);          /* dir: themes/, read now */
const std::vector<CustomTheme>& customThemes();
int  customTheme();                               /* the one in use, -1 a built-in */
void setCustomTheme(int i);                       /* -1: back to the built-in theme() */
int  findCustomTheme(const std::string& file);    /* -1 none */
Palette buildPalette(const CustomTheme& t);
Palette builtInPalette(Theme t);
u32  themeColor(const Palette& p, int i);         /* main colour i, as p has it */
void setThemeColor(CustomTheme& t, const std::string& key, u32 c);
bool parseColor(const std::string& s, u32& c);    /* #RRGGBB(AA), RRGGBB, r,g,b(,a), rgb(r,g,b) */
std::string colorHex(u32 c);                      /* #RRGGBB, alpha too if not opaque */
/* written to themes/ (file from its name), the list read again; its
 * index there, -1 failed */
int  saveTheme(CustomTheme& t);
/* moved to themes/removed/ (never deleted: a PC can put it back), the list
 * read again; in use: back to its base.  false: it couldn't be moved */
bool removeTheme(const std::string& file);
/* the colour editor's palette, drawn by everything while it's set */
void setPreviewPalette(const Palette* p);
bool customColors();                              /* a theme of one's own, or the editor's */
bool lightPalette();                              /* a light background */
/* the Theme setting's choices: the built-in ones, then the files */
int  themeSlot();
int  themeSlotCount();
void setThemeSlot(int slot);
std::string themeSlotName(int slot);
/* Solid colours instead of gradients (no banding on 480i TVs) */
void setSolidColors(bool on);
bool solidColors();
/* The bar's clock: 12-hour (1:31 PM, date month/day) or 24-hour */
void setClock12h(bool on);
bool clock12h();
/* The rows home: the rows under the focused one shaded (fewer things to
 * look at at once), or all of them shown plainly */
void setDimRows(bool on);
bool dimRows();
/* The rows home's header: search, home, browse and menu buttons with
 * icons, or the classic one (search, one pill to the other page, the
 * profile's avatar) */
void setClassicHeader(bool on);
bool classicHeader();

/* Home screen layout: tile grid of libraries, or rows of poster carousels
 * (independent of the theme; picking the Flix theme switches to Rows). */
enum class HomeLayout { Grid = 0, Rows = 1 };
void       setHomeLayout(HomeLayout l);
HomeLayout homeLayout();

/* How libraries are browsed.
 *   Posters    poster grids, carousel with artwork (default)
 *   List       libraries open as text lists instead of poster grids (no
 *              artwork in the list; home, carousel and detail pages keep
 *              theirs)
 *   ListCover  text lists with the selected title's cover beside them */
enum class LibraryStyle { Posters = 0, List = 1, ListCover = 2 };
static const int LIBRARY_STYLE_COUNT = 3;
void         setLibraryStyle(LibraryStyle s);
LibraryStyle libraryStyle();
inline bool  listMode() { return libraryStyle() != LibraryStyle::Posters; }

/* Font used by the components (set once at start-up). */
void setFont(GRRLIB_ttfFont* font);
GRRLIB_ttfFont* font();

/* ---- Primitives -------------------------------------------------------- */
void roundRect(float x, float y, float w, float h, float r, u32 top, u32 bottom);
inline void roundRect(float x, float y, float w, float h, float r, u32 c) { roundRect(x, y, w, h, r, c, c); }
void roundBorder(float x, float y, float w, float h, float r, float thickness, u32 color);
/* Soft shadow / glow around a rounded rect (draw it before the shape). */
void shadow(float x, float y, float w, float h, float r, float blur, u32 color);
void circle(float cx, float cy, float radius, u32 color);
/* Texture clipped to a rounded rectangle. */
void texRound(GRRLIB_texImg* tex, float x, float y, float w, float h, float r,
              u32 tint = 0xFFFFFFFF);
/* Same with an explicit source region (texture coordinates 0..1). */
void texRound(GRRLIB_texImg* tex, float x, float y, float w, float h, float r,
              float u0, float v0, float u1, float v1, u32 tint);
/* Texture scaled to cover a w x h slot (excess cropped evenly).  aspect is
 * the slot's displayed width / height (pass the unsquished width / h). */
void texCover(GRRLIB_texImg* tex, float x, float y, float w, float h, float r,
              float aspect, u32 tint = 0xFFFFFFFF);
/* Rectangle with a colour at each corner (top-left, top-right,
 * bottom-right, bottom-left): the colour picker's square and slides. */
void quad(float x, float y, float w, float h, u32 tl, u32 tr, u32 br, u32 bl);
/* Solid triangle (arrows, play glyphs). */
void triangle(float x0, float y0, float x1, float y1, float x2, float y2, u32 color);
/* Small line icons (about 14 px, 2 px strokes) centred on cx, cy: the
 * header's buttons and the menus */
enum class Icon { Search, Home, Browse, Menu, Play, Back, Profile, Settings, Folder };
/* fill: the inside of an outlined icon (house, tiles) tinted, 0 none */
void icon(Icon i, float cx, float cy, u32 color, u32 fill = 0);

/* Colour helpers */
u32  mix(u32 a, u32 b, float t);
u32  alpha(u32 c, float k);              /* scale the alpha channel */
/* Smoothly move cur toward target (call once per frame). */
float approach(float cur, float target, float speed = 0.25f);

/* ---- Text -------------------------------------------------------------- */
void text(float x, float y, const char* s, int size, u32 color);
void textCentered(float cx, float y, const char* s, int size, u32 color);
void textRight(float xRight, float y, const char* s, int size, u32 color);
int  textWidth(const char* s, int size);

/* ---- Components -------------------------------------------------------- */
/* Screen background.  withBand = false leaves out the theme's header band
 * (screens without a title: main menu, loading spinners). */
void background(bool withBand = true);
/* Height of the coloured header band the background draws (0 if none). */
float headerBand();
/* Redraw that band on top of content that scrolled under it. */
void drawHeaderBand();
/* Card / channel tile.  focus 0..1 fades in the blue border and halo. */
void card(float x, float y, float w, float h, float r, float focus);
/* Pill button with centred label. */
void button(float x, float y, float w, float h, const char* label, int size, float focus);
/* Title at the top left with a thin separator. */
void header(const char* title, const char* subtitle = nullptr);
/* A message (the server's "Send message"): a card at the top of whatever
 * screen is drawn, for ms; drawNotice() draws it (the render hook). */
void showNotice(const std::string& header, const std::string& text, int ms);
/* the music that goes on while browsing: the footers say what plays ("":
 * nothing); any thread */
void setNowPlaying(const std::string& line);
void drawNotice();

/* Bottom bar with the clock in the middle and button hints on the sides.
 * Hints are pairs of (button, label); button is "A", "B", "1", "2", "+",
 * "-", "HOME" or a D-pad glyph name ("UD", "LR"). */
struct Hint { const char* button; const char* label; };
/* Hints name Wii Remote buttons ("A", "1", "-/+", "HOME", ...); they are
 * shown as the buttons of the controller used last (Input sets it). */
enum class ButtonStyle { WiiRemote, Classic, GameCube, Keyboard };
void        setButtonStyle(ButtonStyle s);
ButtonStyle buttonStyle();
const char* buttonName(const char* wiiRemoteButton);
void bottomBar(const Hint* left, int nLeft, const Hint* right = nullptr, int nRight = 0);
/* Wiimote-style button glyph followed by its label; returns the width used. */
float hint(float x, float y, const Hint& h);
float hintWidth(const Hint& h);
/* Compact bar along the bottom edge (y 450..480) for screens whose content
 * reaches the bottom; optional centred caption. */
void footer(const Hint* left, int nLeft, const Hint* right = nullptr, int nRight = 0,
            const char* center = nullptr);

/* Segmented pill of tabs centred on cx; returns the x of its left edge. */
float tabs(float cx, float y, const char* const* names, int n, int sel, int size = 14);
/* The tab under (x, y) on the bar tabs() drew last (in the last 100 ms:
 * still on the screen), -1 none */
int tabAt(float x, float y);
/* Round up/down arrow button (page arrows, scrolling lists).  Disabled
 * buttons are faded; hovering grows the button and lights its ring. */
void arrowButton(float cx, float cy, bool up, bool enabled, bool hover);
/* Bigger left/right arrow at a screen edge to turn pages (Wii Settings style). */
void pageArrow(float cx, float cy, bool left, bool enabled, bool hover);

/* Thin rounded progress bar (frac 0..1). */
void progress(float x, float y, float w, float h, float frac);
/* Vertical scrollbar. */
void scrollbar(float x, float y, float h, int first, int visible, int total);
/* Virtual keyboard key.  special = function key (shift, symbols, delete),
 * latched = highlighted toggle (shift on). */
void key(float x, float y, float w, float h, const char* label,
         bool sel, bool hover, bool special, bool latched, float scale = 1.0f);
/* Text field (rounded, focus ring); value is drawn as given. */
void field(float x, float y, float w, float h, const char* value, int size, bool focused);
/* ---- Screen shape (4:3 / 16:9) ----------------------------------------
 * Views lay out a 640x480 column.  On a 16:9 TV the drawing space is
 * widened to about 854x480 (the column centred in it, x from screenLeft()
 * to screenRight()), so shapes, text and pictures keep their proportions
 * once the TV stretches the picture; full-width chrome (backgrounds, bars,
 * bands, carousel rows) spans screenLeft()..screenRight(). */
void  initScreen(bool widescreen);
float screenLeft();               /* 0 in 4:3, about -107 in 16:9 */
float screenRight();              /* 640 in 4:3, about 747 in 16:9 */
inline float screenWidth() { return screenRight() - screenLeft(); }
/* Map a Wii Remote pointer reading (0..640 across the TV) to drawing space. */
void  pointerToScreen(ir_t& ir);

/* ---- Screen area (overscan compensation) -----------------------------
 * Many TVs, CRTs above all, crop the edges of the picture.  The whole
 * picture (backgrounds, GUI, video, pointer) is drawn into the rectangle
 * left after removing these margins (pixels of the 640x480 frame), with
 * black around it: the GX viewport maps the 640x480 drawing space onto
 * that rectangle, so every view keeps its coordinates and the Wii Remote
 * pointer stays aligned with what it points at. */
static const int SAFE_MAX_X = 80, SAFE_MAX_Y = 60;
void setSafeArea(int left, int top, int right, int bottom);
void safeArea(int& left, int& top, int& right, int& bottom);
/* the screen area's snaps (Settings > Screen Area, -/+): the picture's
 * shapes (a 4:3 box in a 16:9 picture...) and margins for overscan */
struct ScreenPreset { const char* name; int l, t, r, b; };
const ScreenPreset* screenPresets(int& n);
int  screenPreset();     /* the snap the area is on, -1: adjusted by hand */
/* screens of one's own: an area saved under a name (one per TV), in
 * wiifin.cfg; -/+ go through them after the snaps */
struct SavedScreen { std::string name; int l, t, r, b; };
std::vector<SavedScreen>& savedScreens();
int  savedScreen();      /* the saved one the area is on, -1: none */
bool wideFrame();        /* the console's picture is 16:9 */
/* Scissor rectangle given in drawing (640x480) coordinates. */
void clip(float x, float y, float w, float h);
void clipReset();

/* Shift everything drawn afterwards (shapes, text, images) by dx, dy until
 * the matching popOffset(); used for slide-in panels. */
void pushOffset(float dx, float dy);
void popOffset();
/* Gentle 0.8..1 pulse used for the focused element. */
float pulse();

/* Profile picture: the user's initial on an accent disc with a white ring
 * (readable on any background, including the Flix header band). */
/* pic: a Mii or a picture of its own (Avatars), drawn instead when found */
void avatar(float cx, float cy, float r, const char* name, float focus, const std::string& pic = std::string());

/* Rotating loading ring (ring.png) centred at cx, cy. */
void spinner(GRRLIB_texImg* ring, float cx, float cy);
/* A load: the picture the TV shows now kept (from the frame buffer, at
 * half size), drawn back dimmed under the spinner instead of a blank
 * screen; let go once the load is done.  drawFrozen: false when none */
bool freezeFrame();
bool drawFrozen();
void releaseFrozen();
/* each frame, as it is shown (the render hook): whether it had a spinner,
 * which a frozen picture must not (two spinners, one still) */
void frameShown();
/* While a USB mouse has the pointer: its arrow drawn at x, y, true (the
 * screen then draws no Wii Remote cursor). */
bool mouseCursor(float x, float y);

} // namespace Ui
