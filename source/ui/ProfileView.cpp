#include "ProfileView.h"
#include "../core/Text.h"
#include "Ui.h"
#include "../input/Input.h"
#include "../core/SoundFX.h"
#include "../core/Lang.h"
#include "Avatars.h"
#include <stdio.h>
#include <string.h>

ProfileView::ProfileView(GRRLIB_ttfFont* f, GRRLIB_texImg* cursor,
                         const std::vector<SavedProfile>& p)
    : font(f), cursorTex(cursor), profiles(p)
{
    focusedRow = 0;
    kb.setEnterLabel(N_("Done"));
}

/* -----------------------------------------------------------------------
 * update()
 * ----------------------------------------------------------------------- */
ProfileResult ProfileView::update(ir_t& ir) {
    irMode = ir.valid;   /* no pointer on the screen: the D-pad drives, A acts on the highlighted item */

    int totalRows = (int)profiles.size() + 1; /* profiles + "Add New" row */
    const int rowBefore = focusedRow;

    if (picking) return updatePicker(ir);
    if (editing && !renaming) return updateEdit(ir);

    /* its nickname typed: Done keeps it (nothing: the user name again) */
    if (renaming) {
        kb.setOrigin((640 - kb.width()) * 0.5f, 214 - kb.lift());
        const Keyboard::Result r = kb.update(ir, draft, 32);
        if (r == Keyboard::Result::Cancel) { renaming = false; SoundFX::play(SoundFX::FX::Back); }
        if (r == Keyboard::Result::Enter) {
            renaming = false;
            editing = false;
            size_t a = draft.find_first_not_of(' '), b = draft.find_last_not_of(' ');
            newName = a == std::string::npos ? "" : draft.substr(a, b - a + 1);
            selectedIdx = focusedRow;
            SoundFX::play(SoundFX::FX::Open);
            return ProfileResult::Rename;
        }
        return ProfileResult::None;
    }

    /* the delete question over the list: the list behind it stays put (the
     * pointer moving over another row closed it); Left/Right or the pointer
     * pick Delete or Cancel (Cancel first), A acts on it, B cancels */
    if (confirmDelete) {
        const int was = confirmSel;
        if (Input::isLeftPressed())  confirmSel = 0;
        if (Input::isRightPressed()) confirmSel = 1;
        if (ir.valid && ir.y >= 248 && ir.y <= 286) {
            if (ir.x >= 170 && ir.x <= 310) confirmSel = 0;
            if (ir.x >= 330 && ir.x <= 470) confirmSel = 1;
        }
        if (confirmSel != was) SoundFX::play(SoundFX::FX::Move);
        if (Input::isBackPressed() || (Input::isAJustPressed() && confirmSel == 1)) {
            SoundFX::play(SoundFX::FX::Back);
            confirmDelete = false;
            return ProfileResult::None;
        }
        if (Input::isAJustPressed()) {
            SoundFX::play(SoundFX::FX::Open);
            confirmDelete = false;
            selectedIdx = focusedRow;
            return ProfileResult::DeleteOne;
        }
        if (Input::isLPressed()) { confirmDelete = false; SoundFX::play(SoundFX::FX::Back); }
        return ProfileResult::None;
    }

    /* D-pad navigation */
    if (Input::isUpPressed()) {
        focusedRow = (focusedRow - 1 + totalRows) % totalRows;
        irMode = false;
        confirmDelete = false;
    }
    if (Input::isDownPressed()) {
        focusedRow = (focusedRow + 1) % totalRows;
        irMode = false;
        confirmDelete = false;
    }

    /* IR hover */
    bool irHoveredRow = false;
    if (ir.valid) {
        int vis = (totalRows < MAX_VIS) ? totalRows : MAX_VIS;
        for (int i = 0; i < vis; i++) {
            int ry = ROW_Y0 + i * ROW_H;
            if (ir.x >= ROW_X && ir.x <= ROW_X + ROW_W &&
                ir.y >= ry    && ir.y <= ry + ROW_H - 4) {
                if (focusedRow != i) confirmDelete = false;
                focusedRow = i;
                irMode = true;
                irHoveredRow = true;
            }
        }
    }

    if (focusedRow != rowBefore) SoundFX::play(SoundFX::FX::Move);

    /* B = back / cancel confirm */
    if (Input::isBackPressed()) {
        SoundFX::play(SoundFX::FX::Back);
        if (confirmDelete) { confirmDelete = false; return ProfileResult::None; }
        return ProfileResult::Back;
    }

    /* 1 = Edit Profile */
    if (Input::is1Pressed() && focusedRow < (int)profiles.size()) {
        editing = true;
        editSel = 0;
        SoundFX::play(SoundFX::FX::Select);
        return ProfileResult::None;
    }

    /* 2 = its picture */
    if (Input::is2Pressed() && focusedRow < (int)profiles.size()) {
        openPicker();
        return ProfileResult::None;
    }

    /* MINUS (isLPressed) = toggle delete confirm for a profile row */
    if (Input::isLPressed() && focusedRow < (int)profiles.size()) {
        confirmDelete = !confirmDelete;
        confirmSel = 1;
        SoundFX::play(confirmDelete ? SoundFX::FX::Select : SoundFX::FX::Back);
        return ProfileResult::None;
    }

    /* A = confirm */
    if (Input::isAJustPressed() && (irHoveredRow || (!ir.valid && !irMode))) {
        SoundFX::play(SoundFX::FX::Open);
        if (confirmDelete) {
            confirmDelete = false;
            selectedIdx = focusedRow;
            return ProfileResult::DeleteOne;
        }
        if (focusedRow == (int)profiles.size()) {
            return ProfileResult::AddNew;
        }
        selectedIdx = focusedRow;
        return ProfileResult::Selected;
    }

    return ProfileResult::None;
}

/* -----------------------------------------------------------------------
 * render()
 * ----------------------------------------------------------------------- */
void ProfileView::render(ir_t& ir) {
    const Ui::Palette& pal = Ui::pal();
    Ui::background();
    Ui::header(profiles.empty() ? tr("Add a Profile") : tr("Who's watching?"));

    int totalRows = (int)profiles.size() + 1;
    int vis = (totalRows < MAX_VIS) ? totalRows : MAX_VIS;

    for (int i = 0; i < vis; i++) {
        focusAnim[i] = Ui::approach(focusAnim[i], i == focusedRow ? 1.0f : 0.0f);
        float f   = focusAnim[i];
        float ry  = ROW_Y0 + i * ROW_H;
        float g   = 6.0f * f;                       /* grow when focused */
        float x = ROW_X - g, w = ROW_W + g * 2, y = ry - g * 0.3f, h = ROW_H - 8 + g * 0.6f;
        bool isAdd = (i == (int)profiles.size());

        Ui::card(x, y, w, h, 16, f);
        float cx = x + 34, cy = y + h * 0.5f;
        if (isAdd) {
            Ui::circle(cx, cy, 20, Ui::mix(pal.cardBorder, pal.focus, f));
            Ui::circle(cx, cy, 17, pal.cardTop);
            Ui::textCentered(cx, cy - 15, "+", 26, Ui::mix(pal.textDim, pal.accentDark, f));
            Ui::text(x + 68, cy - 10, tr("Add New Profile"), 19, Ui::mix(pal.textDim, pal.accentDark, f));
            continue;
        }
        const SavedProfile& p = profiles[i];

        /* its picture, or the first letter of its name in a disc */
        Ui::avatar(cx, cy, 20, p.shownName().c_str(), 0.0f, p.picture);

        const char* name = !p.shownName().empty() ? p.shownName().c_str()
                         : !p.serverName.empty() ? p.serverName.c_str() : p.serverUrl.c_str();
        char server[96];
        if (!p.serverName.empty()) snprintf(server, sizeof(server), "%s  \xe2\x80\xa2  %s", p.serverName.c_str(), p.serverUrl.c_str());
        else                       snprintf(server, sizeof(server), "%s", p.serverUrl.c_str());
        Ui::text(x + 68, y + 9,  name,   20, pal.text);
        if (!p.nickname.empty() && !p.username.empty()) {   /* and the account's name after it */
            const int nw = Ui::textWidth(name, 20);
            Ui::text(x + 68 + nw + 10, y + 13, p.username.c_str(), 15, pal.textDim);
        }
        Ui::text(x + 68, y + 34, server, 13, pal.textDim);
    }

    /* Delete confirmation */
    if (confirmDelete) {
        GRRLIB_Rectangle(Ui::screenLeft(), 0, Ui::screenWidth(), 480, pal.dim, 1);
        Ui::card(140, 170, 360, 130, 20, 0.0f);
        Ui::textCentered(320, 192, tr("Delete this profile?"), 21, pal.danger);
        Ui::textCentered(320, 222, tr("Its saved sign-in will be forgotten."), 14, pal.textDim);
        Ui::button(170, 248, 140, 38, N_("Delete"), 17, confirmSel == 0 ? Ui::pulse() : 0.0f);
        Ui::button(330, 248, 140, 38, N_("Cancel"), 17, confirmSel == 1 ? Ui::pulse() : 0.0f);
    }

    /* Nickname: the name being typed over the list, the keyboard below */
    if (renaming) {
        GRRLIB_Rectangle(Ui::screenLeft(), 0, Ui::screenWidth(), 480, pal.dim, 1);
        Ui::card((640 - kb.width()) * 0.5f - 10, 104 - kb.lift(), kb.width() + 20, kb.height() + 120, 16, 0.0f);
        Ui::text((640 - kb.width()) * 0.5f + 4, 116 - kb.lift(), tr("Nickname"), 18, pal.text);
        Ui::textRight((640 + kb.width()) * 0.5f - 4, 120 - kb.lift(), tr("Leave it empty for the user name"), 13, pal.textDim);
        std::string shown = draft + "_";
        Ui::field((640 - kb.width()) * 0.5f, 150 - kb.lift(), kb.width(), 40, shown.c_str(), 18, true);
        kb.render(ir);
    }

    if (editing && !renaming && !picking) renderEdit();

    if (picking) {
        renderPicker(ir);
    } else if (renaming) {
        static const Ui::Hint left[] = { { "A", N_("Type") }, { "kbdel", N_("Delete") }, { "kbspace", N_("Space") }, { "kbsym", N_("Symbols") }, { "+!", N_("Done") } };
        Ui::bottomBar(left, 5);
    } else if (confirmDelete) {
        static const Ui::Hint left[] = { { "A", N_("Select") }, { "B", N_("Cancel") } };
        Ui::bottomBar(left, 2);
    } else if (editing) {
        static const Ui::Hint left[]  = { { "A", N_("Change") } };
        static const Ui::Hint right[] = { { "B", N_("Back") } };
        Ui::bottomBar(left, 1, right, 1);
    } else {
        static const Ui::Hint left[]  = { { "A", N_("Open") }, { "1", N_("Edit") }, { "2", N_("Picture") },
                                          { "-", N_("Delete") } };
        static const Ui::Hint right[] = { { "B", N_("Back") } };
        Ui::bottomBar(left, 4, right, 1);
    }

    /* IR cursor */
    if (ir.valid && cursorTex) {
        orient_t orient;
        WPAD_Orientation(Input::pointerChan(), &orient);
        if (!Ui::mouseCursor(ir.x, ir.y)) GRRLIB_DrawImg((int)ir.x - 20, (int)ir.y - 4,
                       cursorTex, Input::pointerRoll(), 1, 1, 0xFFFFFFFF);
    }
}

/* -----------------------------------------------------------------------
 * The picture: each picture in pictures/, each Mii of this Wii, then the
 * letter and the jellyfish; each group under its name, starting a row
 * ----------------------------------------------------------------------- */
static const int   PK_COLS = 5;
static const float PK_X = 70, PK_Y = 100, PK_CW = 100, PK_CH = 92, PK_HEAD = 26, PK_R = 30;
static const float PK_VIEW = 278;                     /* the rows' height on the card: three of pictures */

static float pickRowH(const char* header) { return header ? PK_HEAD : PK_CH; }

int ProfileView::pickRowOf(int sel) const
{
    for (int r = 0; r < (int)pickRows.size(); ++r)
        if (!pickRows[r].header && sel >= pickRows[r].first && sel < pickRows[r].first + pickRows[r].count) return r;
    return 0;
}

/* the selection's row in sight, and its group's name when that fits too */
void ProfileView::pickFollow()
{
    const int r = pickRowOf(pickSel);
    if (r < pickTop) pickTop = r;
    if (pickTop == r && r > 0 && pickRows[r - 1].header) pickTop = r - 1;
    for (;;) {
        float h = 0;
        for (int i = pickTop; i <= r; ++i) h += pickRowH(pickRows[i].header);
        if (h <= PK_VIEW || pickTop >= r) break;
        ++pickTop;
    }
}

void ProfileView::openPicker()
{
    choices.clear();
    pickRows.clear();
    /* a group: its name, then its choices PK_COLS a row */
    auto group = [this](const char* name, const std::vector<std::string>& ids) {
        if (ids.empty()) return;
        PickRow h;
        h.header = name;
        pickRows.push_back(h);
        for (size_t i = 0; i < ids.size(); i += PK_COLS) {
            PickRow r;
            r.first = (int)(choices.size() + i);
            r.count = (int)(ids.size() - i < (size_t)PK_COLS ? ids.size() - i : PK_COLS);
            pickRows.push_back(r);
        }
        choices.insert(choices.end(), ids.begin(), ids.end());
    };
    std::vector<std::string> miis;
    for (const Avatars::Mii& m : Avatars::miis()) miis.push_back(m.id);
    /* TRANSLATORS: the groups of profile pictures */
    group(N_("Pictures"), Avatars::files());
    group(N_("Miis"), miis);
    group(N_("WiiFin"), { std::string(), std::string(Avatars::JELLY) });
    pickSel = 0;
    for (int i = 0; i < (int)choices.size(); ++i) if (choices[i] == profiles[focusedRow].picture) pickSel = i;
    pickTop = 0;
    pickFollow();
    picking = true;
    SoundFX::play(SoundFX::FX::Select);
}

ProfileResult ProfileView::updatePicker(ir_t& ir)
{
    const int n = (int)choices.size(), was = pickSel;
    if (Input::isBackPressed()) { picking = false; SoundFX::play(SoundFX::FX::Back); return ProfileResult::None; }
    if (Input::isLeftPressed()  && pickSel > 0)     pickSel--;
    if (Input::isRightPressed() && pickSel < n - 1) pickSel++;
    /* up and down: the row of pictures above or below (past a group's
     * name), the same column or its last */
    const bool up = Input::isUpPressed(), down = Input::isDownPressed();
    if (up || down) {
        const int r = pickRowOf(pickSel), col = pickSel - pickRows[r].first;
        for (int t = r + (up ? -1 : 1); t >= 0 && t < (int)pickRows.size(); t += up ? -1 : 1) {
            if (pickRows[t].header) continue;
            pickSel = pickRows[t].first + (col < pickRows[t].count ? col : pickRows[t].count - 1);
            break;
        }
    }
    int hover = -1;
    if (ir.valid) {
        float y = PK_Y;
        for (int r = pickTop; r < (int)pickRows.size() && y + pickRowH(pickRows[r].header) <= PK_Y + PK_VIEW; y += pickRowH(pickRows[r].header), ++r) {
            if (pickRows[r].header || ir.y < y || ir.y >= y + PK_CH) continue;
            const int c = (int)((ir.x - PK_X) / PK_CW);
            if (ir.x >= PK_X && c < pickRows[r].count) hover = pickRows[r].first + c;
        }
    }
    if (hover >= 0) pickSel = hover;
    pickFollow();
    if (pickSel != was) SoundFX::play(SoundFX::FX::Move);
    if (Input::isAJustPressed() && (!ir.valid || hover >= 0)) {
        picking = false;
        editing = false;
        newPicture = choices[pickSel];
        selectedIdx = focusedRow;
        SoundFX::play(SoundFX::FX::Open);
        return ProfileResult::Picture;
    }
    return ProfileResult::None;
}

void ProfileView::renderPicker(ir_t& ir)
{
    const Ui::Palette& pal = Ui::pal();
    const SavedProfile& p = profiles[focusedRow];
    GRRLIB_Rectangle(Ui::screenLeft(), 0, Ui::screenWidth(), 480, pal.dim, 1);
    Ui::card(50, 52, 540, 364, 18, 0.0f);   /* above the bottom bar (424) */
    Ui::textCentered(320, 66, tr("Profile Picture"), 20, pal.text);
    float y = PK_Y, above = 0, all = 0;
    for (int r = 0; r < (int)pickRows.size(); ++r) {
        if (r < pickTop) above += pickRowH(pickRows[r].header);
        all += pickRowH(pickRows[r].header);
    }
    for (int r = pickTop; r < (int)pickRows.size() && y + pickRowH(pickRows[r].header) <= PK_Y + PK_VIEW; y += pickRowH(pickRows[r].header), ++r) {
        const PickRow& row = pickRows[r];
        if (row.header) {   /* the group's name, a line after it */
            const char* name = tr(row.header);
            Ui::text(PK_X + 6, y + 4, name, 15, pal.accentDark);
            const float lx = PK_X + 6 + Ui::textWidth(name, 15) + 10;
            GRRLIB_Rectangle(lx, y + 13, PK_X + PK_COLS * PK_CW - 6 - lx, 1, Ui::alpha(pal.textDim, 0.4f), 1);
            continue;
        }
        for (int c = 0; c < row.count; ++c) {
            const int i = row.first + c;
            const float x = PK_X + c * PK_CW;
            const bool sel = i == pickSel;
            Ui::avatar(x + PK_CW / 2, y + PK_R + 4, sel ? PK_R + 3 : PK_R, p.shownName().c_str(),
                       sel ? Ui::pulse() : 0.0f, choices[i]);
            const std::string label = choices[i].empty() ? std::string(tr("Letter")) : Avatars::label(choices[i]);
            std::string fit = label;
            while (fit.size() > 1 && Ui::textWidth(fit.c_str(), 13) > PK_CW - 8) fit.pop_back();
            Ui::textCentered(x + PK_CW / 2, y + PK_R * 2 + 14, fit.c_str(), 13, sel ? pal.accentDark : pal.textDim);
        }
    }
    if (all > PK_VIEW) Ui::scrollbar(574, PK_Y, PK_VIEW - 8, (int)above, (int)PK_VIEW, (int)all);
    Ui::textCentered(320, 380, Avatars::miis().empty() ? tr("No Miis found on this Wii.") : tr("Miis are local to this Wii's Mii Channel."),
                     13, pal.textDim);
    Ui::textCentered(320, 397, tr("Pictures of your own: PNG or JPEG files in apps/WiiFin/pictures/"), 13, pal.textDim);
    static const Ui::Hint left[]  = { { "A", N_("Choose") } };
    static const Ui::Hint right[] = { { "B", N_("Cancel") } };
    Ui::bottomBar(left, 1, right, 1);
}

/* -----------------------------------------------------------------------
 * Edit Profile: each detail a row, its value after it.  The server, the
 * user name and the password are typed on the sign-in screen: the
 * password isn't kept, so changing any of them signs in again
 * ----------------------------------------------------------------------- */
static const float ED_X = 110, ED_W = 420, ED_Y = 70, ED_ROW0 = 118, ED_ROW_H = 50;

ProfileResult ProfileView::updateEdit(ir_t& ir)
{
    const int was = editSel;
    if (Input::isBackPressed()) { editing = false; SoundFX::play(SoundFX::FX::Back); return ProfileResult::None; }
    if (Input::isUpPressed())   editSel = (editSel - 1 + EDIT_ROWS) % EDIT_ROWS;
    if (Input::isDownPressed()) editSel = (editSel + 1) % EDIT_ROWS;
    bool hover = false;
    if (ir.valid && ir.x >= ED_X + 16 && ir.x <= ED_X + ED_W - 16) {
        for (int i = 0; i < EDIT_ROWS; ++i) {
            const float y = ED_ROW0 + i * ED_ROW_H;
            if (ir.y >= y && ir.y < y + ED_ROW_H - 6) { editSel = i; hover = true; }
        }
    }
    if (editSel != was) SoundFX::play(SoundFX::FX::Move);
    if (!Input::isAJustPressed() || (ir.valid && !hover)) return ProfileResult::None;

    switch (editSel) {
    case 0:   /* the nickname: the keyboard, over this */
        renaming = true;
        draft = profiles[focusedRow].shownName();
        kb.reset();
        SoundFX::play(SoundFX::FX::Select);
        return ProfileResult::None;
    case 4:
        openPicker();
        return ProfileResult::None;
    default:  /* the server, the user name, the password */
        editing = false;
        editField = editSel - 1;
        selectedIdx = focusedRow;
        SoundFX::play(SoundFX::FX::Open);
        return ProfileResult::EditSignIn;
    }
}

/* s cut to w pixels, "..." after it if it was longer */
static std::string fitTo(std::string s, int size, float w)
{
    if (Ui::textWidth(s.c_str(), size) <= w) return s;
    while (!s.empty() && Ui::textWidth((s + "...").c_str(), size) > w) {
        s.pop_back();
        while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80) s.pop_back();   /* a whole UTF-8 character */
    }
    return s + "...";
}

void ProfileView::renderEdit()
{
    const Ui::Palette& pal = Ui::pal();
    const SavedProfile& p = profiles[focusedRow];
    GRRLIB_Rectangle(Ui::screenLeft(), 0, Ui::screenWidth(), 480, pal.dim, 1);
    Ui::card(ED_X, ED_Y, ED_W, ED_ROW0 - ED_Y + EDIT_ROWS * ED_ROW_H + 30, 18, 0.0f);
    Ui::textCentered(320, ED_Y + 14, tr("Edit Profile"), 20, pal.text);

    /* TRANSLATORS: Edit Profile, its rows */
    static const char* const names[EDIT_ROWS] = { N_("Nickname"), N_("Server URL"), N_("Username"), N_("Password"), N_("Picture") };
    const std::string values[EDIT_ROWS] = {
        p.nickname.empty() ? std::string(tr("None")) : p.nickname,
        p.serverUrl,
        p.username.empty() ? std::string(tr("Quick Connect")) : p.username,
        tr("Not kept on the Wii"),
        p.picture.empty() ? std::string(tr("Letter")) : Avatars::label(p.picture),
    };
    for (int i = 0; i < EDIT_ROWS; ++i) {
        const float y = ED_ROW0 + i * ED_ROW_H, x = ED_X + 16, w = ED_W - 32;
        const bool on = i == editSel;
        Ui::card(x, y, w, ED_ROW_H - 6, 12, on ? 1.0f : 0.0f);
        const char* name = tr(names[i]);
        Ui::text(x + 14, y + 11, name, 17, on ? pal.accentDark : pal.text);
        const float room = w - 28 - Ui::textWidth(name, 17) - 16;
        Ui::textRight(x + w - 14, y + 14, fitTo(values[i], 14, room).c_str(), 14, pal.textDim);
    }
    Ui::textCentered(320, ED_ROW0 + EDIT_ROWS * ED_ROW_H + 2,
                     tr("Changing the server, user name or password signs in again."), 13, pal.textDim);
}
