#pragma once
#include <vector>
#include <string>
#include <grrlib.h>
#include <wiiuse/wpad.h>
#include "../core/SavedProfile.h"
#include "Keyboard.h"

enum class ProfileResult {
    None,
    Selected,   /* selectedIdx is set — load that profile                */
    DeleteOne,  /* selectedIdx is set — caller removes it then re-loops  */
    AddNew,     /* user wants to connect a new account                   */
    Back,       /* user pressed B                                        */
    Rename,     /* selectedIdx is set — its nickname becomes newName     */
    Picture,    /* selectedIdx is set — its picture becomes newPicture   */
};

class ProfileView {
public:
    ProfileView(GRRLIB_ttfFont* font, GRRLIB_texImg* cursor,
                const std::vector<SavedProfile>& profiles);

    /* Call every frame; returns None while the screen is active. */
    ProfileResult update(ir_t& ir);
    void          render(ir_t& ir);

    int selectedIdx = 0; /* valid when result is Selected, DeleteOne or Rename */
    std::string newName; /* Rename: the nickname ("": the user name again) */
    std::string newPicture; /* Picture: "", "mii:<id>" or "file:<name>" (Avatars) */
    void setFocus(int row) { if (row >= 0 && row <= (int)profiles.size()) focusedRow = row; }

private:
    GRRLIB_ttfFont* font;
    GRRLIB_texImg*  cursorTex;
    const std::vector<SavedProfile>& profiles;

    int  focusedRow    = 0;
    bool irMode        = false;
    bool confirmDelete = false;
    int  confirmSel    = 1;       /* the question's button: 0 Delete, 1 Cancel */
    bool renaming      = false;   /* 1: the keyboard, for its nickname */
    Keyboard    kb;
    std::string draft;

    /* 2: its picture chosen: pictures/, the Miis, then the letter and the
     * jellyfish, each group under its name */
    bool picking = false;
    std::vector<std::string> choices;
    struct PickRow { const char* header = nullptr; int first = 0, count = 0; };   /* a name, or choices[first..] */
    std::vector<PickRow> pickRows;
    int  pickSel = 0, pickTop = 0;    /* pickTop: the first of pickRows shown */
    int  pickRowOf(int sel) const;
    void pickFollow();
    void openPicker();
    ProfileResult updatePicker(ir_t& ir);
    void renderPicker(ir_t& ir);

    static const int ROW_H   = 66;
    static const int ROW_Y0  = 74;
    static const int ROW_X   = 70;
    static const int ROW_W   = 500;
    static const int MAX_VIS = 5; /* max visible rows (profiles + Add New) */

    float focusAnim[MAX_VIS] = {};
};
