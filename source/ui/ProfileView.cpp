#include "ProfileView.h"
#include "../core/Text.h"
#include "Ui.h"
#include "../input/Input.h"
#include "../core/SoundFX.h"
#include "../core/Lang.h"
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

    /* its nickname typed: Done keeps it (nothing: the user name again) */
    if (renaming) {
        kb.setOrigin((640 - kb.width()) * 0.5f, 214);
        const Keyboard::Result r = kb.update(ir, draft, 32);
        if (r == Keyboard::Result::Cancel) { renaming = false; SoundFX::play(SoundFX::FX::Back); }
        if (r == Keyboard::Result::Enter) {
            renaming = false;
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

    /* 1 = a nickname for the profile */
    if (Input::is1Pressed() && focusedRow < (int)profiles.size()) {
        renaming = true;
        draft = profiles[focusedRow].shownName();
        kb.reset();
        SoundFX::play(SoundFX::FX::Select);
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
            Ui::circle(cx, cy, 20, Ui::mix(pal.cardBorder, pal.accent, f));
            Ui::circle(cx, cy, 17, pal.cardTop);
            Ui::textCentered(cx, cy - 15, "+", 26, Ui::mix(pal.textDim, pal.accentDark, f));
            Ui::text(x + 68, cy - 10, tr("Add New Profile"), 19, Ui::mix(pal.textDim, pal.accentDark, f));
            continue;
        }
        const SavedProfile& p = profiles[i];

        /* Avatar: first letter of its name in a Wii-blue disc */
        Ui::avatar(cx, cy, 20, p.shownName().c_str(), 0.0f);

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
        Ui::card((640 - kb.width()) * 0.5f - 10, 104, kb.width() + 20, kb.height() + 120, 16, 0.0f);
        Ui::text((640 - kb.width()) * 0.5f + 4, 116, tr("Nickname"), 18, pal.text);
        Ui::textRight((640 + kb.width()) * 0.5f - 4, 120, tr("Leave it empty for the user name"), 13, pal.textDim);
        std::string shown = draft + "_";
        Ui::field((640 - kb.width()) * 0.5f, 150, kb.width(), 40, shown.c_str(), 18, true);
        kb.render(ir);
    }

    if (renaming) {
        static const Ui::Hint left[] = { { "A", N_("Type") }, { "B", N_("Delete") }, { "+", N_("Done") } };
        Ui::bottomBar(left, 3);
    } else if (confirmDelete) {
        static const Ui::Hint left[] = { { "A", N_("Select") }, { "B", N_("Cancel") } };
        Ui::bottomBar(left, 2);
    } else {
        static const Ui::Hint left[]  = { { "A", N_("Open") }, { "1", N_("Nickname") }, { "-", N_("Delete") } };
        static const Ui::Hint right[] = { { "B", N_("Back") } };
        Ui::bottomBar(left, 3, right, 1);
    }

    /* IR cursor */
    if (ir.valid && cursorTex) {
        orient_t orient;
        WPAD_Orientation(Input::pointerChan(), &orient);
        if (!Ui::mouseCursor(ir.x, ir.y)) GRRLIB_DrawImg((int)ir.x - 20, (int)ir.y - 4,
                       cursorTex, Input::pointerRoll(), 1, 1, 0xFFFFFFFF);
    }
}
