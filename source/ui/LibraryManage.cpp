/* LibraryView: Display Libraries: every library of the server, the ones left off
 * the home too.  A opens one, 1 (or A on its box) shows or hides it on the
 * home: the tiles, the browse page's Libraries row and its rows, the home's
 * New Additions and Top Rated.  The profile keeps the choice. */
#include "LibraryView.h"
#include "LibraryDraw.h"
#include "Ui.h"
#include "../input/Input.h"
#include "../core/SoundFX.h"
#include "../core/Lang.h"

using namespace LibDraw;

static const float LM_W = 440, LM_ROW = 36, LM_Y = 118;   /* card width, line height, first line */
static const int   LM_ROWS = 8;                            /* lines shown                         */
static float lmLeft() { return 320 - LM_W / 2; }

bool LibraryView::isHidden(const std::string& id) const
{
    for (const auto& h : hiddenLibs) if (h == id) return true;
    return false;
}

void LibraryView::filterLibraries()
{
    libraries.clear();
    for (const auto& l : allLibraries) if (!isHidden(l.id)) libraries.push_back(l);
    /* every one hidden: the home would have nothing to open */
    if (libraries.empty()) libraries = allLibraries;
    if (libSel >= (int)libraries.size()) libSel = libraries.empty() ? 0 : (int)libraries.size() - 1;
}

void LibraryView::setHiddenLibraries(const std::string& ids)
{
    hiddenLibs.clear();
    size_t a = 0;
    while (a < ids.size()) {
        size_t b = ids.find(',', a);
        if (b == std::string::npos) b = ids.size();
        if (b > a) hiddenLibs.push_back(ids.substr(a, b - a));
        a = b + 1;
    }
}

std::string LibraryView::hiddenLibraries() const
{
    std::string s;
    for (const auto& h : hiddenLibs) s += (s.empty() ? "" : ",") + h;
    return s;
}

void LibraryView::openLibManager(bool fromSettings)
{
    if (allLibraries.empty()) { if (fromSettings) exitTo = Exit::Settings; return; }
    libManager = true;
    libManagerFromSettings = fromSettings;
    libManagerDirty = false;
    libManagerSel = libManagerTop = 0;
    SoundFX::play(SoundFX::FX::Open);
    SYS_Report("[Library] all libraries (%d, %d hidden)\n", (int)allLibraries.size(), (int)hiddenLibs.size());
}

void LibraryView::closeLibManager()
{
    libManager = false;
    if (!libManagerDirty) return;
    libManagerDirty = false;
    /* the home again, without the hidden ones (App saves the profile) */
    filterLibraries();
    browse.invalidate();
    catalog.invalidate();
    hiddenChanged = true;
    SYS_Report("[Library] %d of %d libraries on the home\n",
               (int)(allLibraries.size() - hiddenLibs.size()), (int)allLibraries.size());
}

bool LibraryView::updateLibManager(ir_t& ir)
{
    const int n = (int)allLibraries.size(), was = libManagerSel;
    if (Input::isBackPressed()) {
        closeLibManager();
        SoundFX::play(SoundFX::FX::Back);
        /* opened from the settings: back to them */
        if (libManagerFromSettings) { libManagerFromSettings = false; exitTo = Exit::Settings; return true; }
        return false;
    }
    if (Input::isUpPressed())   libManagerSel = (libManagerSel + n - 1) % n;
    if (Input::isDownPressed()) libManagerSel = (libManagerSel + 1) % n;
    if (Input::isLPressed())    libManagerSel = libManagerSel - LM_ROWS < 0 ? 0 : libManagerSel - LM_ROWS;
    if (Input::isRPressed())    libManagerSel = libManagerSel + LM_ROWS >= n ? n - 1 : libManagerSel + LM_ROWS;
    int hover = -1;
    bool onBox = false;
    if (ir.valid && ir.x >= lmLeft() && ir.x < lmLeft() + LM_W && ir.y >= LM_Y) {
        int i = libManagerTop + (int)((ir.y - LM_Y) / LM_ROW);
        if (i - libManagerTop < LM_ROWS && i < n) { hover = i; onBox = ir.x < lmLeft() + 48; }
    }
    if (hover >= 0) libManagerSel = hover;
    if (libManagerSel < libManagerTop) libManagerTop = libManagerSel;
    if (libManagerSel >= libManagerTop + LM_ROWS) libManagerTop = libManagerSel - LM_ROWS + 1;
    if (libManagerSel != was) SoundFX::play(SoundFX::FX::Move);

    const bool a = Input::isAJustPressed() && (!ir.valid || hover >= 0);
    if (Input::is1Pressed() || (a && onBox)) {
        const std::string& id = allLibraries[libManagerSel].id;
        bool found = false;
        for (size_t k = 0; k < hiddenLibs.size(); ++k)
            if (hiddenLibs[k] == id) { hiddenLibs.erase(hiddenLibs.begin() + k); found = true; break; }
        if (!found) hiddenLibs.push_back(id);
        libManagerDirty = true;
        SoundFX::play(SoundFX::FX::Move);
        return false;
    }
    if (a) {
        const JellyfinLibrary lib = allLibraries[libManagerSel];
        closeLibManager();
        libManagerFromSettings = false;
        for (int i = 0; i < (int)libraries.size(); ++i)
            if (libraries[i].id == lib.id) libSel = i;
        SoundFX::play(SoundFX::FX::Open);
        openLibrary(lib);
    }
    return false;
}

void LibraryView::renderLibManager(ir_t& ir)
{
    (void)ir;
    const Ui::Palette& p = Ui::pal();
    const int n = (int)allLibraries.size();
    const int shown = n < LM_ROWS ? n : LM_ROWS;
    Ui::roundRect(Ui::screenLeft(), 0, Ui::screenWidth(), 480, 0, 0x00000090);
    const float top = LM_Y - 66, h = 66 + shown * LM_ROW + 16;
    Ui::card(lmLeft() - 16, top, LM_W + 32, h, 18, 0.0f);
    Ui::text(lmLeft() + 4, top + 14, tr("Display Libraries"), 20, p.text);
    char count[48];
    int hidden = 0;
    for (const auto& l : allLibraries) if (isHidden(l.id)) ++hidden;
    /* TRANSLATORS: Display Libraries: how many are on the home */
    snprintf(count, sizeof(count), tr("%d of %d on the home"), n - hidden, n);
    Ui::textRight(lmLeft() + LM_W - 4, top + 20, count, 13, p.textDim);

    for (int k = 0; k < shown; ++k) {
        const int i = libManagerTop + k;
        if (i >= n) break;
        const JellyfinLibrary& l = allLibraries[i];
        const float y = LM_Y + k * LM_ROW;
        const bool sel = i == libManagerSel, off = isHidden(l.id);
        if (sel) Ui::roundRect(lmLeft(), y, LM_W, LM_ROW - 4, 10, Ui::alpha(p.accent, 0.35f * Ui::pulse()));
        /* the box: ticked, on the home */
        const float bx = lmLeft() + 12, by = y + 7;
        Ui::roundRect(bx, by, 18, 18, 4, off ? p.field : p.accent);
        Ui::roundBorder(bx, by, 18, 18, 4, 1.5f, off ? p.fieldBorder : p.accentDark);
        if (!off) Ui::roundRect(bx + 5, by + 5, 8, 8, 2, p.textOnAccent);
        const char* type = labelForType(l.collectionType);
        const int pw = Ui::textWidth(type, 10) + 18;
        std::string name = fitText(font, filterDejaVu(l.name, 40), 16, (int)(LM_W - 60 - pw - 16));
        Ui::text(lmLeft() + 44, y + 7, name.c_str(), 16, off ? p.textDim : p.text);
        const float px = lmLeft() + LM_W - 10 - pw;
        Ui::roundRect(px, y + 8, pw, 17, 8.5f,
                      Ui::alpha(colorForType(l.collectionType, true), off ? 0.5f : 1.0f),
                      Ui::alpha(colorForType(l.collectionType, false), off ? 0.5f : 1.0f));
        Ui::textCentered(px + pw / 2, y + 10, type, 10, 0xFFFFFFFF);
    }
    if (n > LM_ROWS) Ui::scrollbar(lmLeft() + LM_W + 6, LM_Y, LM_ROWS * LM_ROW - 4, libManagerTop, LM_ROWS, n);

    const bool off = libManagerSel < n && isHidden(allLibraries[libManagerSel].id);
    const Ui::Hint l[] = { { "A", N_("Open") }, { "1", off ? N_("Show on Home") : N_("Hide from Home") } };
    const Ui::Hint r[] = { { "B", N_("Back") } };
    Ui::footer(l, 2, r, 1);
}
