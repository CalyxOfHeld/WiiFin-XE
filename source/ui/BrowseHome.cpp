#include "BrowseHome.h"
#include "../core/ExitZone.h"
#include "Ui.h"
#include "../core/Text.h"
#include "../core/Utils.h"
#include "../core/Lang.h"
#include "../input/Input.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ogc/lwp_watchdog.h>

/* WiiFin logo (data/images/logo_wiifin.png), shared by both pages */
extern unsigned char data_logo_wiifin_png[];
static GRRLIB_texImg* s_logo = nullptr;
static int            s_logoUsers = 0;

/* Defined in LibraryView.cpp: decodes a JPEG into an RGBA8 texture. */
GRRLIB_texImg* loadJPEGTexture(const u8* data, u32 size);

/* ---- Layout (640x480; rows, band and header span the whole screen, which
 * is wider in 16:9 — see Ui::screenLeft/Right) ----------------------------- */
static const float BAND_Y     = 48,  BAND_H = 252;   /* focused row panel        */
static const float POSTER_Y   = 80;                  /* top of the focused row   */
static const float POSTER_H   = 146, POSTER_W = 98;  /* 2:3 posters              */
static const float GAP        = 9;
#define X0 (Ui::screenLeft() + 30.0f)              /* first poster of a row */
static const float ROW_STRIDE = 252;                 /* poster top to poster top */
static const float INFO_Y     = 240, INFO_H = 52;
/* the Libraries row (no info panel): lower, clear of the header, on a
 * shorter band */
static const float WIDE_DROP  = 14, BAND_H_WIDE = 210;
#define INFO_X (Ui::screenLeft() + 28.0f)
#define INFO_W (Ui::screenWidth() - 56.0f)
static const int   MAX_ITEMS  = 20;                  /* per row (home)           */
static const int   CATALOG_ITEMS = 60;               /* per row (browse page)    */
static const int   SECTION_ITEMS = 12;               /* per library (New Additions, Top Rated) */

/* Header pills */
static const float PILL_Y = 9, PILL_H = 26;
#define AVATAR_CX (Ui::screenRight() - 24.0f)
static const float AVATAR_CY = 22, AVATAR_R = 14;
static const char* pillLabel(int i, bool catalog)
{
    /* TRANSLATORS: the pills at the top right of the home screen, in lower case */
    return i == 0 ? tr("search") : (catalog ? tr("home") : tr("browse"));
}

static u32 lerpCol(u32 a, u32 b, float t) { return Ui::mix(a, b, t); }

/* Colours of the carousel home.  Flix keeps the look of the old video
 * channels (grey band, light info panel, white pills on the red header);
 * the other themes take everything from their palette. */
struct Look {
    bool band;                                   /* red header band (Flix) */
    u32  bandTop, bandBottom, bandEdge;          /* focused-row panel      */
    u32  title, titleDim, dimTint;
    u32  phTop, phBottom, phTopDim, phBottomDim, phText, phTextDim;
    u32  selBorder, tileBorder, selShadow, tileShadow, arrow;
    u32  panelTop, panelBottom, panelBorder, panelText, panelMeta;
    u32  starOn, starOff;
    u32  word, wordShadow;
    u32  pillTop, pillBottom, pillBorder, pillFocus, pillText, pillGlow;
    u32  floor;
};

static Look look()
{
    const Ui::Palette& p = Ui::pal();
    Look l;
    if (Ui::theme() == Ui::Theme::Flix) {
        l = Look{ true,
            0x6A6A6AFF, 0x474747FF, 0xFFFFFF30,
            0xFFFFFFFF, 0xA0A0A0FF, 0x6A6A6AFF,
            0x505050FF, 0x3A3A3AFF, 0x2A2A2AFF, 0x202020FF, 0xC8C8C8FF, 0x707070FF,
            0xFFFFFFFF, 0x00000060, 0x00000090, 0x00000070, 0xFFFFFFE0,
            0xF5F5F5FF, 0xC6C6C6FF, 0x8A8A8AFF, 0x262626FF, 0x3E3E3EFF,
            0xD81F26FF, 0xA9A9A9FF,
            0xFFFFFFFF, 0x00000070,
            0xFFFFFFFF, 0xCFCFCFFF, 0x5E5E5EFF, 0xFFFFFFFF, 0x333333FF, 0xFFFFFFB0,
            0x111111FF };
        return l;
    }
    const bool light = Ui::theme() == Ui::Theme::Light;
    l.band        = false;
    l.bandTop     = Ui::alpha(p.cardTop, 0.80f);
    l.bandBottom  = Ui::alpha(p.cardBottom, 0.80f);
    l.bandEdge    = Ui::alpha(p.cardBorder, 0.70f);
    l.title       = p.text;
    l.titleDim    = p.textDim;
    l.dimTint     = light ? 0xC4C4C4FF : 0x6A6A6AFF;
    l.phTop       = p.cardTop;     l.phBottom    = p.cardBottom;
    l.phTopDim    = Ui::mix(p.cardTop, p.bgBottom, 0.5f);
    l.phBottomDim = Ui::mix(p.cardBottom, p.bgBottom, 0.5f);
    l.phText      = p.textDim;     l.phTextDim   = Ui::alpha(p.textDim, 0.6f);
    l.selBorder   = p.accent;      l.tileBorder  = Ui::alpha(p.cardBorder, 0.7f);
    l.selShadow   = p.glow;        l.tileShadow  = p.shadow;
    l.arrow       = p.accent;
    l.panelTop    = p.cardTop;     l.panelBottom = p.cardBottom;  l.panelBorder = p.cardBorder;
    l.panelText   = p.text;        l.panelMeta   = p.textDim;
    l.starOn      = 0xF2B01EFF;    l.starOff     = light ? 0xC5C9CEFF : 0x4A5668FF;
    l.word        = p.text;        l.wordShadow  = 0x00000000;
    l.pillTop     = p.cardTop;     l.pillBottom  = p.cardBottom;  l.pillBorder  = p.cardBorder;
    l.pillFocus   = p.accent;      l.pillText    = p.text;        l.pillGlow    = p.glow;
    l.floor       = p.bgBottom;
    return l;
}

static std::string fit(const std::string& s, int size, int maxW)
{
    if (Ui::textWidth(s.c_str(), size) <= maxW) return s;
    std::string t = s;
    while (!t.empty() && Ui::textWidth((t + "...").c_str(), size) > maxW) {
        while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80) t.pop_back();
        if (!t.empty()) t.pop_back();
    }
    return t + "...";
}

static float pillWidth(int i, bool catalog) { return Ui::textWidth(pillLabel(i, catalog), 15) + (i == 0 ? 40 : 28); }
static float pillX(int i, bool catalog)
{
    float right = AVATAR_CX - AVATAR_R - 10;
    float w1 = pillWidth(1, catalog), w0 = pillWidth(0, catalog);
    return i == 1 ? right - w1 : right - w1 - 8 - w0;
}

/* ------------------------------------------------------------------------ */

BrowseHome::BrowseHome(JellyfinClient& c, const std::string& url, const JellyfinAuth& a, Mode m)
    : client(c), mode(m), serverUrl(url), auth(a)
{
    LWP_MutexInit(&texLock, false);
    if (s_logoUsers++ == 0) s_logo = GRRLIB_LoadTexture(data_logo_wiifin_png);
}

BrowseHome::~BrowseHome()
{
    stopLoader();
    for (auto& r : rows) freeTextures(r);
    freeTextures(exp);
    if (texLock != LWP_MUTEX_NULL) LWP_MutexDestroy(texLock);
    if (--s_logoUsers == 0 && s_logo) { GRRLIB_FreeTexture(s_logo); s_logo = nullptr; }
}

void BrowseHome::invalidate()
{
    stopLoader();
    for (auto& r : rows) freeTextures(r);
    rows.clear();
    freeTextures(exp);   /* an expanded row stays open: fetched again by build() */
    isBuilt = false;
}

void BrowseHome::freeTextures(Row& r)
{
    LWP_MutexLock(texLock);
    for (size_t i = 0; i < r.tex.size(); ++i) {
        if (r.tex[i]) { GRRLIB_FreeTexture(r.tex[i]); r.tex[i] = nullptr; }
        r.st[i] = TILE_NONE;
    }
    LWP_MutexUnlock(texLock);
}

/* ---- Rows ---------------------------------------------------------------- */

void BrowseHome::build(const std::vector<JellyfinLibrary>& libs)
{
    stopLoader();
    /* keep the selection across rebuilds (matched by row title) */
    std::string keepTitle = rowSel < (int)rows.size() ? rows[rowSel].title : "";
    for (auto& r : rows) freeTextures(r);
    rows.clear();

    if (mode == Mode::Catalog) buildCatalog(libs);
    else                       buildHome(libs);

    rowSel = 0;
    for (int i = 0; i < (int)rows.size(); ++i)
        if (rows[i].title == keepTitle) { rowSel = i; break; }
    rowAnim = (float)rowSel;
    headerFocus = -1;
    if (expanded) fetchExpanded();   /* fresh too: resume points, favourites */
    isBuilt = true;
}

/* Row from a query; empty rows are dropped. */
#define ADD_ROW(T_, Q_, SQ_, CAP_) do {                                   \
        Row r_;                                                              \
        r_.title  = (T_);                                                    \
        r_.square = (SQ_);                                                   \
        r_.query  = (Q_);                                                    \
        client.getItemsByQuery(serverUrl, auth, (Q_), r_.items);             \
        if (!r_.items.empty()) {                                             \
            if ((int)r_.items.size() > (CAP_)) r_.items.resize(CAP_);        \
            r_.tex.assign(r_.items.size(), nullptr);                         \
            r_.st.assign(r_.items.size(), TILE_NONE);                        \
            rows.push_back(std::move(r_));                                   \
        }                                                                    \
    } while (0)

void BrowseHome::buildHome(const std::vector<JellyfinLibrary>& libs)
{
    const std::string& uid = auth.userId;
    auto add = [&](const std::string& title, const std::string& query, bool square) {
        ADD_ROW(title, query, square, MAX_ITEMS);
    };

    /* one part of a row of several (one per library), empty ones dropped */
    auto addSection = [&](Row& r, const std::string& label, const std::string& query) {
        std::vector<JellyfinItem> got;
        client.getItemsByQuery(serverUrl, auth, query, got);
        if (got.empty()) return;
        if ((int)got.size() > SECTION_ITEMS) got.resize(SECTION_ITEMS);
        r.sections.push_back({ (int)r.items.size(), label, query });
        r.items.insert(r.items.end(), got.begin(), got.end());
    };
    auto pushRow = [&](Row& r) {
        if (r.items.empty()) return;
        r.tex.assign(r.items.size(), nullptr);
        r.st.assign(r.items.size(), TILE_NONE);
        rows.push_back(std::move(r));
    };

    char q[320];
    snprintf(q, sizeof(q), "/Users/%s/Items/Resume?Limit=%d&MediaTypes=Video", uid.c_str(), MAX_ITEMS);
    add(tr("Continue Watching"), q, false);
    snprintf(q, sizeof(q),
             "/Users/%s/Items?Filters=IsFavorite&Recursive=true"
             "&IncludeItemTypes=Movie,Series,Episode&SortBy=SortName&Limit=%d", uid.c_str(), MAX_ITEMS);
    add(tr("My Favorites"), q, false);
    snprintf(q, sizeof(q), "/Shows/NextUp?UserId=%s&Limit=%d", uid.c_str(), MAX_ITEMS);
    add(tr("Next Up"), q, false);

    /* the latest and the best rated of every library, each in one row: a
     * part per library, its name over its first poster */
    Row added, top;
    added.title = tr("New Additions");
    top.title   = tr("Top Rated");
    for (const auto& lib : libs) {
        const std::string& t = lib.collectionType;
        if (!(t == "movies" || t == "tvshows" || t == "music" || t == "homevideos" ||
              t == "musicvideos" || t.empty() || t == "mixed"))
            continue;
        snprintf(q, sizeof(q), "/Users/%s/Items/Latest?ParentId=%s&Limit=%d",
                 uid.c_str(), lib.id.c_str(), SECTION_ITEMS);
        addSection(added, lib.name, q);
        if (t == "movies" || t == "tvshows") {
            snprintf(q, sizeof(q),
                     "/Users/%s/Items?ParentId=%s&Recursive=true&IncludeItemTypes=%s"
                     "&SortBy=CommunityRating,SortName&SortOrder=Descending&Limit=%d",
                     uid.c_str(), lib.id.c_str(), t == "movies" ? "Movie" : "Series", SECTION_ITEMS);
            addSection(top, lib.name, q);
        }
    }
    /* the rows' own queries: every library at once (the "All" part of the
     * expanded page) */
    snprintf(q, sizeof(q), "/Users/%s/Items/Latest?Limit=%d", uid.c_str(), SECTION_ITEMS);
    added.query = q;
    snprintf(q, sizeof(q),
             "/Users/%s/Items?Recursive=true&IncludeItemTypes=Movie,Series"
             "&SortBy=CommunityRating,SortName&SortOrder=Descending&Limit=%d", uid.c_str(), SECTION_ITEMS);
    top.query = q;
    pushRow(added);
    pushRow(top);

    snprintf(q, sizeof(q),
             "/Users/%s/Items?Filters=IsFavorite&Recursive=true"
             "&IncludeItemTypes=MusicAlbum&SortBy=SortName&Limit=%d", uid.c_str(), MAX_ITEMS);
    add(tr("Favorite Albums"), q, true);
    int favs = 0;
    for (const auto& r : rows) if (r.title == tr("My Favorites")) favs = (int)r.items.size();
    SYS_Report("[Home] %d rows, %d favourite titles\n", (int)rows.size(), favs);
}

/* "browse" page: the libraries themselves, then every title of each library
 * (A-Z) and a few genre rows for films and series. */
void BrowseHome::buildCatalog(const std::vector<JellyfinLibrary>& libs)
{
    const std::string& uid = auth.userId;
    char q[384];

    {
        Row r;
        r.title = tr("Libraries");
        /* Jellyfin draws each library's picture itself, 16:9 with the name
         * across the middle: shown whole, not cut to a portrait poster (that
         * kept a blurred strip of it, a few letters of the name) */
        r.wide  = true;
        for (const auto& lib : libs) {
            JellyfinItem it;
            it.id   = lib.id;
            it.name = lib.name;
            it.type = "CollectionFolder";
            it.seriesName = lib.collectionType;   /* shown in the info panel */
            r.items.push_back(it);
        }
        if (!r.items.empty()) {
            r.tex.assign(r.items.size(), nullptr);
            r.st.assign(r.items.size(), TILE_NONE);
            rows.push_back(std::move(r));
        }
    }

    for (const auto& lib : libs) {
        const std::string& t = lib.collectionType;
        const char* types = t == "movies"  ? "Movie"
                          : t == "tvshows" ? "Series"
                          : t == "music"   ? "MusicAlbum"
                          : (t == "homevideos" || t == "musicvideos") ? "Video,MusicVideo,Movie"
                          : nullptr;
        if (!types) continue;
        snprintf(q, sizeof(q),
                 "/Users/%s/Items?ParentId=%s&Recursive=true&IncludeItemTypes=%s"
                 "&SortBy=SortName&SortOrder=Ascending&Limit=%d",
                 uid.c_str(), lib.id.c_str(), types, CATALOG_ITEMS);
        char title[160];
        /* TRANSLATORS: a row of the browse page; %s is the name of a library */
        snprintf(title, sizeof(title), tr("All %s"), lib.name.c_str());
        ADD_ROW(std::string(title), q, t == "music", CATALOG_ITEMS);

        if (t != "movies" && t != "tvshows") continue;
        std::vector<JellyfinItem> genres;
        snprintf(q, sizeof(q), "/Genres?ParentId=%s&UserId=%s&SortBy=SortName&Limit=8",
                 lib.id.c_str(), uid.c_str());
        client.getItemsByQuery(serverUrl, auth, q, genres);
        for (const auto& g : genres) {
            snprintf(q, sizeof(q),
                     "/Users/%s/Items?ParentId=%s&Recursive=true&IncludeItemTypes=%s"
                     "&GenreIds=%s&SortBy=Random&Limit=%d",
                     uid.c_str(), lib.id.c_str(), types, g.id.c_str(), MAX_ITEMS);
            ADD_ROW(g.name + " \xc2\xb7 " + lib.name, q, false, MAX_ITEMS);
        }
    }
}

int BrowseHome::sectionOf(const Row& r, int i)
{
    int s = -1;
    for (int k = 0; k < (int)r.sections.size() && r.sections[k].start <= i; ++k) s = k;
    return s;
}

/* an album in a row of portrait posters (New Additions): square, its whole
 * cover in the middle of the tile */
static bool squareInTall(bool rowSquare, bool rowWide, const JellyfinItem& it)
{
    return !rowSquare && !rowWide && it.type == "MusicAlbum";
}

const JellyfinItem* BrowseHome::selectedItem() const
{
    if (expanded)
        return exp.sel >= 0 && exp.sel < (int)exp.items.size() ? &exp.items[exp.sel] : nullptr;
    if (rowSel < 0 || rowSel >= (int)rows.size()) return nullptr;
    const Row& r = rows[rowSel];
    if (r.sel < 0 || r.sel >= (int)r.items.size()) return nullptr;
    return &r.items[r.sel];
}

/* ---- Geometry ------------------------------------------------------------ */

float BrowseHome::rowDrop(const Row& r) { return r.wide ? WIDE_DROP : 0.0f; }

/* the band's height, following the rows as they slide */
float BrowseHome::bandHeight() const
{
    if (rows.empty()) return BAND_H;
    int a = (int)floorf(rowAnim), b = a + 1;
    float t = rowAnim - a;
    auto h = [&](int r) {
        if (r < 0) r = 0;
        if (r >= (int)rows.size()) r = (int)rows.size() - 1;
        return rows[r].wide ? BAND_H_WIDE : BAND_H;
    };
    return h(a) + (h(b) - h(a)) * t;
}

float BrowseHome::tileW(const Row& r) const
{
    return (r.wide ? POSTER_H * 16.0f / 9.0f : r.square ? POSTER_H : POSTER_W) * WiiUtils::wsScaleX();
}

float BrowseHome::stride(const Row& r) const
{
    return tileW(r) + GAP * WiiUtils::wsScaleX();
}

int BrowseHome::fullyVisible(const Row& r) const
{
    int n = (int)((Ui::screenRight() - 28.0f - X0 + GAP) / stride(r));
    return n < 1 ? 1 : n;
}

float BrowseHome::targetScroll(const Row& r) const
{
    int n = (int)r.items.size(), vis = fullyVisible(r);
    int t = r.first;
    if (t > n - vis) t = n - vis;
    if (t < 0) t = 0;
    return (float)t;
}

/* The view only scrolls on purpose: d-pad moves past the edge, or the side
 * arrows.  Hovering a poster selects it without moving anything, otherwise
 * the poster under the pointer would keep changing as the row slides. */
void BrowseHome::followSelection(Row& r)
{
    int n = (int)r.items.size(), vis = fullyVisible(r);
    if (r.sel < r.first + 1)       r.first = r.sel - 1;          /* keep one poster of context */
    if (r.sel > r.first + vis - 2) r.first = r.sel - vis + 2;
    if (r.first > n - vis) r.first = n - vis;
    if (r.first < 0) r.first = 0;
}

void BrowseHome::scrollRow(Row& r, int delta)
{
    int n = (int)r.items.size(), vis = fullyVisible(r);
    r.first += delta;
    if (r.first > n - vis) r.first = n - vis;
    if (r.first < 0) r.first = 0;
    /* keep the selection on a visible poster */
    if (r.sel < r.first) r.sel = r.first;
    if (r.sel > r.first + vis - 1) r.sel = r.first + vis - 1;
    if (r.sel > n - 1) r.sel = n - 1;
}

/* ---- Poster loader --------------------------------------------------------- */

void BrowseHome::startLoader()
{
    if (loaderThread != LWP_THREAD_NULL || rows.empty()) return;
    static u8 stack[64 * 1024] DEAD_AT_EXIT __attribute__((aligned(32)));
    stopReq = false;
    /* Below the main thread: it runs while the UI waits for vsync. */
    if (LWP_CreateThread(&loaderThread, loaderMain, this, stack, sizeof(stack), 40) < 0)
        loaderThread = LWP_THREAD_NULL;
}

void BrowseHome::stopLoader()
{
    if (loaderThread == LWP_THREAD_NULL) return;
    stopReq = true;
    LWP_JoinThread(loaderThread, nullptr);
    loaderThread = LWP_THREAD_NULL;
}

void* BrowseHome::loaderMain(void* self)
{
    static_cast<BrowseHome*>(self)->loaderLoop();
    return nullptr;
}

/* Tiles worth keeping around a row's scroll position (long catalogue rows
 * would not fit in memory otherwise). */
void BrowseHome::tileWindow(const Row& r, int& first, int& last) const
{
    first = (int)r.scroll - 6;
    last  = (int)r.scroll + fullyVisible(r) + 10;
}

/* Next tile to fetch: what is on screen first (focused row, then the one
 * below, then the one above), then the rest of those rows. */
bool BrowseHome::pickNext(int& outR, int& outI)
{
    if (expanded) {   /* the page's tiles only: r -1 */
        int first, last;
        expWindow(first, last);
        for (int i = first; i <= last && i < (int)exp.items.size(); ++i) {
            LWP_MutexLock(texLock);
            bool take = exp.st[i] == TILE_NONE;
            if (take) exp.st[i] = TILE_LOADING;
            LWP_MutexUnlock(texLock);
            if (take) { outR = -1; outI = i; return true; }
        }
        return false;
    }
    const int nRows = (int)rows.size();
    const int order[4] = { rowSel, rowSel + 1, rowSel - 1, rowSel + 2 };
    for (int pass = 0; pass < 2; ++pass) {
        for (int o = 0; o < 4; ++o) {
            int r = order[o];
            if (r < 0 || r >= nRows) continue;
            Row& row = rows[r];
            int n = (int)row.items.size();
            int first = (int)row.scroll - 1;
            int last  = (int)row.scroll + fullyVisible(row) + 1;
            int wFirst, wLast;
            tileWindow(row, wFirst, wLast);
            for (int i = 0; i < n; ++i) {
                if (i < wFirst || i > wLast) continue;
                bool onScreen = i >= first && i <= last;
                if (onScreen != (pass == 0)) continue;
                LWP_MutexLock(texLock);
                bool take = row.st[i] == TILE_NONE;
                if (take) row.st[i] = TILE_LOADING;
                LWP_MutexUnlock(texLock);
                if (take) { outR = r; outI = i; return true; }
            }
        }
    }
    return false;
}

void BrowseHome::loaderLoop()
{
    while (!stopReq) {
        int r, i;
        if (!pickNext(r, i)) { usleep(30 * 1000); continue; }
        Row& row = r < 0 ? exp : rows[r];
        const JellyfinItem& it = row.items[i];
        /* Episodes: the series poster fits a portrait tile better */
        const std::string& id = (it.type == "Episode" && !it.seriesId.empty()) ? it.seriesId : it.id;
        /* the size drawn: Jellyfin fits the picture inside it, so a smaller
         * or narrower request came back blurred once cropped */
        int h = (int)POSTER_H,
            w = row.wide ? (int)(POSTER_H * 16.0f / 9.0f) : row.square ? h : (int)POSTER_W;
        if (squareInTall(row.square, row.wide, it)) h = w;
        std::string bytes;
        client.getItemImageBytes(serverUrl, auth, id, w, h, bytes);
        /* seasons without artwork of their own: use the series poster */
        if (bytes.empty() && it.type == "Season" && !it.seriesId.empty())
            client.getItemImageBytes(serverUrl, auth, it.seriesId, w, h, bytes);
        GRRLIB_texImg* t = bytes.empty() ? nullptr
                         : loadJPEGTexture((const u8*)bytes.data(), (u32)bytes.size());
        LWP_MutexLock(texLock);
        if (row.st[i] == TILE_LOADING) {              /* not evicted meanwhile */
            row.tex[i] = t;
            row.st[i]  = t ? TILE_READY : TILE_FAILED;
            t = nullptr;
        }
        LWP_MutexUnlock(texLock);
        if (t) GRRLIB_FreeTexture(t);
    }
}

/* Keep textures for the rows around the focus only (memory: MPlayer needs
 * room when playback starts).  Runs on the main thread between frames, so
 * the GPU is done with the textures it frees. */
void BrowseHome::evictFarRows()
{
    if (expanded) {   /* the page's tiles around what it shows */
        int first, last;
        expWindow(first, last);
        LWP_MutexLock(texLock);
        for (int i = 0; i < (int)exp.st.size(); ++i) {
            if (exp.st[i] == TILE_NONE || (i >= first - 12 && i <= last + 12)) continue;
            if (exp.tex[i]) { GRRLIB_FreeTexture(exp.tex[i]); exp.tex[i] = nullptr; }
            exp.st[i] = TILE_NONE;
        }
        LWP_MutexUnlock(texLock);
    }
    for (int r = 0; r < (int)rows.size(); ++r) {
        Row& row = rows[r];
        bool keepRow = r >= rowSel - 1 && r <= rowSel + 2;
        int first, last;
        tileWindow(row, first, last);
        LWP_MutexLock(texLock);
        for (int i = 0; i < (int)row.st.size(); ++i) {
            if (row.st[i] == TILE_NONE) continue;
            if (keepRow && i >= first - 4 && i <= last + 4) continue;   /* hysteresis */
            if (row.tex[i]) { GRRLIB_FreeTexture(row.tex[i]); row.tex[i] = nullptr; }
            row.st[i] = TILE_NONE;
        }
        LWP_MutexUnlock(texLock);
    }
}

/* ---- Input -------------------------------------------------------------------- */

BrowseHome::Action BrowseHome::update(const ir_t& ir, bool& irMode)
{
    const int nRows = (int)rows.size();
    evictFarRows();
    if (expanded) return updateExpanded(ir, irMode);

    /* Animation (targets from the previous frame's selection) */
    rowAnim = Ui::approach(rowAnim, (float)rowSel, 0.22f);
    for (auto& r : rows) r.scroll = Ui::approach(r.scroll, targetScroll(r), 0.25f);

    bool up    = Input::isUpPressed(),   down  = Input::isDownPressed();
    bool left  = Input::isLeftPressed(), right = Input::isRightPressed();
    bool a     = Input::isAJustPressed();
    if (up || down || left || right) irMode = false;

    if (Input::is1Pressed()) return Action::Search;
    if (Input::isActionPressed()) return Action::Browse;
    /* 2: the focused row as a page (its part, in a row of parts; not the
     * Libraries row: nothing to ask the server) */
    if (Input::is2Pressed() && headerFocus < 0 && rowSel < nRows &&
        (!rows[rowSel].sections.empty() || !rows[rowSel].query.empty()))
        return Action::Expand;
    if (mode == Mode::Catalog && Input::isBackPressed()) return Action::Back;
    /* B on the home: the avatar, its menu open */
    if (mode == Mode::Home && Input::isBackPressed()) { headerFocus = 2; irMode = false; return Action::Profiles; }

    /* L / R (the GameCube's and Classic's triggers, the Wii Remote's -):
     * from the rows, to search (L) or the avatar (R); in the header, the
     * header's buttons in turn, round and round.  + stays Browse (above) */
    const bool l = Input::isLPressed(), r = Input::isRPressed();
    if (l || r) {
        if (headerFocus < 0) headerFocus = l ? 0 : 2;
        else                 headerFocus = (headerFocus + (l ? 2 : 1)) % 3;
        irMode = false;
        return Action::None;
    }

    /* ---- Pointer ---- */
    hoverHeader = -1;
    int  hoverTile = -1;
    bool hoverNextRow = false, hoverUpArrow = false, hoverLeft = false, hoverRight = false;
    if (ir.valid && irMode) {
        for (int i = 0; i < 2; ++i)
            if (ir.x >= pillX(i, mode == Mode::Catalog) && ir.x < pillX(i, mode == Mode::Catalog) + pillWidth(i, mode == Mode::Catalog) && ir.y >= PILL_Y && ir.y < PILL_Y + PILL_H)
                hoverHeader = i;
        if (fabsf(ir.x - AVATAR_CX) < AVATAR_R + 4 && fabsf(ir.y - AVATAR_CY) < AVATAR_R + 4)
            hoverHeader = 2;
        hoverUpArrow = rowSel > 0 && fabsf(ir.x - 320) < 20 && ir.y < 44;
        if (nRows > 0) {
            Row& row = rows[rowSel];
            float tw = tileW(row), st = stride(row);
            const float py = POSTER_Y + rowDrop(row);
            if (ir.y >= py && ir.y < py + POSTER_H) {
                hoverLeft  = ir.x < X0 - 4;
                hoverRight = ir.x > Ui::screenRight() - 24.0f;
                for (int i = 0; i < (int)row.items.size(); ++i) {
                    float x = X0 + (i - row.scroll) * st;
                    if (x < X0 - 2 || x + tw > Ui::screenRight() - 24.0f) continue;   /* only whole tiles */
                    if (ir.x >= x && ir.x < x + tw) hoverTile = i;
                }
            }
            hoverNextRow = rowSel + 1 < nRows && ir.y >= POSTER_Y + ROW_STRIDE - 26;
        }
        if (hoverHeader >= 0) headerFocus = hoverHeader;
        else if (ir.y > 44) headerFocus = -1;
        if (hoverTile >= 0) rows[rowSel].sel = hoverTile;
    }

    /* Pointer resting on a side arrow scrolls the row, one poster at a time */
    {
        int arrow = hoverLeft ? -1 : (hoverRight ? 1 : 0);
        unsigned long long now = ticks_to_millisecs(gettime());
        if (arrow != hoverArrow) { hoverArrow = arrow; arrowSince = now; arrowLast = 0; }
        if (arrow && nRows > 0 && headerFocus < 0 && now - arrowSince > 350 && now - arrowLast > 260) {
            arrowLast = now;
            scrollRow(rows[rowSel], arrow);
        }
    }

    /* ---- Header ---- */
    if (headerFocus >= 0) {
        /* past either end, round to the other */
        if (left)  headerFocus = (headerFocus + 2) % 3;
        if (right) headerFocus = (headerFocus + 1) % 3;
        if (down && nRows > 0) headerFocus = -1;
        if (a) {
            int f = headerFocus;
            if (!irMode || hoverHeader >= 0) {
                if (f == 0) return Action::Search;
                if (f == 1) return Action::Browse;
                return Action::Profiles;
            }
        }
        return Action::None;
    }
    if (nRows == 0) {
        if (up) headerFocus = 1;
        return Action::None;
    }

    /* ---- Rows ---- */
    Row& row = rows[rowSel];
    int n = (int)row.items.size();
    if (up) {
        if (rowSel > 0) rowSel--;
        else headerFocus = 1;
    }
    if (down && rowSel + 1 < nRows) rowSel++;
    if (left  && row.sel > 0)     row.sel--;
    if (right && row.sel < n - 1) row.sel++;
    if (left || right) followSelection(row);

    if (a) {
        if (hoverUpArrow)            { rowSel--; return Action::None; }
        if (hoverLeft)               { scrollRow(row, -1); arrowLast = ticks_to_millisecs(gettime()); return Action::None; }
        if (hoverRight)              { scrollRow(row, +1); arrowLast = ticks_to_millisecs(gettime()); return Action::None; }
        if (hoverNextRow && hoverTile < 0) { rowSel++; return Action::None; }
        if (!irMode || hoverTile >= 0) return Action::Open;
    }
    return Action::None;
}

/* ---- Drawing --------------------------------------------------------------- */

/* Five-pointed star centred on cx, cy. */
static void star(float cx, float cy, float rad, u32 col)
{
    float px[10], py[10];
    for (int i = 0; i < 10; ++i) {
        float a = -1.5707963f + i * 0.6283185f;
        float r = (i & 1) ? rad * 0.45f : rad;
        px[i] = cx + cosf(a) * r;
        py[i] = cy + sinf(a) * r;
    }
    for (int i = 0; i < 10; ++i) {
        int j = (i + 1) % 10;
        Ui::triangle(cx, cy, px[i], py[i], px[j], py[j], col);
    }
}

/* 0..5 stars with halves, from a 0..10 community rating. */
static float drawStars(float x, float cy, float rating10, u32 on, u32 off)
{
    const float R = 7, STEP = 17;
    float v = rating10 / 2.0f;
    for (int i = 0; i < 5; ++i) {
        float cx = x + R + i * STEP;
        star(cx, cy, R, off);
        float fill = v - i;
        if (fill >= 0.75f) {
            star(cx, cy, R, on);
        } else if (fill >= 0.25f) {
            Ui::clip(cx - R, cy - R, R, R * 2);
            star(cx, cy, R, on);
            Ui::clipReset();
        }
    }
    return 5 * STEP;
}

void BrowseHome::drawRow(int r, float offY, float focus)
{
    Row& row = rows[r];
    const float ws = WiiUtils::wsScaleX();
    const float tw = tileW(row), st = stride(row);
    const float y = POSTER_Y + rowDrop(row) + offY;
    const bool  focused = focus > 0.5f && headerFocus < 0;
    const Look  L = look();

    /* Row title (bold for the focused row); a row of parts adds the part
     * shown (the selection's, or the first poster's), and the name of each
     * part further along over its first poster, a line between parts */
    u32 tc = lerpCol(L.titleDim, L.title, focus);
    int n = (int)row.items.size();
    std::string head = row.title;
    int cur = -1;
    if (!row.sections.empty()) {
        int lead = (int)ceilf(row.scroll - 0.05f);
        cur = sectionOf(row, focus > 0.5f ? row.sel : (lead < 0 ? 0 : lead));
        if (cur >= 0) head += "  \xc2\xb7  " + row.sections[cur].label;
    }
    Ui::text(X0 - 2, y - 25, head.c_str(), 16, tc);
    if (focus > 0.5f) Ui::text(X0 - 1, y - 25, head.c_str(), 16, tc);
    const float headEnd = X0 - 2 + Ui::textWidth(head.c_str(), 16) + 18;
    for (int s = 0; s < (int)row.sections.size(); ++s) {
        const float sx = X0 + (row.sections[s].start - row.scroll) * st;
        if (sx > Ui::screenRight() || sx < Ui::screenLeft()) continue;
        if (row.sections[s].start > 0)
            GRRLIB_Rectangle(sx - GAP * ws * 0.5f - 1, y, 2, POSTER_H, Ui::alpha(L.titleDim, 0.6f), 1);
        if (s == cur || sx < headEnd) continue;
        std::string lab = fit(row.sections[s].label, 14, (int)(Ui::screenRight() - 24.0f - sx));
        Ui::text(sx, y - 23, lab.c_str(), 14, tc);
    }

    u32 tint = lerpCol(L.dimTint, 0xFFFFFFFF, focus);
    int first = (int)floorf(row.scroll) - 1;
    for (int pass = 0; pass < 2; ++pass) {           /* selected tile last */
        for (int i = first < 0 ? 0 : first; i < n; ++i) {
            float x = X0 + (i - row.scroll) * st;
            if (x > Ui::screenRight()) break;
            if (x + tw < Ui::screenLeft()) continue;
            bool sel = focused && i == row.sel;
            if (sel != (pass == 1)) continue;

            float dx = x, dy = y, dw = tw, dh = POSTER_H;
            if (sel) {                                 /* pop out a little */
                float g = 5;
                dx -= g * ws * 0.5f; dy -= g * 0.5f; dw += g * ws; dh += g;
                Ui::shadow(dx - 3, dy - 3, dw + 6, dh + 6, 8, 10.0f, L.selShadow);
            } else {
                Ui::shadow(dx + 1, dy + 3, dw - 2, dh - 2, 5, 5.0f, L.tileShadow);
            }
            LWP_MutexLock(texLock);
            GRRLIB_texImg* tex = row.tex[i];
            LWP_MutexUnlock(texLock);
            if (tex && squareInTall(row.square, row.wide, row.items[i])) {
                const float sq = dw / ws;   /* as tall as it is wide, on screen */
                Ui::roundRect(dx, dy, dw, dh, 5, lerpCol(L.phTopDim, L.phTop, focus),
                              lerpCol(L.phBottomDim, L.phBottom, focus));
                Ui::texCover(tex, dx, dy + (dh - sq) * 0.5f, dw, sq, 3, 1.0f, tint);
            } else if (tex) {
                Ui::texCover(tex, dx, dy, dw, dh, 5, (dw / ws) / dh, tint);
            } else {
                Ui::roundRect(dx, dy, dw, dh, 5, lerpCol(L.phTopDim, L.phTop, focus),
                              lerpCol(L.phBottomDim, L.phBottom, focus));
                const JellyfinItem& it = row.items[i];
                const std::string& nm = (it.type == "Episode" || it.type == "Season") && !it.seriesName.empty()
                                        ? it.seriesName : it.name;
                std::string cap = fit(nm, 12, (int)dw - 10);
                Ui::textCentered(dx + dw * 0.5f, dy + dh * 0.5f - 7, cap.c_str(), 12,
                                 lerpCol(L.phTextDim, L.phText, focus));
            }
            const JellyfinItem& it = row.items[i];
            if (it.playbackPositionTicks > 0 && it.runtimeTicks > 0)
                Ui::progress(dx + 5, dy + dh - 9, dw - 10, 4,
                             (float)it.playbackPositionTicks / (float)it.runtimeTicks);
            if (sel) Ui::roundBorder(dx, dy, dw, dh, 5, 3.0f, L.selBorder);
            else     Ui::roundBorder(dx, dy, dw, dh, 5, 1.0f, L.tileBorder);
        }
    }

    /* Scroll arrows (white triangles on the sides) */
    if (focus > 0.5f) {
        float cy = y + POSTER_H * 0.5f;
        auto arrow = [&](bool leftSide) {
            bool hot = hoverArrow == (leftSide ? -1 : 1);
            float g = hot ? 1.35f : 1.0f;           /* grows under the pointer */
            float x = leftSide ? Ui::screenLeft() + 15.0f : Ui::screenRight() - 15.0f, s = leftSide ? -1.0f : 1.0f;
            if (hot) Ui::circle(x, cy, 16, Ui::alpha(L.arrow, 0.25f));
            Ui::triangle(x + s * 6 * g, cy, x - s * 6 * g, cy - 11 * g, x - s * 6 * g, cy + 11 * g, L.arrow);
        };
        if (row.scroll > 0.05f) arrow(true);
        if (row.scroll + fullyVisible(row) < n - 0.05f) arrow(false);
    }
}

void BrowseHome::drawInfoPanel(float k)
{
    const JellyfinItem* itp = selectedItem();
    if (!itp || k <= 0.01f) return;
    const JellyfinItem& it = *itp;
    /* a library: its picture says its name; A opens it, nothing to add */
    if (it.type == "CollectionFolder") return;
    const Row& row = rows[rowSel];

    /* Pointer tab from the selected poster into the panel */
    float cx = X0 + (row.sel - row.scroll) * stride(row) + tileW(row) * 0.5f;
    if (cx < INFO_X + 20) cx = INFO_X + 20;
    if (cx > INFO_X + INFO_W - 20) cx = INFO_X + INFO_W - 20;

    const Look L = look();
    Ui::shadow(INFO_X, INFO_Y + 2, INFO_W, INFO_H, 8, 6.0f, Ui::alpha(0x00000080, k * (L.band ? 1.0f : 0.5f)));
    Ui::roundRect(INFO_X, INFO_Y, INFO_W, INFO_H, 8, Ui::alpha(L.panelTop, k), Ui::alpha(L.panelBottom, k));
    Ui::roundBorder(INFO_X, INFO_Y, INFO_W, INFO_H, 8, 1.0f, Ui::alpha(L.panelBorder, k));
    Ui::triangle(cx - 10, INFO_Y + 1, cx + 10, INFO_Y + 1, cx, INFO_Y - 9, Ui::alpha(L.panelTop, k));

    /* Title */
    std::string title = it.name;
    if ((it.type == "Episode" || it.type == "Season") && !it.seriesName.empty()) title = it.seriesName;
    title = fit(title, 17, (int)INFO_W - 32);
    Ui::text(INFO_X + 16, INFO_Y + 6, title.c_str(), 17, Ui::alpha(L.panelText, k));

    /* Stars, rating chip, details */
    float x = INFO_X + 16, cy = INFO_Y + 38;
    if (it.communityRating > 0.0f) x += drawStars(x, cy, it.communityRating, L.starOn, L.starOff) + 10;
    if (!it.officialRating.empty()) {
        float w = Ui::textWidth(it.officialRating.c_str(), 12) + 12;
        Ui::roundBorder(x, cy - 9, w, 18, 3, 1.5f, Ui::alpha(L.panelMeta, k));
        Ui::textCentered(x + w * 0.5f, cy - 7, it.officialRating.c_str(), 12, Ui::alpha(L.panelMeta, k));
        x += w + 12;
    }
    char meta[160] = "";
    if (it.type == "Series") {
        if (it.recursiveItemCount > 0)
            snprintf(meta, sizeof(meta), it.recursiveItemCount > 1 ? tr("%d episodes") : tr("%d episode"),
                     it.recursiveItemCount);
        else if (it.childCount > 0)
            snprintf(meta, sizeof(meta), it.childCount > 1 ? tr("%d seasons") : tr("%d season"), it.childCount);
        if (it.year > 0) {
            size_t l = strlen(meta);
            snprintf(meta + l, sizeof(meta) - l, "%s%d", l ? "   \xc2\xb7   " : "", it.year);
        }
    } else if (it.type == "Season") {
        /* e.g. "Season 2  ·  12 episodes  ·  2026" */
        int eps = it.recursiveItemCount > 0 ? it.recursiveItemCount : it.childCount;
        snprintf(meta, sizeof(meta), "%s", it.name.c_str());
        size_t l = strlen(meta);
        if (eps > 0) {
            snprintf(meta + l, sizeof(meta) - l, "   \xc2\xb7   ");
            l = strlen(meta);
            snprintf(meta + l, sizeof(meta) - l, eps > 1 ? tr("%d episodes") : tr("%d episode"), eps);
        }
        l = strlen(meta);
        if (it.year > 0) snprintf(meta + l, sizeof(meta) - l, "   \xc2\xb7   %d", it.year);
    } else if (it.type == "Episode") {
        /* TRANSLATORS: season and episode numbers, then the episode's title */
        snprintf(meta, sizeof(meta), tr("S%d E%d  \xc2\xb7  %s"), it.seasonNumber, it.episodeNumber, it.name.c_str());
    } else {
        int mins = (int)(it.runtimeTicks / 600000000LL);
        if (it.year > 0 && mins > 0) {
            snprintf(meta, sizeof(meta), "%d   \xc2\xb7   %s", it.year, Lang::runTime(mins).c_str());
        } else if (it.year > 0)
            snprintf(meta, sizeof(meta), "%d", it.year);
        else if (mins > 0)
            snprintf(meta, sizeof(meta), "%s", Lang::runTime(mins).c_str());
    }
    if (it.playbackPositionTicks > 0 && it.runtimeTicks > 0) {
        int left = (int)((it.runtimeTicks - it.playbackPositionTicks) / 600000000LL);
        size_t l = strlen(meta);
        if (left > 0) {
            snprintf(meta + l, sizeof(meta) - l, "%s", l ? "   \xc2\xb7   " : "");
            l = strlen(meta);
            snprintf(meta + l, sizeof(meta) - l, tr("%d min left"), left);
        }
    }
    if (meta[0]) {
        std::string m = fit(meta, 14, (int)(INFO_X + INFO_W - 16 - x));
        Ui::text(x, cy - 8, m.c_str(), 14, Ui::alpha(L.panelMeta, k));
    }
}

void BrowseHome::drawHeader()
{
    const Look L = look();
    if (L.band) {
        Ui::drawHeaderBand();
    } else {
        /* no band: a plain strip of background, edge to edge, so rows that
         * scroll up don't show under the header (the row band right below
         * draws its own edge) */
        GRRLIB_Rectangle(Ui::screenLeft(), 0, Ui::screenWidth(), 46, Ui::pal().bgTop, 1);
    }

    /* Logo, top left.  On the red Flix band it sits on a white glossy pill
     * like the search/browse buttons (its grey would vanish on the red). */
    if (s_logo) {
        const float h = 30, w = h * s_logo->w / s_logo->h * WiiUtils::wsScaleX();
        float x = Ui::screenLeft() + 20, y = 7;
        if (L.band) {
            const float px = x - 4, py = y - 1, pw = w + 14, ph = h + 2;
            Ui::shadow(px, py + 2, pw, ph, ph * 0.5f, 4.0f, 0x00000070);
            Ui::roundRect(px, py, pw, ph, ph * 0.5f, 0xFFFFFFFF, 0xDCDCDCFF);
            Ui::roundBorder(px, py, pw, ph, ph * 0.5f, 1.5f, 0x5E5E5EFF);
            x += 3;
        }
        GRRLIB_DrawImg(x, y, s_logo, 0, w / s_logo->w, h / s_logo->h, 0xFFFFFFFF);
    }

    /* "More rows above" arrow */
    if (rowSel > 0)
        Ui::triangle(320, 13, 332, 29, 308, 29, L.band ? 0xFFFFFFE8 : L.arrow);

    /* search / browse pills */
    for (int i = 0; i < 2; ++i) {
        float x = pillX(i, mode == Mode::Catalog), w = pillWidth(i, mode == Mode::Catalog);
        bool f = headerFocus == i;
        if (f) Ui::shadow(x - 3, PILL_Y - 3, w + 6, PILL_H + 6, PILL_H * 0.5f + 3, 8.0f, L.pillGlow);
        else   Ui::shadow(x, PILL_Y + 2, w, PILL_H, PILL_H * 0.5f, 4.0f, L.tileShadow);
        Ui::roundRect(x, PILL_Y, w, PILL_H, PILL_H * 0.5f, L.pillTop, L.pillBottom);
        Ui::roundBorder(x, PILL_Y, w, PILL_H, PILL_H * 0.5f, f ? 2.0f : 1.5f, f ? L.pillFocus : L.pillBorder);
        float tx = x + w * 0.5f;
        if (i == 0) {
            /* magnifier */
            float mx = x + 15, my = PILL_Y + 12;
            Ui::roundBorder(mx - 5, my - 5, 10, 10, 5, 2.0f, L.pillText);
            Ui::triangle(mx + 3, my + 4, mx + 5, my + 2, mx + 9, my + 8, L.pillText);
            Ui::triangle(mx + 3, my + 4, mx + 9, my + 8, mx + 7, my + 10, L.pillText);
            tx += 7;
        }
        Ui::textCentered(tx, PILL_Y + 4, pillLabel(i, mode == Mode::Catalog), 15, L.pillText);
    }

    /* profile avatar */
    {
        bool f = headerFocus == 2;
        Ui::avatar(AVATAR_CX, AVATAR_CY, f ? AVATAR_R + 1 : AVATAR_R, userName.c_str(), f ? 1.0f : 0.0f);
    }
}

void BrowseHome::render(const ir_t& ir)
{
    if (expanded) { renderExpanded(ir); return; }
    Ui::background();

    /* Focused-row band */
    const Look L = look();
    Ui::roundRect(Ui::screenLeft() - 20, BAND_Y, Ui::screenWidth() + 40, bandHeight(), 16, L.bandTop, L.bandBottom);
    Ui::roundRect(Ui::screenLeft() - 20, BAND_Y, Ui::screenWidth() + 40, 1.5f, 0.75f, L.bandEdge);

    if (rows.empty()) {
        Ui::textCentered(320, 150, tr("Nothing to show yet"), 20, L.title);
        Ui::textCentered(320, 180, tr("Use \"browse\" to open your libraries."), 14, L.titleDim);
    }

    /* Rows around the focus, sliding with rowAnim */
    for (int r = rowSel - 2; r <= rowSel + 2; ++r) {
        if (r < 0 || r >= (int)rows.size()) continue;
        float d = r - rowAnim;
        float off = d * ROW_STRIDE;
        if (off < -ROW_STRIDE - 40 || off > 480 - POSTER_Y + 30) continue;
        float focus = 1.0f - fabsf(d);
        if (focus < 0) focus = 0;
        drawRow(r, off, focus);
    }

    /* Info panel once the rows have settled */
    float settle = 1.0f - fabsf(rowAnim - rowSel) * 4.0f;
    if (headerFocus < 0) drawInfoPanel(settle < 0 ? 0 : settle);

    /* Fade the peeking row into the floor */
    {
        const u32 c = L.floor;
        GRRLIB_Rectangle(Ui::screenLeft(), 470, Ui::screenWidth(), 10, c, 1);
        for (int i = 0; i < 16; ++i) {
            float y = 386 + i * 5.25f;
            GRRLIB_Rectangle(Ui::screenLeft(), y, Ui::screenWidth(), 5.25f, Ui::alpha(c, (i + 1) / 17.0f), 1);
        }
    }

    /* Button hints along the bottom */
    {
        const Ui::Hint hHome[] = { { "A", N_("Open") }, { "2", N_("Expand") }, { "1", N_("Search") }, { "+!", N_("Browse") } };
        const Ui::Hint hCat[]  = { { "A", N_("Open") }, { "2", N_("Expand") }, { "1", N_("Search") }, { "B", N_("Home") } };
        Ui::Hint h[4];
        int nh = 0;
        const bool canExpand = rowSel < (int)rows.size() &&
                               (!rows[rowSel].sections.empty() || !rows[rowSel].query.empty());
        for (const Ui::Hint& e : mode == Mode::Catalog ? hCat : hHome)
            if (canExpand || strcmp(e.button, "2") != 0) h[nh++] = e;
        float w = 0;
        for (int i = 0; i < nh; ++i) w += Ui::hintWidth(h[i]);
        float x = Ui::screenRight() - 14 - w;
        for (int i = 0; i < nh; ++i) x += Ui::hint(x, 454, h[i]);
    }

    drawHeader();
}

/* ---- A row as a page (expand) ---------------------------------------------- */

static const int   EXPAND_ITEMS = 100;
static const float EXP_Y = 58;                               /* top of the first line   */
static const float EXP_W = 80, EXP_H = 120, EXP_GAP = 10;    /* grid tiles (2:3)        */
static const float EXP_STRIDE = EXP_H + 10;
static const int   EXP_GRID_LINES = 3;
static const int   EXP_LIST_ROWS = 10, EXP_LIST_H = 36;
static const float EXP_COVER_W = 150;                        /* List + Posters: the cover */
/* a row of parts: the parts listed down the left, any one a press away */
static const float SIDE_W = 130, SIDE_H = 34;
static const int   SIDE_ROWS = 11;

int BrowseHome::expView() { return (int)Ui::libraryStyle(); }

/* the page's content, right of the parts' list if there is one */
float BrowseHome::expLeft() const
{
    return expSecs.empty() ? 40.0f : Ui::screenLeft() + 28.0f + SIDE_W + 14.0f;
}

float BrowseHome::expRight() const { return expSecs.empty() ? 600.0f : Ui::screenRight() - 40.0f; }

float BrowseHome::expListW() const
{
    float w = expRight() - expLeft();
    return expView() == 2 ? w - EXP_COVER_W - 26.0f : w;
}

int BrowseHome::expCols() const
{
    if (expView() != 0) return 1;
    const float ws = WiiUtils::wsScaleX();
    int c = (int)((expRight() - expLeft() + EXP_GAP * ws) / ((EXP_W + EXP_GAP) * ws));
    return c < 1 ? 1 : c;
}

int BrowseHome::expLines() const { return expView() == 0 ? EXP_GRID_LINES : EXP_LIST_ROWS; }

/* the grid: the tiles shown; a list: none, or the cover beside it */
void BrowseHome::expWindow(int& first, int& last) const
{
    const int v = expView();
    if (v == 0)      { first = expTop * expCols(); last = first + expCols() * expLines() - 1; }
    else if (v == 2) { first = last = exp.sel; }
    else             { first = 0; last = -1; }
}

void BrowseHome::expFollow()
{
    const int cols = expCols(), lines = expLines(), line = exp.sel / cols;
    if (line < expTop) expTop = line;
    if (line >= expTop + lines) expTop = line - lines + 1;
    if (expTop < 0) expTop = 0;
}

void BrowseHome::expand()
{
    /* open already: the part picked in the list on the left */
    if (expanded) {
        if (sideSel < 0 || sideSel >= (int)expSecs.size() || sideSel == expSec) return;
        freeTextures(exp);
        expSec     = sideSel;
        exp.title  = expBase + " \xc2\xb7 " + expSecs[expSec].label;
        exp.query  = expSecs[expSec].query;
        exp.items.clear(); exp.tex.clear(); exp.st.clear();
        exp.sel    = 0;
        fetchExpanded();
        return;
    }
    if (rowSel < 0 || rowSel >= (int)rows.size()) return;
    const Row& r = rows[rowSel];
    std::string title = r.title, query = r.query;
    int s = sectionOf(r, r.sel);
    if (s >= 0) { title += " \xc2\xb7 " + r.sections[s].label; query = r.sections[s].query; }
    if (query.empty()) return;
    freeTextures(exp);
    exp = Row();
    exp.title  = title;
    exp.query  = query;
    exp.square = r.square;
    expBase    = r.title;
    expSecs.clear();
    if (!r.sections.empty() && !r.query.empty())   /* first: every library at once */
        expSecs.push_back({ 0, tr("All"), r.query });
    const int skip = (int)expSecs.size();
    expSecs.insert(expSecs.end(), r.sections.begin(), r.sections.end());
    expSec     = sideSel = s < 0 ? -1 : s + skip;
    sideTop    = 0;
    sideFocus  = false;
    if (r.sel >= 0 && r.sel < (int)r.items.size()) {   /* the selection stays on it */
        exp.items.push_back(r.items[r.sel]);
        exp.tex.assign(1, nullptr);
        exp.st.assign(1, TILE_NONE);
    }
    expanded = true;
    fetchExpanded();
}

/* the row's query for more of it (Limit=EXPAND_ITEMS), the selection kept */
void BrowseHome::fetchExpanded()
{
    std::string keep = exp.sel >= 0 && exp.sel < (int)exp.items.size() ? exp.items[exp.sel].id : "";
    freeTextures(exp);
    std::string q = exp.query;
    char lim[24];
    snprintf(lim, sizeof(lim), "Limit=%d", EXPAND_ITEMS);
    size_t p = q.find("Limit=");
    if (p != std::string::npos) {
        size_t e = q.find('&', p);
        q.replace(p, e == std::string::npos ? std::string::npos : e - p, lim);
    } else {
        q += (q.find('?') == std::string::npos ? "?" : "&");
        q += lim;
    }
    std::vector<JellyfinItem> got;
    client.getItemsByQuery(serverUrl, auth, q, got);
    if ((int)got.size() > EXPAND_ITEMS) got.resize(EXPAND_ITEMS);
    exp.items = std::move(got);
    exp.tex.assign(exp.items.size(), nullptr);
    exp.st.assign(exp.items.size(), TILE_NONE);
    exp.sel = 0;
    for (int i = 0; i < (int)exp.items.size(); ++i)
        if (exp.items[i].id == keep) { exp.sel = i; break; }
    expTop = 0;
    expFollow();
}

void BrowseHome::collapse()
{
    stopLoader();
    freeTextures(exp);
    exp = Row();
    expSecs.clear();
    expanded = false;
    sideFocus = false;
    startLoader();
}

BrowseHome::Action BrowseHome::updateExpanded(const ir_t& ir, bool& irMode)
{
    const int n = (int)exp.items.size(), nSec = (int)expSecs.size();
    bool up    = Input::isUpPressed(),   down  = Input::isDownPressed();
    bool left  = Input::isLeftPressed(), right = Input::isRightPressed();
    bool a     = Input::isAJustPressed();
    if (up || down || left || right) irMode = false;

    if (Input::isBackPressed()) { collapse(); return Action::None; }
    /* 1: the next library view (Posters, List, List + Posters) */
    if (Input::is1Pressed()) {
        Ui::setLibraryStyle((Ui::LibraryStyle)((expView() + 1) % Ui::LIBRARY_STYLE_COUNT));
        expTop = 0;
        expFollow();
        return Action::None;
    }

    /* pointer: the parts on the left, or the page */
    int hover = -1, hoverSide = -1;
    const int cols = expCols(), page = cols * expLines();
    const float ws = WiiUtils::wsScaleX();
    if (ir.valid && irMode) {
        const float sx = Ui::screenLeft() + 28.0f;
        if (nSec > 0 && ir.x >= sx && ir.x < sx + SIDE_W && ir.y >= EXP_Y) {
            int i = sideTop + (int)((ir.y - EXP_Y) / SIDE_H);
            if (i - sideTop < SIDE_ROWS && i < nSec) hoverSide = i;
        }
        if (expView() == 0) {
            const float tw = EXP_W * ws, gap = EXP_GAP * ws;
            const float x0 = (expLeft() + expRight()) * 0.5f - (cols * tw + (cols - 1) * gap) * 0.5f;
            for (int l = 0; l < expLines(); ++l)
                for (int c = 0; c < cols; ++c) {
                    int i = (expTop + l) * cols + c;
                    float x = x0 + c * (tw + gap), y = EXP_Y + l * EXP_STRIDE;
                    if (i < n && ir.x >= x && ir.x < x + tw && ir.y >= y && ir.y < y + EXP_H) hover = i;
                }
        } else if (ir.x >= expLeft() && ir.x < expLeft() + expListW() && ir.y >= EXP_Y) {
            int i = expTop + (int)((ir.y - EXP_Y) / EXP_LIST_H);
            if (i - expTop < EXP_LIST_ROWS && i < n) hover = i;
        }
        if (hoverSide >= 0) { sideSel = hoverSide; sideFocus = true; }
        if (hover >= 0)     { exp.sel = hover; sideFocus = false; }
    }

    /* the parts: Up/Down to one, A (or Right) shows it */
    if (sideFocus) {
        if (up   && sideSel > 0)        sideSel--;
        if (down && sideSel < nSec - 1) sideSel++;
        if (sideSel < sideTop) sideTop = sideSel;
        if (sideSel >= sideTop + SIDE_ROWS) sideTop = sideSel - SIDE_ROWS + 1;
        if ((right && n > 0) || Input::isLPressed() || Input::isRPressed()) { sideFocus = false; sideSel = expSec; }
        if (a && (!irMode || hoverSide >= 0)) {
            sideFocus = false;
            if (sideSel != expSec) return Action::Expand;   /* expand(): that part */
        }
        return Action::None;
    }
    if (n == 0) {
        if (left && nSec > 0) sideFocus = true;
        return Action::None;
    }

    int sel = exp.sel;
    if (Input::isLPressed()) sel -= page;
    if (Input::isRPressed()) sel += page;
    if (expView() == 0) {
        /* Left past the first column: to the parts */
        if (left && nSec > 0 && sel % cols == 0) { sideFocus = true; sideSel = expSec; return Action::None; }
        if (left)  sel--;
        if (right) sel++;
        if (up    && sel - cols >= 0) sel -= cols;
        if (down) sel = sel + cols < n ? sel + cols : (sel / cols < (n - 1) / cols ? n - 1 : sel);
    } else {
        if (left && nSec > 0) { sideFocus = true; sideSel = expSec; return Action::None; }
        if (up)    sel--;
        if (down)  sel++;
        if (left)  sel -= page;
        if (right) sel += page;
    }
    if (sel < 0) sel = 0;
    if (sel > n - 1) sel = n - 1;
    if (sel != exp.sel) { exp.sel = sel; expFollow(); }

    if (a && (!irMode || hover >= 0)) return Action::Open;
    return Action::None;
}

/* what a tile or a line is called: an episode by its series too */
static std::string expName(const JellyfinItem& it)
{
    if (it.type == "Episode" && !it.seriesName.empty()) return it.seriesName + " \xc2\xb7 " + it.name;
    if (it.type == "Season" && !it.seriesName.empty())  return it.seriesName + " \xc2\xb7 " + it.name;
    return it.name;
}

void BrowseHome::renderExpanded(const ir_t& ir)
{
    (void)ir;
    Ui::background();
    const Ui::Palette& p = Ui::pal();
    const Look L = look();
    const int n = (int)exp.items.size(), cols = expCols(), lines = expLines(), v = expView();
    const float ws = WiiUtils::wsScaleX();

    char pos[24];
    snprintf(pos, sizeof(pos), "%d / %d", n > 0 ? exp.sel + 1 : 0, n);
    Ui::header(fit(exp.title, 24, 440).c_str(), pos);

    /* the parts of the row, the one shown marked */
    if (!expSecs.empty()) {
        const float sx = Ui::screenLeft() + 28.0f;
        for (int r = 0; r < SIDE_ROWS; ++r) {
            int i = sideTop + r;
            if (i >= (int)expSecs.size()) break;
            float y = EXP_Y + r * SIDE_H;
            bool cur = i == expSec, f = sideFocus && i == sideSel;
            Ui::card(sx, y + 2, SIDE_W, SIDE_H - 4, 8, f ? 1.0f : 0.0f);
            if (cur) Ui::roundRect(sx + 4, y + 9, 4, SIDE_H - 18, 2, p.accent);
            std::string lab = fit(expSecs[i].label, 14, (int)SIDE_W - 24);
            Ui::text(sx + 14, y + 9, lab.c_str(), 14, cur || f ? p.text : p.textDim);
            if (cur) Ui::text(sx + 15, y + 9, lab.c_str(), 14, p.text);
        }
        if ((int)expSecs.size() > SIDE_ROWS)
            Ui::scrollbar(sx + SIDE_W + 4, EXP_Y + 2, SIDE_ROWS * SIDE_H - 4, sideTop, SIDE_ROWS, (int)expSecs.size());
    }

    if (n == 0) Ui::textCentered((expLeft() + expRight()) * 0.5f, 200, tr("Nothing to show yet"), 16, p.textDim);

    auto texOf = [&](int i) {
        LWP_MutexLock(texLock);
        GRRLIB_texImg* t = exp.tex[i];
        LWP_MutexUnlock(texLock);
        return t;
    };
    const bool contentFocus = !sideFocus;

    if (v == 0) {
        const float tw = EXP_W * ws, gap = EXP_GAP * ws;
        const float x0 = (expLeft() + expRight()) * 0.5f - (cols * tw + (cols - 1) * gap) * 0.5f;
        for (int pass = 0; pass < 2; ++pass)                 /* selected tile last */
            for (int l = 0; l < lines; ++l)
                for (int c = 0; c < cols; ++c) {
                    int i = (expTop + l) * cols + c;
                    if (i >= n) break;
                    bool sel = contentFocus && i == exp.sel;
                    if (sel != (pass == 1)) continue;
                    float dx = x0 + c * (tw + gap), dy = EXP_Y + l * EXP_STRIDE, dw = tw, dh = EXP_H;
                    if (sel) {
                        float g = 4;
                        dx -= g * ws * 0.5f; dy -= g * 0.5f; dw += g * ws; dh += g;
                        Ui::shadow(dx - 3, dy - 3, dw + 6, dh + 6, 8, 10.0f, L.selShadow);
                    } else {
                        Ui::shadow(dx + 1, dy + 3, dw - 2, dh - 2, 5, 5.0f, L.tileShadow);
                    }
                    const JellyfinItem& it = exp.items[i];
                    GRRLIB_texImg* tex = texOf(i);
                    if (tex && (exp.square || it.type == "MusicAlbum")) {
                        const float sq = dw / ws;
                        Ui::roundRect(dx, dy, dw, dh, 5, L.phTop, L.phBottom);
                        Ui::texCover(tex, dx, dy + (dh - sq) * 0.5f, dw, sq, 3, 1.0f);
                    } else if (tex) {
                        Ui::texCover(tex, dx, dy, dw, dh, 5, (dw / ws) / dh);
                    } else {
                        Ui::roundRect(dx, dy, dw, dh, 5, L.phTop, L.phBottom);
                        std::string cap = fit(it.type == "Episode" && !it.seriesName.empty() ? it.seriesName : it.name,
                                              12, (int)dw - 8);
                        Ui::textCentered(dx + dw * 0.5f, dy + dh * 0.5f - 7, cap.c_str(), 12, L.phText);
                    }
                    if (it.playbackPositionTicks > 0 && it.runtimeTicks > 0)
                        Ui::progress(dx + 5, dy + dh - 9, dw - 10, 4,
                                     (float)it.playbackPositionTicks / (float)it.runtimeTicks);
                    if (sel) Ui::roundBorder(dx, dy, dw, dh, 5, 3.0f, L.selBorder);
                    else     Ui::roundBorder(dx, dy, dw, dh, 5, 1.0f, L.tileBorder);
                }
        int total = (n + cols - 1) / cols;
        Ui::scrollbar(Ui::screenRight() - 18.0f, EXP_Y, lines * EXP_STRIDE - 10, expTop, lines, total);
    } else {
        const float lx = expLeft(), lw = expListW();
        for (int r = 0; r < lines; ++r) {
            int i = expTop + r;
            if (i >= n) break;
            const JellyfinItem& it = exp.items[i];
            float ry = EXP_Y + r * EXP_LIST_H;
            bool sel = i == exp.sel;
            Ui::card(lx, ry + 2, lw, EXP_LIST_H - 4, 9, sel && contentFocus ? 1.0f : 0.0f);
            float rightW = 0;
            if (it.year > 0) {
                char y[12];
                snprintf(y, sizeof(y), "%d", it.year);
                Ui::textRight(lx + lw - 12, ry + 10, y, 14, p.textDim);
                rightW = Ui::textWidth(y, 14) + 12;
            }
            std::string t = fit(expName(it), 16, (int)(lw - 28 - rightW));
            Ui::text(lx + 14, ry + 9, t.c_str(), 16, sel && contentFocus ? p.accentDark : p.text);
            if (it.playbackPositionTicks > 0 && it.runtimeTicks > 0)
                Ui::progress(lx + 14, ry + EXP_LIST_H - 9, 60, 3,
                             (float)it.playbackPositionTicks / (float)it.runtimeTicks);
        }
        Ui::scrollbar(lx + lw + 8, EXP_Y + 2, lines * EXP_LIST_H - 4, expTop, lines, n);

        /* List + Posters: the selected title's cover beside the list */
        if (v == 2 && exp.sel >= 0 && exp.sel < n) {
            const JellyfinItem& it = exp.items[exp.sel];
            const float PX = lx + lw + 26, PW = expRight() - PX, PY = EXP_Y + 2;
            const bool sq = exp.square || it.type == "MusicAlbum";
            const float PH = sq ? PW : PW * 1.5f;
            GRRLIB_texImg* tex = texOf(exp.sel);
            if (tex) Ui::texCover(tex, PX, PY, PW, PH, 6, PW / ws / PH);
            else {
                Ui::roundRect(PX, PY, PW, PH, 6, L.phTop, L.phBottom);
                Ui::textCentered(PX + PW * 0.5f, PY + PH * 0.5f - 7, fit(it.name, 13, (int)PW - 10).c_str(), 13, L.phText);
            }
            Ui::text(PX, PY + PH + 10, fit(expName(it), 15, (int)PW).c_str(), 15, p.text);
            int mins = (int)(it.runtimeTicks / 600000000LL);
            char meta[64] = "";
            if (it.type == "Episode")
                snprintf(meta, sizeof(meta), tr("S%d E%d"), it.seasonNumber, it.episodeNumber);
            else if (mins > 0)
                snprintf(meta, sizeof(meta), "%s", Lang::runTime(mins).c_str());
            if (it.year > 0) {
                size_t l = strlen(meta);
                snprintf(meta + l, sizeof(meta) - l, "%s%d", l ? "  \xc2\xb7  " : "", it.year);
            }
            if (meta[0]) Ui::text(PX, PY + PH + 30, meta, 13, p.textDim);
        }
    }

    static const char* const VIEWS[Ui::LIBRARY_STYLE_COUNT] = { N_("Posters"), N_("List"), N_("List + Posters") };
    const Ui::Hint l[] = { { "A", sideFocus ? N_("Show") : N_("Open") }, { "B", N_("Back") }, { "1", VIEWS[v] } };
    const Ui::Hint r[] = { { "-/+", N_("Page") } };
    std::string cap = v == 0 && contentFocus && exp.sel >= 0 && exp.sel < n ? fit(expName(exp.items[exp.sel]), 14, 240) : "";
    Ui::footer(l, 3, r, n > cols * lines && !sideFocus ? 1 : 0, cap.empty() ? nullptr : cap.c_str());
}
