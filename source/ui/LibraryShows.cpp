/* LibraryView: TV series: season and episode lists. */
#include "LibraryView.h"
#include "LibraryDraw.h"
#include "Ui.h"
#include "../input/Input.h"
#include "../core/Lang.h"
#include "../core/SoundFX.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <algorithm>

using namespace LibDraw;

/* Seasons as posters (Posters view): two rows of four, the name under
 * each, centred when there are fewer than four */
static const int SG_COLS = 4, SG_ROWS = 2, SG_W = 112, SG_H = 160;
static const int SG_STRIDE_X = 150, SG_STRIDE_Y = 188, SG_Y0 = 56;
static int seasonGridX0(int n)
{
    int cols = n < SG_COLS ? (n > 0 ? n : 1) : SG_COLS;
    return (640 - ((cols - 1) * SG_STRIDE_X + SG_W)) / 2;
}
/* the List + Posters view: the list narrower, the poster on its right */
static const int SEASON_LW = 384;

/* the series' page: its poster where A plays it from the start */
static const float SP_X = 40, SP_Y = 62, SP_W = 170, SP_H = 255;
static const float PAGE_H = 420;   /* the overview over the seasons: one page down */

/* the first episode of its first season (not the specials), played; the
 * season's others after it */
bool LibraryView::startSeriesFromStart()
{
    std::string sid;
    for (const JellyfinSeason& se : seasons)
        if (se.id != SPECIALS_ROW && se.indexNumber > 0) { sid = se.id; break; }
    if (sid.empty())
        for (const JellyfinSeason& se : seasons) if (se.id != SPECIALS_ROW) { sid = se.id; break; }
    if (sid.empty()) return false;
    std::vector<JellyfinEpisode> eps;
    bool ok = false;
    std::string err;
    freeDetail();
    runWithLoading([&]() {
        ok = client.getEpisodes(serverUrl, auth, currentSeriesId, sid, eps);
        if (ok && !eps.empty()) ok = client.getItemDetail(serverUrl, auth, eps[0].id, detail);
        if (!ok) err = client.lastError();
    });
    if (!ok) { errMsg = err; state = State::Error; return false; }
    if (eps.empty()) return false;
    SYS_Report("[Series] from the start: %s\n", eps[0].name.c_str());
    detailItemId    = eps[0].id;
    detailIsEpisode = true;
    preparePlay(0LL, &eps);
    return true;
}

bool LibraryView::updateSeasons(ir_t& ir, bool aPressed) {
    if (seasonAsk) { updateSeasonAsk(ir); return false; }
    int n = (int)seasons.size();
    if (Input::isBackPressed()) {
        freeSeasonPosters();
        state = seasonsCallerState;
        return false;
    }
    seriesScrollK = Ui::approach(seriesScrollK, seriesTop ? 0.0f : 1.0f, 0.22f);
    if (seriesTop) {
        /* the overview: A on the poster plays the series from the start,
         * Down (or the pointer low) to the seasons */
        const bool onPoster = ir.valid && ir.x >= SP_X && ir.x < SP_X + SP_W && ir.y >= SP_Y && ir.y < SP_Y + SP_H;
        if (Input::isDownPressed() || (aPressed && ir.valid && ir.y > PAGE_H - 40)) { seriesTop = false; irMode = false; return false; }
        if (Input::is2Pressed() && n > 0) return startShuffle("");
        if (Input::is1Pressed()) toggleFavorite(currentSeriesId, seriesFavorite);
        if (aPressed && (!ir.valid || onPoster) && n > 0) return startSeriesFromStart();
        return false;
    }
    /* back up to the overview: Up from the first row, or A on the header */
    {
        const bool firstRow = seasonGrid() ? seasonSel < SG_COLS : seasonSel == 0;
        if ((Input::isUpPressed() && firstRow) || (aPressed && ir.valid && ir.y < 46)) {
            seriesTop = true;
            return false;
        }
    }
    if (seasonGrid()) {
        const bool moved = Input::isLeftPressed() || Input::isRightPressed() ||
                           Input::isUpPressed() || Input::isDownPressed();
        if (Input::isLeftPressed()  && seasonSel % SG_COLS > 0) seasonSel--;
        if (Input::isRightPressed() && seasonSel % SG_COLS < SG_COLS - 1 && seasonSel + 1 < n) seasonSel++;
        if (Input::isUpPressed()    && seasonSel >= SG_COLS) seasonSel -= SG_COLS;
        if (Input::isDownPressed()  && seasonSel / SG_COLS < (n - 1) / SG_COLS)
            seasonSel = seasonSel + SG_COLS < n ? seasonSel + SG_COLS : n - 1;   /* a shorter last row */
        if (moved) irMode = false;
        if (irMode && ir.valid && !moved) {
            const int x0 = seasonGridX0(n);
            for (int k = 0; k < SG_COLS * SG_ROWS && seasonTop + k < n; k++) {
                int px = x0 + (k % SG_COLS) * SG_STRIDE_X, py = SG_Y0 + (k / SG_COLS) * SG_STRIDE_Y;
                if (ir.x >= px && ir.x < px + SG_W && ir.y >= py && ir.y < py + SG_H) seasonSel = seasonTop + k;
            }
        }
        /* scroll a row at a time */
        if (seasonSel < seasonTop) seasonTop = seasonSel / SG_COLS * SG_COLS;
        if (seasonSel >= seasonTop + SG_COLS * SG_ROWS) seasonTop = (seasonSel / SG_COLS - (SG_ROWS - 1)) * SG_COLS;
    } else {
        const int lw = Ui::libraryStyle() == Ui::LibraryStyle::ListCover ? SEASON_LW : LIST_W;
        if (Input::isUpPressed())   { if (seasonSel > 0)   { seasonSel--; clampSeasonScroll(); } irMode = false; }
        if (Input::isDownPressed()) { if (seasonSel < n-1) { seasonSel++; clampSeasonScroll(); } irMode = false; }
        if (irMode && ir.valid && !Input::isUpPressed() && !Input::isDownPressed()) {
            for (int i = 0; i < ITEMS_VISIBLE; i++) {
                int idx = seasonTop + i;
                if (idx >= n) break;
                int ry = LIST_Y + i * ROW_H;
                if (ir.x >= LIST_X && ir.x <= LIST_X + lw &&
                    ir.y >= ry && ir.y < ry + ROW_H) {
                    seasonSel = idx;
                    irMode = true;
                }
            }
        }
    }
    if (Input::is2Pressed() && n > 0) return startShuffle("");   /* whole series */
    if (Input::is1Pressed()) toggleFavorite(currentSeriesId, seriesFavorite);
    if (Input::isActionPressed() && n > 0 && seasonSel < n) {   /* +: the season watched, asked first */
        askSeasonWatched(seasons[seasonSel].id, seasons[seasonSel].name);
        return false;
    }
    if (aPressed && n > 0 && seasonSel < n && seasons[seasonSel].id == SPECIALS_ROW) {
        openSpecials(currentSeriesId, currentSeriesName, State::SeasonsReady);
        return false;
    }
    if (aPressed && n > 0 && seasonSel < n) {
        currentSeasonId   = seasons[seasonSel].id;
        currentSeasonName = seasons[seasonSel].name;
        episodes.clear(); episodeSel = 0; episodeTop = 0;
        state = State::EpisodesLoad;
    }
    return false;
}

/* The episodes: Up/Down, a page at a time with Left/Right or -/+ (seasons
 * of hundreds of episodes), 1 watched or not, 2 the options (sort,
 * search, the season marked watched, shuffle); B clears a search first */
bool LibraryView::updateEpisodes(ir_t& ir, bool aPressed) {
    if (seasonAsk) { updateSeasonAsk(ir); return false; }
    if (epPanel || epTyping) return updateEpisodePanel(ir);
    const int n = (int)episodeView.size();
    if (Input::isBackPressed()) {
        if (!episodeQuery.empty()) { episodeQuery.clear(); buildEpisodeView(); return false; }
        if (episodesFromHome) { episodesFromHome = false; state = State::LibsReady; }
        else                  state = State::SeasonsReady;
        return false;
    }
    const int was = episodeSel;
    if (Input::isUpPressed())   { if (episodeSel > 0)   episodeSel--; irMode = false; }
    if (Input::isDownPressed()) { if (episodeSel < n-1) episodeSel++; irMode = false; }
    if (Input::isLeftPressed() || Input::isLPressed()) {   /* a page up */
        episodeSel -= ITEMS_VISIBLE; episodeTop -= ITEMS_VISIBLE; irMode = false;
    }
    if (Input::isRightPressed() || Input::isRPressed()) {  /* a page down */
        episodeSel += ITEMS_VISIBLE; episodeTop += ITEMS_VISIBLE; irMode = false;
        if (episodeTop > n - ITEMS_VISIBLE) episodeTop = n - ITEMS_VISIBLE;
    }
    if (episodeSel != was || episodeTop < 0) clampEpisodeScroll();
    if (irMode && ir.valid && !Input::isUpPressed() && !Input::isDownPressed()) {
        for (int i = 0; i < ITEMS_VISIBLE; i++) {
            int idx = episodeTop + i;
            if (idx >= n) break;
            int ry = LIST_Y + i * ROW_H;
            if (ir.x >= LIST_X && ir.x <= LIST_X + LIST_W &&
                ir.y >= ry && ir.y < ry + ROW_H) {
                episodeSel = idx;
                irMode = true;
            }
        }
    }
    if (Input::is2Pressed() && !episodes.empty()) {
        epPanel = true;
        epPanelRow = 0;
        SoundFX::play(SoundFX::FX::Open);
        return false;
    }
    const int e = shownEpisode();
    if (Input::is1Pressed() && e >= 0) {   /* watched or not */
        togglePlayed(episodes[e].id, episodes[e].played);
        if (episodes[e].played) episodes[e].playbackPositionTicks = 0;
    }
    if (aPressed && e >= 0) {
        detailItemId = episodes[e].id;
        detailReturnState = State::EpisodesReady;
        state = State::DetailLoad;
    }
    return false;
}

/* ---------------------------------------------------------------
 * The episodes as shown: sorted one way or another, searched
 * --------------------------------------------------------------- */
enum EpisodeSort { ES_EPISODE, ES_TITLE, ES_LAST, ES_WATCHED, ES_RATING, ES_LENGTH, ES_COUNT };
static const char* const EP_SORTS[ES_COUNT] = {
    N_("Episode"), N_("Title A-Z"), N_("Last Watched"), N_("Watched Status"), N_("Rating"), N_("Length")
};
/* each starts the useful way round: the latest watched, the best rated, the longest first */
static const bool EP_DESC[ES_COUNT] = { false, false, true, false, true, true };

int LibraryView::shownEpisode() const
{
    return episodeSel >= 0 && episodeSel < (int)episodeView.size() ? episodeView[episodeSel] : -1;
}

void LibraryView::buildEpisodeView()
{
    const int keep = shownEpisode();
    std::string q = episodeQuery;
    for (char& c : q) c = (char)tolower((unsigned char)c);
    const bool number = !q.empty() && q.find_first_not_of("0123456789") == std::string::npos;
    episodeView.clear();
    for (int i = 0; i < (int)episodes.size(); ++i) {
        if (!q.empty()) {
            std::string name = episodes[i].name;
            for (char& c : name) c = (char)tolower((unsigned char)c);
            if (name.find(q) == std::string::npos && !(number && episodes[i].indexNumber == atoi(q.c_str())))
                continue;
        }
        episodeView.push_back(i);
    }
    const int sort = episodeSort;
    const bool desc = episodeDesc;
    std::stable_sort(episodeView.begin(), episodeView.end(), [&](int a, int b) {
        const JellyfinEpisode &x = episodes[a], &y = episodes[b];
        int k = 0;
        switch (sort) {
        case ES_TITLE:   k = strcasecmp(x.name.c_str(), y.name.c_str()); break;
        case ES_LAST:    /* never watched: at the end, either way round */
            if (x.lastPlayed.empty() != y.lastPlayed.empty()) return y.lastPlayed.empty();
            k = x.lastPlayed.compare(y.lastPlayed);
            break;
        case ES_WATCHED: k = (int)x.played - (int)y.played; break;
        case ES_RATING:  k = x.rating < y.rating ? -1 : x.rating > y.rating; break;
        case ES_LENGTH:  k = x.runTimeTicks < y.runTimeTicks ? -1 : x.runTimeTicks > y.runTimeTicks; break;
        default:         k = a - b; break;
        }
        if (desc) k = -k;
        return k ? k < 0 : a < b;   /* the season's order among equals */
    });
    episodeSel = 0;
    for (int i = 0; i < (int)episodeView.size(); ++i) if (episodeView[i] == keep) episodeSel = i;
    episodeTop = episodeSel - ITEMS_VISIBLE / 2;
    clampEpisodeScroll();
}

/* the options: 2 in the episodes */
static const float EPP_W = 400, EPP_X = 320 - EPP_W / 2, EPP_Y = 82, EPP_STEP = 42;
static const int   EPP_ROWS = 5;

bool LibraryView::updateEpisodePanel(ir_t& ir)
{
    if (epTyping) {   /* the search: the list follows each letter */
        epKb.setOrigin((640 - epKb.width()) * 0.5f, 214 - epKb.lift());
        const Keyboard::Result r = epKb.update(ir, epDraft, 40);
        if (epDraft != episodeQuery) { episodeQuery = epDraft; buildEpisodeView(); }
        if (r == Keyboard::Result::Enter)  { epTyping = false; epPanel = false; SoundFX::play(SoundFX::FX::Open); }
        if (r == Keyboard::Result::Cancel) { epTyping = false; SoundFX::play(SoundFX::FX::Back); }
        return false;
    }
    if (Input::isBackPressed()) { epPanel = false; SoundFX::play(SoundFX::FX::Back); return false; }
    const int was = epPanelRow;
    if (Input::isUpPressed())   epPanelRow = (epPanelRow + EPP_ROWS - 1) % EPP_ROWS;
    if (Input::isDownPressed()) epPanelRow = (epPanelRow + 1) % EPP_ROWS;
    int hover = -1;
    if (ir.valid && ir.x >= EPP_X + 16 && ir.x < EPP_X + EPP_W - 16)
        for (int i = 0; i < EPP_ROWS; ++i) {
            const float ry = EPP_Y + 50 + i * EPP_STEP;
            if (ir.y >= ry && ir.y < ry + 34) hover = i;
        }
    if (hover >= 0) epPanelRow = hover;
    if (epPanelRow != was) SoundFX::play(SoundFX::FX::Move);
    const int d = Input::isRightPressed() ? 1 : Input::isLeftPressed() ? -1 : 0;
    if (d && epPanelRow == 0) {
        episodeSort = (episodeSort + d + ES_COUNT) % ES_COUNT;
        episodeDesc = EP_DESC[episodeSort];
        buildEpisodeView();
        SoundFX::play(SoundFX::FX::Move);
    }
    if (d && epPanelRow == 1) { episodeDesc = !episodeDesc; buildEpisodeView(); SoundFX::play(SoundFX::FX::Move); }
    if (!Input::isAJustPressed() || (ir.valid && hover < 0)) return false;
    switch (epPanelRow) {
    case 2:
        epTyping = true;
        epDraft = episodeQuery;
        epKb.reset();
        SoundFX::play(SoundFX::FX::Select);
        return false;
    case 3:
        epPanel = false;
        askSeasonWatched(currentSeasonId, currentSeasonName);
        return false;
    case 4:
        epPanel = false;
        return startShuffle(currentSeasonId);
    default:
        epPanel = false;
        SoundFX::play(SoundFX::FX::Open);
        return false;
    }
}

static const char* orderName(int sort, bool desc)
{
    if (sort == ES_WATCHED) return desc ? N_("Watched first") : N_("Not watched first");
    return desc ? N_("Descending") : N_("Ascending");
}

void LibraryView::renderEpisodePanel(ir_t& ir)
{
    const Ui::Palette& p = Ui::pal();
    if (epTyping) {
        GRRLIB_Rectangle(Ui::screenLeft(), 0, Ui::screenWidth(), 480, p.dim, 1);
        Ui::card((640 - epKb.width()) * 0.5f - 10, 104 - epKb.lift(), epKb.width() + 20, epKb.height() + 120, 16, 0.0f);
        Ui::text((640 - epKb.width()) * 0.5f + 4, 116 - epKb.lift(), tr("Search this season"), 18, p.text);
        char found[48];
        snprintf(found, sizeof(found), tr("%d of %d"), (int)episodeView.size(), (int)episodes.size());
        Ui::textRight((640 + epKb.width()) * 0.5f - 4, 120 - epKb.lift(), found, 13, p.textDim);
        const std::string shown = epDraft + "_";
        Ui::field((640 - epKb.width()) * 0.5f, 150 - epKb.lift(), epKb.width(), 40, shown.c_str(), 18, true);
        epKb.render(ir);
        const Ui::Hint l[] = { { "A", N_("Type") }, { "kbdel", N_("Delete") }, { "kbspace", N_("Space") }, { "kbsym", N_("Symbols") }, { "+!", N_("Done") } };
        Ui::bottomBar(l, 5);
        return;
    }
    const float H = 50 + EPP_ROWS * EPP_STEP + 34;
    Ui::roundRect(Ui::screenLeft(), 0, Ui::screenWidth(), 480, 0, 0x00000080);
    Ui::shadow(EPP_X, EPP_Y + 4, EPP_W, H, 16, 10.0f, p.shadow);
    Ui::roundRect(EPP_X, EPP_Y, EPP_W, H, 16, p.cardTop, p.cardBottom);
    Ui::roundBorder(EPP_X, EPP_Y, EPP_W, H, 16, 1.5f, p.cardBorder);
    Ui::textCentered(320, EPP_Y + 14, fitText(font, filterDejaVu(currentSeasonName, 60), 19, (int)EPP_W - 40).c_str(), 19, p.text);
    const char* labels[EPP_ROWS] = { N_("Sort by"), N_("Order"), N_("Search"), N_("Mark Season Watched"), N_("Shuffle Season") };
    for (int i = 0; i < EPP_ROWS; ++i) {
        const float ry = EPP_Y + 50 + i * EPP_STEP;
        const bool on = i == epPanelRow;
        Ui::card(EPP_X + 16, ry, EPP_W - 32, 34, 10, on ? Ui::pulse() : 0.0f);
        Ui::text(EPP_X + 30, ry + 8, tr(labels[i]), 15, i >= 3 ? (on ? p.accentDark : p.text) : p.textDim);
        std::string v;
        if (i == 0) v = tr(EP_SORTS[episodeSort]);
        if (i == 1) v = tr(orderName(episodeSort, episodeDesc));
        if (i == 2) v = episodeQuery.empty() ? std::string(tr("A to type")) : fitText(font, episodeQuery, 16, 170);
        if (i <= 1 && on) v = "\xe2\x80\xb9  " + v + "  \xe2\x80\xba";
        if (!v.empty()) Ui::textRight(EPP_X + EPP_W - 30, ry + 7, v.c_str(), 16, on ? p.text : p.textDim);
    }
    const Ui::Hint l[] = { { "A", N_("Select") }, { "LR", N_("Change") } };
    const Ui::Hint r[] = { { "B", N_("Close") } };
    Ui::footer(l, 2, r, 1);
}

/* ---------------------------------------------------------------
 * A season marked watched: asked first
 * --------------------------------------------------------------- */
void LibraryView::askSeasonWatched(const std::string& id, const std::string& name)
{
    if (id.empty() || id == SPECIALS_ROW) return;
    seasonAsk = true;
    seasonAskSel = 1;   /* Cancel first */
    seasonAskId = id;
    seasonAskName = name;
    SoundFX::play(SoundFX::FX::Select);
}

void LibraryView::updateSeasonAsk(ir_t& ir)
{
    const int was = seasonAskSel;
    if (Input::isLeftPressed())  seasonAskSel = 0;
    if (Input::isRightPressed()) seasonAskSel = 1;
    if (ir.valid && ir.y >= 258 && ir.y < 296) {
        if (ir.x >= 150 && ir.x < 310) seasonAskSel = 0;
        if (ir.x >= 330 && ir.x < 490) seasonAskSel = 1;
    }
    if (seasonAskSel != was) SoundFX::play(SoundFX::FX::Move);
    if (Input::isBackPressed() || (Input::isAJustPressed() && seasonAskSel == 1)) {
        seasonAsk = false;
        SoundFX::play(SoundFX::FX::Back);
        return;
    }
    if (Input::isAJustPressed()) {
        seasonAsk = false;
        SoundFX::play(SoundFX::FX::Open);
        markSeasonWatched(seasonAskId, seasonAskName);
    }
}

void LibraryView::renderSeasonAsk()
{
    const Ui::Palette& p = Ui::pal();
    GRRLIB_Rectangle(Ui::screenLeft(), 0, Ui::screenWidth(), 480, p.dim, 1);
    Ui::card(120, 160, 400, 152, 20, 0.0f);
    char q[160];
    /* TRANSLATORS: %s is a season's name */
    snprintf(q, sizeof(q), tr("Mark %s watched?"), filterDejaVu(seasonAskName, 60).c_str());
    Ui::textCentered(320, 178, fitText(font, q, 20, 370).c_str(), 20, p.text);
    Ui::textCentered(320, 210, tr("Every episode not watched yet is marked watched."), 14, p.textDim);
    Ui::textCentered(320, 228, tr("The ones already watched stay as they are."), 14, p.textDim);
    Ui::button(150, 258, 160, 38, N_("Mark Watched"), 16, seasonAskSel == 0 ? Ui::pulse() : 0.0f);
    Ui::button(330, 258, 160, 38, N_("Cancel"), 16, seasonAskSel == 1 ? Ui::pulse() : 0.0f);
    const Ui::Hint l[] = { { "A", N_("Select") } };
    const Ui::Hint r[] = { { "B", N_("Cancel") } };
    Ui::footer(l, 1, r, 1);
}

/* only the episodes not watched yet: one request each (the season's own
 * request would mark the watched ones again, their last-watched dates too) */
void LibraryView::markSeasonWatched(const std::string& seasonId, const std::string& name)
{
    std::vector<JellyfinEpisode> eps;
    const bool here = seasonId == currentSeasonId && !episodes.empty();
    bool ok = true;
    std::string err;
    if (here) eps = episodes;
    else runWithLoading([&]() {
        ok = client.getEpisodes(serverUrl, auth, currentSeriesId, seasonId, eps);
        if (!ok) err = client.lastError();
    });
    if (!ok) { Ui::showNotice(tr("The season wasn't marked"), err, 5000); return; }
    std::vector<std::string> ids;
    for (const JellyfinEpisode& e : eps) if (!e.played) ids.push_back(e.id);
    if (ids.empty()) { Ui::showNotice(name, tr("Every episode is already watched."), 3000); return; }

    std::vector<char> marked(ids.size(), 0);
    int count = 0;
    progressLabel = tr("Marking watched");
    progressDone  = 0;
    progressTotal = (int)ids.size();
    runWithLoading([&]() {
        for (size_t i = 0; i < ids.size(); ++i) {
            if (client.setPlayed(serverUrl, auth, ids[i], true)) { marked[i] = 1; ++count; }
            progressDone = (int)i + 1;
        }
    });
    progressLabel.clear();
    progressTotal = 0;
    SYS_Report("[Series] season marked watched: %d of %d\n", count, (int)ids.size());
    if (here)
        for (JellyfinEpisode& e : episodes)
            for (size_t i = 0; i < ids.size(); ++i)
                if (marked[i] && e.id == ids[i]) { e.played = true; e.playbackPositionTicks = 0; break; }
    browse.invalidate();    /* continue watching, next up */
    if (here) buildEpisodeView();
    char msg[96];
    if (count == (int)ids.size())
        snprintf(msg, sizeof(msg), count == 1 ? tr("%d episode marked watched") : tr("%d episodes marked watched"), count);
    else
        snprintf(msg, sizeof(msg), tr("%d of %d marked watched; the server refused the others"), count, (int)ids.size());
    Ui::showNotice(name, msg, 4000);
}

/* the series' page: its poster (A plays it from the start), its name,
 * year, rating and seasons, genres, overview, cast; the seasons below */
void LibraryView::drawSeriesOverview(ir_t& ir)
{
    const Ui::Palette& p = Ui::pal();
    const JellyfinItemDetail& d = seriesDetail;
    /* the poster, focused: a play mark over it */
    const bool hover = ir.valid && ir.x >= SP_X && ir.x < SP_X + SP_W && ir.y >= SP_Y && ir.y < SP_Y + SP_H;
    const float f = seriesTop ? (ir.valid && !hover ? 0.35f : Ui::pulse()) : 0.0f;
    drawThumb(seriesPosterTex, SP_X, SP_Y, SP_W, SP_H, f, -1.0f, seriesPosterTex ? nullptr : currentSeriesName.c_str());
    if (seriesTop) {
        const float cx = SP_X + SP_W / 2, cy = SP_Y + SP_H / 2;
        Ui::circle(cx, cy, 30, Ui::alpha(0x000000FF, 0.55f));
        Ui::triangle(cx - 9, cy - 14, cx - 9, cy + 14, cx + 15, cy, 0xFFFFFFFF);
    }
    /* TRANSLATORS: under the series' poster: A on it plays the first episode */
    Ui::textCentered(SP_X + SP_W / 2, SP_Y + SP_H + 10, tr("Play from the start"), 14,
                     seriesTop ? p.accentDark : p.textDim);
    /* its facts */
    const float X = SP_X + SP_W + 26, W = 600 - X;
    float y = SP_Y - 2;
    for (const std::string& line : wrapText(filterDejaVu(currentSeriesName, 200), 24, W, 2)) {
        Ui::text(X, y, line.c_str(), 24, p.text);
        y += 30;
    }
    {
        std::string meta;
        auto add = [&](const std::string& part) { if (!part.empty()) meta += (meta.empty() ? "" : "  \xc2\xb7  ") + part; };
        if (d.year > 0) add(std::to_string(d.year));
        add(d.officialRating);
        int real = 0;
        for (const JellyfinSeason& se : seasons) real += se.id != SPECIALS_ROW;
        char ns[48];
        /* TRANSLATORS: how many seasons a series has */
        snprintf(ns, sizeof ns, real == 1 ? tr("%d season") : tr("%d seasons"), real);
        if (real > 0) add(ns);
        Ui::text(X, y + 2, fitText(font, meta, 15, (int)W).c_str(), 15, p.textDim);
        y += 24;
    }
    if (!d.genres.empty()) {
        std::string g;
        for (size_t i = 0; i < d.genres.size(); i++) g += (i ? "  \xc2\xb7  " : "") + d.genres[i];
        Ui::text(X, y, fitText(font, g, 14, (int)W).c_str(), 14, p.accentDark);
        y += 22;
    }
    y += 4;
    for (const std::string& line : seriesLines) {
        Ui::text(X, y, line.c_str(), 14, Ui::mix(p.text, p.textDim, 0.2f));
        y += 18;
    }
    /* the cast: its actors, the first few */
    std::string cast;
    int shown = 0;
    for (const JellyfinPerson& pp : d.people) {
        if (pp.role != "Actor" || shown >= 5) continue;
        cast += (shown++ ? ", " : "") + pp.name;
    }
    if (!cast.empty()) {
        y += 8;
        /* TRANSLATORS: before the series' actors' names */
        const std::string with = std::string(tr("With")) + "  ";
        Ui::text(X, y, with.c_str(), 14, p.accentDark);
        Ui::text(X + Ui::textWidth(with.c_str(), 14), y,
                 fitText(font, filterDejaVu(cast, 200), 14, (int)(W - Ui::textWidth(with.c_str(), 14))).c_str(), 14, p.text);
    }
    /* the seasons, below: Down */
    const float cy = PAGE_H - 22;
    /* TRANSLATORS: at the bottom of a series' page: Down shows its seasons */
    const char* down = tr("Seasons");
    const float w = Ui::textWidth(down, 16);
    Ui::text(320 - w / 2 - 10, cy - 9, down, 16, p.accentDark);
    Ui::triangle(320 + w / 2 - 2, cy - 4, 320 + w / 2 + 12, cy - 4, 320 + w / 2 + 5, cy + 4, p.accentDark);
}

// Season list
void LibraryView::renderSeasons(ir_t& ir) {
    drawBreadcrumb(currentSeriesName, 20, seriesFavorite ? 480 : 580);
    if (seriesFavorite) Ui::textRight(620, 16, (std::string("\xe2\x99\xa5 ") + tr("Favorite")).c_str(), 16, 0xE8455AFF);
    headerLine(46);
    /* the overview over the seasons, one sliding under the other */
    const float k = seriesScrollK * seriesScrollK * (3 - 2 * seriesScrollK);
    const bool sliding = k > 0.001f && k < 0.999f;
    if (sliding) Ui::clip(Ui::screenLeft(), 48, Ui::screenWidth(), PAGE_H - 48 + 10);   /* under the header */
    if (k < 0.999f) {
        Ui::pushOffset(0, -k * PAGE_H);
        drawSeriesOverview(ir);
        Ui::popOffset();
    }
    if (k > 0.001f) {
        Ui::pushOffset(0, (1 - k) * PAGE_H);
        drawSeasonsBody(ir);
        Ui::popOffset();
    }
    if (sliding) Ui::clipReset();
    if (seriesTop) {
        const Ui::Hint l[] = { { "A", N_("Play") }, { "2", N_("Shuffle") }, { "B", N_("Back") } };
        const Ui::Hint r[] = { { "UD", N_("Seasons") }, { "1", seriesFavorite ? N_("Unfavorite") : N_("Favorite") } };
        Ui::footer(l, 3, r, 2);
        return;
    }
    const bool real = seasonSel < (int)seasons.size() && seasons[seasonSel].id != SPECIALS_ROW;
    const Ui::Hint l[] = { { "A", N_("Select") }, { "2", N_("Shuffle") }, { "B", N_("Back") } };
    const Ui::Hint r[] = { { "+!", N_("Mark Watched") }, { "1", seriesFavorite ? N_("Unfavorite") : N_("Favorite") } };
    Ui::footer(l, 3, real ? r : r + 1, real ? 2 : 1);
    if (seasonAsk) renderSeasonAsk();
}

/* the seasons themselves: posters, or a list */
void LibraryView::drawSeasonsBody(ir_t& ir) {
    const Ui::Palette& p = Ui::pal();
    int n = (int)seasons.size();
    if (seasonGrid()) {
        const int x0 = seasonGridX0(n);
        /* the focused poster last: its halo over its neighbours */
        for (int pass = 0; pass < 2; pass++) {
            for (int k = 0; k < SG_COLS * SG_ROWS && seasonTop + k < n; k++) {
                const int idx = seasonTop + k;
                const bool sel = idx == seasonSel;
                if (sel != (pass == 1)) continue;
                int px = x0 + (k % SG_COLS) * SG_STRIDE_X, py = SG_Y0 + (k / SG_COLS) * SG_STRIDE_Y;
                GRRLIB_texImg* t = seasonPoster(idx);
                drawThumb(t, px, py, SG_W, SG_H, sel ? Ui::pulse() : 0.0f, -1.0f,
                          t ? nullptr : seasons[idx].name.c_str());
                std::string name = fitText(font, filterDejaVu(seasons[idx].name, 45), 14, SG_STRIDE_X - 8);
                Ui::textCentered(px + SG_W / 2, py + SG_H + 6, name.c_str(), 14,
                                 sel ? p.accentDark : p.text);
            }
        }
        const int rows = (n + SG_COLS - 1) / SG_COLS;
        if (rows > SG_ROWS)
            Ui::scrollbar(614, SG_Y0, SG_ROWS * SG_STRIDE_Y - 8, seasonTop / SG_COLS, SG_ROWS, rows);
    } else {
        const bool cover = Ui::libraryStyle() == Ui::LibraryStyle::ListCover;
        const int lw = cover ? SEASON_LW : LIST_W;
        for (int i = 0; i < ITEMS_VISIBLE; i++) {
            int idx = seasonTop + i;
            if (idx >= n) break;
            bool sel   = (idx == seasonSel);
            bool hover = ir.valid &&
                         ir.y >= LIST_Y + i * ROW_H &&
                         ir.y <  LIST_Y + (i + 1) * ROW_H &&
                         ir.x >= LIST_X && ir.x <= LIST_X + lw;
            int ry = LIST_Y + i * ROW_H;
            float f = focusOf(sel, hover);
            drawRow(LIST_X, ry, lw, ROW_H, f);
            std::string name = fitText(font, filterDejaVu(seasons[idx].name, 45), 18, lw - 32);
            Ui::text(LIST_X + 16, ry + 11, name.c_str(), 18, Ui::mix(p.text, p.accentDark, f));
        }
        Ui::scrollbar(LIST_X + lw + 8, LIST_Y + 2, ITEMS_VISIBLE * ROW_H - 6, seasonTop, ITEMS_VISIBLE, n);
        /* the selected season's poster (List + Posters) */
        if (cover && seasonSel >= 0 && seasonSel < n) {
            GRRLIB_texImg* t = seasonPoster(seasonSel);
            const float PX = LIST_X + lw + 26, PW = 600 - PX, PH = fitHeight(t, PW, PW * 1.5f), PY = LIST_Y + 2;
            drawThumb(t, PX, PY, PW, PH, 0.0f, -1.0f, t ? nullptr : seasons[seasonSel].name.c_str(), false);
            std::string name = fitText(font, filterDejaVu(seasons[seasonSel].name, 45), 15, (int)PW);
            Ui::text(PX, PY + PH + 10, name.c_str(), 15, p.text);
        }
    }
}

// Episode list
void LibraryView::renderEpisodes(ir_t& ir) {
    const Ui::Palette& p = Ui::pal();
    std::string hdr = currentSeriesName + "  /  " + currentSeasonName;
    drawBreadcrumb(hdr, 18, 580);
    headerLine(46);

    const int n = (int)episodeView.size();
    for (int i = 0; i < ITEMS_VISIBLE; i++) {
        int idx = episodeTop + i;
        if (idx >= n) break;
        const JellyfinEpisode& ep = episodes[episodeView[idx]];
        bool sel   = (idx == episodeSel);
        bool hover = ir.valid &&
                     ir.y >= LIST_Y + i * ROW_H &&
                     ir.y <  LIST_Y + (i + 1) * ROW_H &&
                     ir.x >= LIST_X && ir.x <= LIST_X + LIST_W;
        int ry = LIST_Y + i * ROW_H;
        float f = focusOf(sel, hover);
        drawRow(LIST_X, ry, LIST_W, ROW_H, f);

        int tx = LIST_X + 16;
        // Episode number badge
        if (ep.indexNumber > 0) {
            char num[16];
            /* TRANSLATORS: E = episode, its number in the season */
            snprintf(num, sizeof(num), tr("E%02d"), ep.indexNumber);
            int bw = Ui::textWidth(num, 12) + 14;
            Ui::roundRect(tx, ry + 12, bw, 20, 10, Ui::mix(p.accent, 0xFFFFFFFF, 0.2f), p.accentDark);
            Ui::textCentered(tx + bw / 2, ry + 15, num, 12, p.textOnAccent);
            tx += bw + 10;
        }
        /* sorted by rating or length: that value on the right */
        char extra[24] = "";
        if (episodeSort == ES_RATING && ep.rating > 0) snprintf(extra, sizeof(extra), "\xe2\x98\x85 %.1f", ep.rating);
        if (episodeSort == ES_LENGTH && ep.runTimeTicks > 0)
            snprintf(extra, sizeof(extra), tr("%d min"), (int)((ep.runTimeTicks + 300000000LL) / 600000000LL));
        const int ew = extra[0] ? Ui::textWidth(extra, 14) + 14 : 0;
        const bool seen = ep.played;
        const int  avail = LIST_X + LIST_W - 16 - tx - (seen ? 34 : 0) - ew;
        const u32  col = seen ? Ui::mix(p.textDim, p.accentDark, f) : Ui::mix(p.text, p.accentDark, f);
        titleText(0, tx, ry + 11, avail, filterDejaVu(ep.name, 200), 18, col, sel);
        if (extra[0]) Ui::textRight(LIST_X + LIST_W - 16 - (seen ? 34 : 0), ry + 13, extra, 14, p.textDim);
        if (seen)   /* watched: a check on the right */
            Ui::textRight(LIST_X + LIST_W - 16, ry + 11, "\xe2\x9c\x93", 18, p.ok);
    }
    if (n == 0 && !episodeQuery.empty())
        Ui::textCentered(320, LIST_Y + 40, tr("No episode matches"), 16, p.textDim);
    Ui::scrollbar(614, LIST_Y + 2, ITEMS_VISIBLE * ROW_H - 6, episodeTop, ITEMS_VISIBLE, n);

    /* how the list is shown, under it: the search, the sort */
    std::string how;
    if (!episodeQuery.empty()) {
        char s[96];
        snprintf(s, sizeof(s), tr("\"%s\": %d of %d"), episodeQuery.c_str(), n, (int)episodes.size());
        how = s;
    }
    if (episodeSort != ES_EPISODE || episodeDesc)
        how += (how.empty() ? "" : "  \xc2\xb7  ") + std::string(tr(EP_SORTS[episodeSort])) + ", " +
               tr(orderName(episodeSort, episodeDesc));

    const int e = shownEpisode();
    const bool seenSel = e >= 0 && episodes[e].played;
    const Ui::Hint l[] = { { "A", N_("Details") }, { "1", seenSel ? N_("Unwatched") : N_("Watched") },
                           { "2", N_("Options") } };
    const Ui::Hint r[] = { { "-/+", N_("Page") }, { "B", episodeQuery.empty() ? N_("Back") : N_("Clear") } };
    Ui::footer(l, 3, r, 2, how.empty() ? nullptr : how.c_str());

    if (epPanel || epTyping) renderEpisodePanel(ir);
    if (seasonAsk) renderSeasonAsk();
}
