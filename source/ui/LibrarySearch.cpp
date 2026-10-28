/* LibraryView: Search: query keyboard and results. */
#include "LibraryView.h"
#include "LibraryDraw.h"
#include "Ui.h"
#include "../input/Input.h"
#include "../core/Text.h"
#include "../core/Lang.h"
#include "../core/SoundFX.h"
#include <stdio.h>
#include <ogc/lwp_watchdog.h>   // gettime(), ticks_to_millisecs()

using namespace LibDraw;

bool LibraryView::updateSearchInput(ir_t& ir) {
    searchKb.setOrigin((640 - searchKb.width()) * 0.5f, 132);
    searchKb.setEnterLabel(N_("Search"));
    searchKb.setTextField(62, 70, 516, 38);
    switch (searchKb.update(ir, searchQuery, 64)) {
    case Keyboard::Result::Enter:            // Search key or [+]
        if (!searchQuery.empty()) state = State::SearchLoad;
        break;
    case Keyboard::Result::Cancel:           // B with an empty query
        state = searchReturnState;
        break;
    default: break;
    }
    return false;
}

bool LibraryView::updateSearchResults(ir_t& ir, bool aPressed) {
    int n = (int)searchResults.size();
    // B → back to search input
    if (Input::isBackPressed()) {
        state = State::SearchInput;
        return false;
    }
    // [1] → new search
    if (Input::is1Pressed()) {
        searchQuery.clear();
        allSearchResults.clear();
        searchResults.clear();
        searchSel = 0; searchTop = 0;
        searchFilterTab = 0;
        searchKb.reset();
        state = State::SearchInput;
        irMode = false;
        return false;
    }

    // Tab switching with -/+
    const int numTabs = 6;
    if (Input::isLPressed()) {
        searchFilterTab = (searchFilterTab + numTabs - 1) % numTabs;
        applySearchFilter();
        SoundFX::play(SoundFX::FX::Move);
        return false;
    }
    if (Input::isRPressed()) {
        searchFilterTab = (searchFilterTab + 1) % numTabs;
        applySearchFilter();
        SoundFX::play(SoundFX::FX::Move);
        return false;
    }

    // IR click on tab bar
    if (ir.valid && aPressed) {
        int clickedTab = Ui::tabAt(ir.x, ir.y);
        if (clickedTab >= 0 && clickedTab < numTabs) {
            if (clickedTab != searchFilterTab) {
                searchFilterTab = clickedTab;
                applySearchFilter();
                SoundFX::play(SoundFX::FX::Move);
            }
            return false;
        }
    }

    if (n == 0) return false;

    if (Input::isUpPressed())   { searchSel--; irMode = false; clampSearchScroll(); }
    if (Input::isDownPressed()) { searchSel++; irMode = false; clampSearchScroll(); }

    // IR hover
    if (irMode && ir.valid && !Input::isUpPressed() && !Input::isDownPressed()) {
        for (int i = 0; i < SEARCH_VISIBLE; i++) {
            int idx = searchTop + i;
            if (idx >= n) break;
            int ry = LIST_Y + i * ROW_H;
            if (ir.x >= LIST_X && ir.x <= LIST_X + LIST_W &&
                ir.y >= ry && ir.y < ry + ROW_H) {
                searchSel = idx;
                irMode = true;
                clampSearchScroll();
            }
        }
    }

    if (aPressed && n > 0 && searchSel < n) {
        const JellyfinItem& sel = searchResults[searchSel];
        if (sel.type == "Series") {
            currentSeriesId   = sel.id;
            currentSeriesName = sel.name;
            seasonsCallerState = State::SearchReady;
            seasons.clear(); seasonSel = 0; seasonTop = 0;
            state = State::SeasonsLoad;
        } else if (sel.type == "MusicAlbum") {
            musicAlbumId     = sel.id;
            musicAlbumName   = sel.name;
            musicAlbumArtist.clear();
            musicTracks.clear();
            musicTrackSel = 0;
            musicTrackTop = 0;
            musicIsPlaylist = false;
            state = State::MusicTracksLoad;
        } else if (sel.type == "Playlist") {
            musicAlbumId     = sel.id;
            musicAlbumName   = sel.name;
            musicAlbumArtist.clear();
            musicTracks.clear();
            musicTrackSel = 0;
            musicTrackTop = 0;
            musicIsPlaylist = true;
            state = State::MusicTracksLoad;
        } else if (sel.type == "MusicArtist") {
            parentLibId      = currentLibId;
            parentLibName    = currentLibName;
            parentItemPage   = itemPage;
            inItemsDrilldown    = true;
            drilldownFromSearch = true;
            currentLibId     = sel.id;
            currentLibName   = sel.name;
            currentLibType   = "music";
            posterMode       = false;
            itemPage         = 0;
            state            = State::ItemsInit;
        } else if (sel.type == "BoxSet" && Ui::listMode()) {
            // text list of the collection; B comes back to the results
            inItemsDrilldown    = true;
            drilldownFromSearch = true;
            currentLibId      = sel.id;
            currentLibName    = sel.name;
            currentLibType    = "boxsets";
            posterMode        = false;
            itemPage          = 0;
            state = State::ItemsInit;
        } else if (sel.type == "BoxSet") {
            inBoxSetDrilldown   = true;
            drilldownFromSearch = true;
            currentLibId      = sel.id;
            currentLibName    = sel.name;
            posterMode        = true;
            itemPage          = 0;
            freePosters();
            state = State::ItemsInit;
        } else if (sel.type == "Audio") {
            MusicTrack t;
            t.id    = sel.id;
            t.title = sel.name;
            t.runtimeTicks = sel.runtimeTicks;
            pendingMusicTracks.clear();
            pendingMusicTracks.push_back(t);
            pendingMusicTrackIdx = 0;
            pendingPlayIsMusic   = true;
            return true;
        } else {
            detailItemId        = sel.id;
            detailReturnState   = State::SearchReady;
            detailIsEpisodeHint = (sel.type == "Episode");
            state = State::DetailLoad;
        }
    }
    return false;
}

// ---------------------------------------------------------------
void LibraryView::clampSearchScroll() {
    int n = (int)searchResults.size();
    if (searchSel < 0) searchSel = 0;
    if (n > 0 && searchSel >= n) searchSel = n - 1;
    if (searchSel < searchTop) searchTop = searchSel;
    if (searchSel >= searchTop + SEARCH_VISIBLE) searchTop = searchSel - SEARCH_VISIBLE + 1;
    if (searchTop < 0) searchTop = 0;
}

// ---------------------------------------------------------------
void LibraryView::applySearchFilter() {
    searchResults.clear();
    for (const auto& item : allSearchResults) {
        // Respect hidden libraries: if parentId is known and hidden, skip
        if (!item.parentId.empty() && isHidden(item.parentId)) continue;

        // Check category tab:
        // 0: All
        // 1: Movies
        // 2: Shows
        // 3: Episodes
        // 4: Music
        // 5: Collections
        if (searchFilterTab == 1 && item.type != "Movie") continue;
        if (searchFilterTab == 2 && item.type != "Series") continue;
        if (searchFilterTab == 3 && item.type != "Episode") continue;
        if (searchFilterTab == 4 && item.type != "MusicAlbum" && item.type != "Audio" && item.type != "MusicArtist") continue;
        if (searchFilterTab == 5 && item.type != "BoxSet" && item.type != "Playlist") continue;

        // On "All" tab, respect if user has no visible music libraries
        if (searchFilterTab == 0) {
            bool hasMusic = false;
            for (const auto& lib : libraries) {
                if (lib.collectionType == "music") { hasMusic = true; break; }
            }
            if (!hasMusic && (item.type == "MusicAlbum" || item.type == "Audio" || item.type == "MusicArtist")) {
                continue;
            }
        }

        searchResults.push_back(item);
    }
    searchSel = 0;
    searchTop = 0;
    clampSearchScroll();
}

// ---------------------------------------------------------------
void LibraryView::performSearch() {
    allSearchResults.clear();
    searchResults.clear();
    bool ok = false; std::string err;
    if (!runWithLoading([&]() {
        ok = client.searchItems(serverUrl, auth, searchQuery, 100, allSearchResults);
        if (!ok) err = client.lastError();
    })) {
        state = State::SearchInput;
        return;
    }
    if (!ok) { errMsg = err; state = State::Error; return; }
    applySearchFilter();
    state = State::SearchReady;
}

// ---------------------------------------------------------------
void LibraryView::renderSearchInput(ir_t& ir) {
    const Ui::Palette& p = Ui::pal();
    Ui::header(tr("Search"));

    // Search field: placeholder when empty, blinking caret
    {
        const float FX = 62, FY = 70, FW = 516, FH = 38;
        int cPos = searchKb.cursorPos();
        if (cPos < 0 || cPos > (int)searchQuery.size()) cPos = (int)searchQuery.size();
        int maxW = (int)FW - 44;
        int scroll = searchKb.calcScrollOffset(searchQuery, maxW, 18);
        std::string display = searchQuery.substr(scroll);
        while (display.size() > 1 && Text::width(font, display.c_str(), 18) > (u32)maxW)
            display.pop_back();

        int relCaret = cPos - scroll;
        if (relCaret < 0) relCaret = 0;
        if (relCaret > (int)display.size()) relCaret = (int)display.size();

        bool caretOn = (ticks_to_millisecs(gettime()) / 400) % 2 == 0;
        Ui::field(FX, FY, FW, FH, nullptr, 18, true);
        if (searchKb.isTextFieldFocused()) {
            Ui::roundBorder(FX, FY, FW, FH, 7, 2.0f, p.accent);
        }
        if (searchQuery.empty())
            Ui::text(FX + 20, FY + 9, tr("Type a title, an actor..."), 18, Ui::alpha(p.textDim, 0.8f));
        else
            Ui::text(FX + 20, FY + 9, display.c_str(), 18, p.text);
        if (caretOn) {
            float cx = FX + 20 + (relCaret > 0 ? Ui::textWidth(display.substr(0, relCaret).c_str(), 18) : 0);
            Ui::roundRect(cx, FY + 9, 2, 20, 1, p.accent);
        }
    }

    // Keyboard on its panel
    Ui::card((640 - searchKb.width()) * 0.5f - 10, 122,
             searchKb.width() + 20, searchKb.height() + 20, 16, 0.0f);
    searchKb.render(ir);

    const Ui::Hint l[] = { { "A", N_("Type") }, { "kbdel", N_("Delete") }, { "kbspace", N_("Space") } };
    const Ui::Hint r[] = { { "kbsym", N_("Symbols") }, { "+!", N_("Search") }, { "B", N_("Back") } };
    Ui::footer(l, 3, r, searchQuery.empty() ? 3 : 2);
}

// ---------------------------------------------------------------
void LibraryView::renderSearchResults(ir_t& ir) {
    const Ui::Palette& p = Ui::pal();
    int n = (int)searchResults.size();

    // Tabbed header
    const char* const kSearchTabs[6] = {
        N_("All"),
        N_("Movies"),
        N_("Shows"),
        N_("Episodes"),
        N_("Music"),
        N_("Collections")
    };
    float tabX0 = Ui::tabs(320, 10, kSearchTabs, 6, searchFilterTab, 13);

    // Left of tabs: query breadcrumb
    std::string srv = fitText(font, filterDejaVu(searchQuery, 30), 14, (int)tabX0 - 35);
    Ui::text(20, 18, srv.c_str(), 14, p.textDim);

    // Right of tabs: count
    if (n > 0) {
        char sc[32];
        if (n > SEARCH_VISIBLE) snprintf(sc, sizeof(sc), "%d / %d", searchSel + 1, n);
        else snprintf(sc, sizeof(sc), "%d", n);
        Ui::textRight(612, 17, sc, 14, p.textDim);
    }
    headerLine(46);

    if (n == 0) {
        Ui::textCentered(320, 200, tr("No results."), 20, p.textDim);
    } else {
        for (int i = 0; i < SEARCH_VISIBLE; i++) {
            int idx = searchTop + i;
            if (idx >= n) break;
            int   ry  = LIST_Y + i * ROW_H;
            bool  sel = (idx == searchSel);
            bool  hov = ir.valid &&
                        ir.x >= LIST_X && ir.x <= LIST_X + LIST_W &&
                        ir.y >= ry && ir.y < ry + ROW_H;
            float f = focusOf(sel, hov);
            drawRow(LIST_X - 4, ry, LIST_W + 8, ROW_H, f);

            // Type badge
            const std::string& type = searchResults[idx].type;
            const char* badge = "?";
            u32 bCol = 0x446688FF;
            /* TRANSLATORS: type badges of the search results, 54 pixels wide: keep them short */
            if      (type == "Movie")       { badge = N_("MOVIE");   bCol = 0x2D7DE0FF; }
            else if (type == "Series")      { badge = N_("SERIES");  bCol = 0xE0563FFF; }
            else if (type == "Episode")     { badge = N_("EP");      bCol = 0xC2453AFF; }
            else if (type == "MusicAlbum")  { badge = N_("ALBUM");   bCol = 0x3DAF5AFF; }
            else if (type == "Audio")       { badge = N_("TRACK");   bCol = 0x3DAF5AFF; }
            else if (type == "MusicArtist") { badge = N_("ARTIST");  bCol = 0x2C9450FF; }
            else if (type == "BoxSet")      { badge = N_("COLLEC");  bCol = 0x8E54D6FF; }
            else if (type == "Playlist")    { badge = N_("LIST");    bCol = 0x2A9FB0FF; }
            Ui::roundRect(LIST_X + 8, ry + 12, 54, 20, 10, Ui::mix(bCol, 0xFFFFFFFF, 0.15f), bCol);
            Ui::textCentered(LIST_X + 35, ry + 15, tr(badge), 10, 0xFFFFFFFF);

            // Title
            std::string label = filterDejaVu(searchResults[idx].name, 200);
            if (type == "Episode" && !searchResults[idx].seriesName.empty()) {
                // Show "Series S01E02 - Episode title"
                char ep[64];
                /* TRANSLATORS: season and episode numbers before the episode's title */
                snprintf(ep, sizeof(ep), tr("S%02dE%02d - "),
                         searchResults[idx].seasonNumber,
                         searchResults[idx].episodeNumber);
                label = filterDejaVu(searchResults[idx].seriesName, 200) + " " + ep + filterDejaVu(searchResults[idx].name, 200);
            } else if (searchResults[idx].year > 0) {
                char yb[16];
                snprintf(yb, sizeof(yb), " (%d)", searchResults[idx].year);
                label += yb;
            }
            titleText(0, LIST_X + 74, ry + 12, LIST_W - 90, label, 16, Ui::mix(p.text, p.accentDark, f), sel);
        }
    }

    const Ui::Hint l[] = { { "A", N_("Open") }, { "-/+", N_("Filter") }, { "1", N_("New search") } };
    const Ui::Hint r[] = { { "B", N_("Back") } };
    Ui::footer(l, 3, r, 1);
}
